#!/usr/bin/env python3
"""Command-line client for the Guardian ESP32-S3 serial protocol."""

from __future__ import annotations

import argparse
import binascii
import re
import struct
import sys
import time
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


MAGIC = b"GS"
VERSION = 1
MAX_PAYLOAD = 512

TYPES = {
    "hello": 0x01,
    "caps": 0x02,
    "ping": 0x03,
    "pong": 0x04,
    "speak": 0x10,
    "stop": 0x11,
    "status": 0x12,
    "ack": 0x13,
    "started": 0x14,
    "done": 0x15,
    "error": 0x16,
    "set-rgb": 0x18,
    "test-all": 0x19,
    "set-volume": 0x1A,
    "bt-status": 0x30,
    "bt-enable": 0x31,
    "bt-scan": 0x32,
    "bt-scan-stop": 0x33,
    "bt-connect": 0x34,
    "bt-disconnect": 0x35,
    "bt-forget": 0x36,
    "bt-state": 0x37,
    "bt-device": 0x38,
    "bt-hid-event": 0x39,
    "bt-test": 0x3A,
}
TYPE_NAMES = {value: name.upper().replace("-", "_") for name, value in TYPES.items()}
ERROR_NAMES = {
    1: "malformed",
    2: "invalid UTF-8",
    3: "queue full",
    4: "unsupported",
    5: "playback",
    6: "update rejected",
}
PRIORITIES = {"low": 0, "normal": 1, "high": 2, "critical": 3}


@dataclass
class Frame:
    kind: int
    sequence: int
    payload: bytes


def bounded(value: str, minimum: int, maximum: int) -> int:
    number = int(value)
    if not minimum <= number <= maximum:
        raise argparse.ArgumentTypeError(f"must be between {minimum} and {maximum}")
    return number


def percent(value: str) -> int:
    return bounded(value, 0, 100)


def voice_setting(value: str) -> int:
    return bounded(value, 50, 200)


def byte_value(value: str) -> int:
    return bounded(value, 0, 255)


def bluetooth_address(value: str) -> str:
    if not re.fullmatch(r"[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}", value):
        raise argparse.ArgumentTypeError("must be a Bluetooth address such as aa:bb:cc:dd:ee:ff")
    return value


def encode(kind: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 512 bytes")
    body = bytes((VERSION, kind)) + struct.pack("<HH", sequence, len(payload)) + payload
    return MAGIC + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))


class Parser:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self.buffer.extend(data)
        frames: list[Frame] = []
        while len(self.buffer) >= 10:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 10:
                break
            length = int.from_bytes(self.buffer[6:8], "little")
            if self.buffer[2] != VERSION or length > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            end = 10 + length
            if len(self.buffer) < end:
                break
            body = bytes(self.buffer[2 : 8 + length])
            received_crc = int.from_bytes(self.buffer[8 + length : end], "little")
            if binascii.crc_hqx(body, 0xFFFF) == received_crc:
                frames.append(
                    Frame(
                        self.buffer[3],
                        int.from_bytes(self.buffer[4:6], "little"),
                        bytes(self.buffer[8 : 8 + length]),
                    )
                )
            del self.buffer[:end]
        return frames


def describe(frame: Frame) -> str:
    name = TYPE_NAMES.get(frame.kind, f"0x{frame.kind:02X}")
    detail = ""
    if frame.kind == TYPES["error"] and frame.payload:
        detail = f" ({ERROR_NAMES.get(frame.payload[0], f'code {frame.payload[0]}')})"
    elif frame.kind == TYPES["bt-state"] and len(frame.payload) == 3:
        flags = frame.payload[1]
        detail = (f" version={frame.payload[0]} enabled={bool(flags & 1)}"
                  f" scanning={bool(flags & 2)} connected={bool(flags & 4)}"
                  f" wifi_off={bool(flags & 8)} bonds={frame.payload[2]}")
    elif frame.kind == TYPES["bt-device"] and len(frame.payload) >= 19:
        address = frame.payload[:17].decode("ascii", "replace")
        address_type = frame.payload[17]
        rssi = struct.unpack("b", frame.payload[18:19])[0]
        device_name = frame.payload[19:].decode("utf-8", "replace")
        detail = f" address={address} type={address_type} rssi={rssi} name={device_name!r}"
    elif frame.kind == TYPES["bt-hid-event"] and len(frame.payload) >= 3:
        handle = int.from_bytes(frame.payload[:2], "little")
        detail = f" handle=0x{handle:04X} hid={frame.payload[2:].hex().upper()}"
    elif frame.payload:
        detail = f" payload={frame.payload.hex(' ')}"
    return f"{name} seq=0x{frame.sequence:04X}{detail}"


def transact(port: str, baud: int, timeout: float, request: Frame,
             expected: list[int]) -> None:
    parser = Parser()
    seen: list[int] = []
    with serial.Serial(port, baud, timeout=0.1) as link:
        link.reset_input_buffer()
        link.write(encode(request.kind, request.sequence, request.payload))
        link.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for response in parser.feed(link.read(522)):
                print(describe(response))
                if response.sequence != request.sequence:
                    continue
                if response.kind == TYPES["error"]:
                    raise SystemExit(2)
                if response.kind in expected:
                    seen.append(response.kind)
                    if seen == expected:
                        return
        wanted = ", ".join(TYPE_NAMES[kind] for kind in expected)
        received = ", ".join(TYPE_NAMES.get(kind, hex(kind)) for kind in seen)
        raise SystemExit(f"timeout waiting for {wanted}; received: {received or 'nothing'}")


