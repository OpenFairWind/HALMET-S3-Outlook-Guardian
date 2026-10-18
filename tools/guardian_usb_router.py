#!/usr/bin/env python3
"""Route Guardian protocol frames between HALMET and S3 USB serial ports.

HALMET developer firmware shares its USB UART with ESP-IDF logs. This router
forwards only version-1, CRC-valid Guardian frames; ordinary log text is never
sent to the S3.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


MAGIC = b"GS"
VERSION = 1
MAX_PAYLOAD = 512
TYPE_NAMES = {
    0x01: "HELLO", 0x02: "CAPS", 0x03: "PING", 0x04: "PONG",
    0x10: "SPEAK", 0x11: "STOP", 0x12: "STATUS", 0x13: "ACK",
    0x14: "STARTED", 0x15: "DONE", 0x16: "ERROR", 0x17: "SET_CONFIG",
    0x18: "SET_RGB", 0x19: "TEST_ALL", 0x1A: "SET_VOLUME",
    0x30: "BT_GET_STATUS", 0x31: "BT_SET_ENABLED", 0x32: "BT_SCAN",
    0x33: "BT_SCAN_STOP", 0x34: "BT_CONNECT", 0x35: "BT_DISCONNECT",
    0x36: "BT_FORGET", 0x37: "BT_STATE", 0x38: "BT_DEVICE",
    0x39: "BT_HID_EVENT", 0x3A: "BT_TEST",
}


class FrameExtractor:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> tuple[list[bytes], bytes]:
        self.buffer.extend(data)
        frames: list[bytes] = []
        noise = bytearray()
        while self.buffer:
            start = self.buffer.find(MAGIC)
            if start < 0:
                keep = 1 if self.buffer[-1:] == MAGIC[:1] else 0
                if len(self.buffer) > keep:
                    noise.extend(self.buffer[:-keep] if keep else self.buffer)
                    if keep:
                        del self.buffer[:-keep]
                    else:
                        del self.buffer[:]
                break
            if start:
                noise.extend(self.buffer[:start])
                del self.buffer[:start]
            if len(self.buffer) < 8:
                break
            length = int.from_bytes(self.buffer[6:8], "little")
            if self.buffer[2] != VERSION or length > MAX_PAYLOAD:
                noise.append(self.buffer[0])
                del self.buffer[0]
                continue
            total = 10 + length
            if len(self.buffer) < total:
                break
            body = bytes(self.buffer[2:8 + length])
            received = int.from_bytes(self.buffer[8 + length:total], "little")
            if binascii.crc_hqx(body, 0xFFFF) == received:
                frames.append(bytes(self.buffer[:total]))
                del self.buffer[:total]
            else:
                noise.append(self.buffer[0])
                del self.buffer[0]
        return frames, bytes(noise)


def describe(wire: bytes) -> tuple[int, int, str]:
    kind = wire[3]
    sequence = int.from_bytes(wire[4:6], "little")
    length = int.from_bytes(wire[6:8], "little")
    payload = wire[8:8 + length]
    detail = ""
    if kind == 0x1A and len(payload) == 1:
        detail = f" volume={payload[0]}%"
    elif kind == 0x10 and len(payload) >= 6:
        language_length = payload[4]
        if 5 + language_length <= len(payload):
            language = payload[5:5 + language_length].decode("ascii", "replace")
            message = payload[5 + language_length:].decode("utf-8", "replace")
            detail = f" language={language} volume={payload[1]}% text={message!r}"
    return kind, sequence, (f"{TYPE_NAMES.get(kind, f'0x{kind:02X}')} "
                            f"seq=0x{sequence:04X} len={length}{detail}")


def reset_halmet(link: serial.Serial) -> None:
    # ESP32 auto-reset: keep GPIO0 high (DTR inactive), pulse EN through RTS.
    link.dtr = False
    link.rts = True
    time.sleep(0.12)
    link.rts = False
    time.sleep(0.8)
    link.reset_input_buffer()


def run(args: argparse.Namespace) -> int:
    halmet_parser = FrameExtractor()
    s3_parser = FrameExtractor()
    speech_sequences: set[int] = set()
    completed_sequences: set[int] = set()
    halmet_frames = 0
    s3_frames = 0
    with serial.Serial(args.halmet, args.baud, timeout=0) as halmet, \
            serial.Serial(args.s3, args.baud, timeout=0) as s3:
        if args.reset_halmet:
            reset_halmet(halmet)
        started = time.monotonic()
        print(f"routing {args.halmet} ⇄ {args.s3} at {args.baud} baud", flush=True)
        while args.duration <= 0 or time.monotonic() - started < args.duration:
            progressed = False
            data = halmet.read(halmet.in_waiting or 1)
            if data:
                progressed = True
                frames, noise = halmet_parser.feed(data)
                if args.show_logs and noise:
                    text = noise.decode("utf-8", "replace")
                    if text:
                        print(text, end="", file=sys.stderr, flush=True)
                for frame in frames:
                    kind, sequence, label = describe(frame)
                    s3.write(frame)
                    s3.flush()
                    halmet_frames += 1
                    if kind == 0x10:
                        speech_sequences.add(sequence)
                    print(f"HALMET → S3  {label}", flush=True)
            data = s3.read(s3.in_waiting or 1)
            if data:
                progressed = True
                frames, _ = s3_parser.feed(data)
                for frame in frames:
                    kind, sequence, label = describe(frame)
                    halmet.write(frame)
                    halmet.flush()
                    s3_frames += 1
                    if kind == 0x15 and sequence in speech_sequences:
                        completed_sequences.add(sequence)
                    print(f"S3 → HALMET  {label}", flush=True)
            if args.require_speech and completed_sequences:
                print("verified routed SPEAK → ACK/STARTED/DONE lifecycle", flush=True)
                return 0
            if not progressed:
                time.sleep(0.002)
    if args.require_speech:
        print(f"no complete speech lifecycle (HALMET frames={halmet_frames}, S3 frames={s3_frames})",
              file=sys.stderr)
        return 2
    return 0


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--halmet", required=True, help="HALMET USB serial port")
    parser.add_argument("--s3", required=True, help="ESP32-S3 USB serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=0,
                        help="seconds to run; zero runs until interrupted")
    parser.add_argument("--reset-halmet", action="store_true",
                        help="reset HALMET after both ports are open")
    parser.add_argument("--require-speech", action="store_true",
                        help="exit successfully only after a routed speech lifecycle")
    parser.add_argument("--show-logs", action="store_true",
                        help="print HALMET non-protocol serial output to stderr")
    return parser


if __name__ == "__main__":
    raise SystemExit(run(argument_parser().parse_args()))