def make_request(args: argparse.Namespace) -> tuple[Frame, list[int], float]:
    sequence = args.sequence or ((int(time.time() * 1000) & 0xFFFF) or 1)
    if args.command == "ping":
        return Frame(TYPES["ping"], sequence, b""), [TYPES["pong"]], args.timeout
    if args.command == "stop":
        expected = [TYPES["ack"], TYPES["done"]]
        return Frame(TYPES["stop"], sequence, b""), expected, args.timeout
    if args.command == "rgb":
        expected = [TYPES["ack"], TYPES["done"]]
        return Frame(TYPES["set-rgb"], sequence, bytes((args.red, args.green, args.blue))), expected, args.timeout
    if args.command == "volume":
        expected = [TYPES["ack"], TYPES["done"]]
        return Frame(TYPES["set-volume"], sequence, bytes((args.percent,))), expected, args.timeout
    if args.command == "test-all":
        expected = [TYPES["ack"], TYPES["started"], TYPES["done"]]
        return Frame(TYPES["test-all"], sequence, b""), expected, max(args.timeout, 90.0)
    if args.command == "bt-status":
        expected = [TYPES["ack"], TYPES["bt-state"], TYPES["done"]]
        return Frame(TYPES["bt-status"], sequence, b""), expected, args.timeout
    if args.command == "bt-enable":
        expected = [TYPES["ack"], TYPES["bt-state"], TYPES["done"]]
        return Frame(TYPES["bt-enable"], sequence, bytes((args.enabled,))), expected, args.timeout
    if args.command == "bt-scan":
        expected = [TYPES["ack"], TYPES["started"], TYPES["done"]]
        return Frame(TYPES["bt-scan"], sequence, bytes((args.seconds,))), expected, max(args.timeout, args.seconds + 3.0)
    if args.command == "bt-scan-stop":
        return Frame(TYPES["bt-scan-stop"], sequence, b""), [TYPES["ack"], TYPES["done"]], args.timeout
    if args.command == "bt-connect":
        payload = args.address.lower().encode("ascii") + bytes((args.address_type,))
        return Frame(TYPES["bt-connect"], sequence, payload), [TYPES["ack"], TYPES["started"], TYPES["done"]], max(args.timeout, 8.0)
    if args.command == "bt-disconnect":
        return Frame(TYPES["bt-disconnect"], sequence, b""), [TYPES["ack"], TYPES["done"]], args.timeout
    if args.command == "bt-forget":
        return Frame(TYPES["bt-forget"], sequence, b""), [TYPES["ack"], TYPES["done"]], args.timeout
    if args.command == "bt-test":
        return Frame(TYPES["bt-test"], sequence, b""), [TYPES["ack"], TYPES["bt-hid-event"], TYPES["done"]], args.timeout
    if args.command == "speak":
        language = args.language.encode("ascii")
        text = args.text.encode("utf-8")
        if not 1 <= len(language) <= 15:
            raise SystemExit("language must contain 1-15 ASCII bytes")
        payload = bytes(
            (PRIORITIES[args.priority], args.volume, args.pitch, args.speed, len(language))
        ) + language + text
        expected = [TYPES["ack"], TYPES["started"], TYPES["done"]]
        return Frame(TYPES["speak"], sequence, payload), expected, max(args.timeout, 60.0)
    raise AssertionError(args.command)


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial device, for example /dev/cu.usbmodem…")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--sequence", type=lambda value: bounded(value, 1, 65535))
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("ping", help="verify PING/PONG")
    commands.add_parser("stop", help="stop speech and clear the queue")

    rgb = commands.add_parser("rgb", help="set the S3 RGB LED")
    rgb.add_argument("red", type=byte_value)
    rgb.add_argument("green", type=byte_value)
    rgb.add_argument("blue", type=byte_value)

    volume = commands.add_parser("volume", help="set master output volume")
    volume.add_argument("percent", type=percent)
    commands.add_parser("test-all", help="speak one sentence in every installed language")
    commands.add_parser("bt-status", help="show BLE state and confirm Wi-Fi is off")
    bt_enable = commands.add_parser("bt-enable", help="enable (1) or disable (0) BLE")
    bt_enable.add_argument("enabled", type=lambda value: bounded(value, 0, 1))
    bt_scan = commands.add_parser("bt-scan", help="scan asynchronously for BLE peripherals")
    bt_scan.add_argument("seconds", nargs="?", type=lambda value: bounded(value, 1, 30), default=5)
    commands.add_parser("bt-scan-stop", help="stop an active BLE scan")
    bt_connect = commands.add_parser("bt-connect", help="connect to a scanned BLE address")
    bt_connect.add_argument("address", type=bluetooth_address)
    bt_connect.add_argument("--address-type", type=lambda value: bounded(value, 0, 3), default=0,
                            help="0 public, 1 random, 2 public identity, 3 random identity")
    commands.add_parser("bt-disconnect", help="disconnect the current BLE peripheral")
    commands.add_parser("bt-forget", help="delete all stored BLE bonds")
    commands.add_parser("bt-test", help="replay the last received HID report")

    speak = commands.add_parser("speak", help="speak UTF-8 text")
    speak.add_argument("text")
    speak.add_argument("--language", default="it-IT")
    speak.add_argument("--priority", choices=PRIORITIES, default="normal")
    speak.add_argument("--volume", type=percent, default=100,
                       help="per-request volume before master scaling")
    speak.add_argument("--pitch", type=voice_setting, default=100)
    speak.add_argument("--speed", type=voice_setting, default=100)
    return parser


def main() -> None:
    args = argument_parser().parse_args()
    request, expected, timeout = make_request(args)
    transact(args.port, args.baud, timeout, request, expected)


if __name__ == "__main__":
    main()
