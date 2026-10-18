# Test plan and results

## Evidence strategy

Testing follows a layered argument: portable unit tests establish deterministic
protocol semantics; cross-compilation establishes toolchain and memory-layout
feasibility; instrumented hardware tests establish physical behavior. Evidence
from one layer must not be promoted to another.

| Layer | Question answered | Current status |
|---|---|---|
| Host unit tests | Are framing, validation, retries and scheduling deterministic? | Passed |
| Target builds | Do all sources link and fit their configured images? | Passed |
| Bench integration | Do UART, I2S, audio and failure containment work physically? | Partial: USB protocol and audio passed |
| Electrical qualification | Are power, thermal, backfeed and isolation margins acceptable? | Not run |

## Run on 2026-10-18

```sh
cmake -S . -B /tmp/halmet-guardian-host-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/halmet-guardian-host-build --parallel
ctest --test-dir /tmp/halmet-guardian-host-build --output-on-failure
```

Result: 3/3 passed (`guardian_protocol_test`, `guardian_speech_server_test`,
`guardian_uart_backend_test`).
The tests cover the CRC check value and golden wire vector, byte-at-a-time
fragmentation, noise resynchronization, corrupt CRC, payload overflow, valid and
invalid UTF-8, message validation, priority ordering, duplicate idempotence,
critical interruption and malformed SPEAK rejection.
They also cover RGB control, master-volume validation and restoration framing,
the atomic six-language TEST_ALL batch, and its single lifecycle.

PlatformIO 6.1.19 was available by absolute path. `pio project config` passed.
Both `pio run -e halmet` (preserved example) and `pio run -e halmet_guardian`
(distributed Guardian) passed. The former used 53,300 bytes RAM / 1,781,092
bytes flash; the latter 53,936 / 1,730,316. These Arduino builds are compile
checks: the repository warns that the production HALMET build needs the
ESP-IDF-from-source TLS configuration.

`pio run -e esp32s3_speech` passed with all six languages. PlatformIO measured
7,316,691 bytes flash and 37,812 bytes internal RAM with BLE linked. Each A/B
application slot is 0x760000 (7,733,248 bytes), leaving 416,557 bytes of
headroom per slot.

## Hardware-required tests

On 2026-10-18, the S3 was flashed over USB and returned valid CRC-framed
HELLO, PONG, ACK, STARTED and DONE responses. SET_RGB was exercised for red,
green, blue and off. PicoTTS audio became audible after the MAX98357A SD pin was
tied high, and TEST_ALL completed all six voices with one ACK/STARTED/DONE
lifecycle. SET_VOLUME set the master level to 60% over USB and returned
ACK/DONE; a following speech request returned ACK/STARTED/DONE and was audible.
The reusable Python CLI also completed a PING/PONG exchange. Host tests cover
invalid volume rejection and HALMET restoring its configured volume after HELLO.
The BLE regression returned enabled=true, wifi_off=true and zero bonds, found
ten devices in a three-second scan, survived disable/re-enable, and completed a
following English SPEAK lifecycle.

HALMET Bluetooth/HID coverage verifies encoding BT status/scan/connect/test,
decoding BT_STATE and BT_DEVICE, and delivering raw BT_HID_EVENT payloads to
the application callback. The production HALMET image calls `btStop()` before
SensESP starts, leaving Wi-Fi and Signal K active while the S3 owns BLE. The
2026-10-18 HALMET build used 54,044 bytes RAM and 1,747,952 bytes flash and was
flashed with a verified hash to the 16 MB ESP32 on `/dev/cu.usbserial-1140`.

The USB developer route was hardware-tested without GPIO UART wiring. HALMET's
USB build emitted SET_VOLUME, BT_GET_STATUS and SPEAK; the router forwarded
valid frames to the S3 USB port; S3 returned BT_STATE and a complete
ACK/STARTED/DONE speech lifecycle. The automated check exited successfully
after matching DONE to the routed SPEAK sequence.

On 2026-10-19, repeated HALMET resets exposed a cross-boot idempotence issue:
the HALMET sequence counter restarted at one while the continuously powered S3
still remembered that sequence and acknowledged the startup SPEAK without
playing it. HALMET now seeds each boot's nonzero sequence from `esp_random()`.
The repaired USB image was flashed with verified hashes and tested through two
consecutive routed resets. The startup sentence `Benvenuti a bordo!` completed
ACK/STARTED/DONE both times, using distinct SPEAK sequences `0xDD3C` and
`0x8D18`.

The compile-time USB firmware variant was then removed. The single
`halmet_guardian` image now provides Auto, GPIO and USB runtime transport
settings, with Auto as the migration-safe default. The unified image used
54,044 bytes RAM and 1,752,400 bytes flash, was flashed with verified hashes,
and its Auto mode completed SET_VOLUME, BT_GET_STATUS and the startup SPEAK
over the USB router. The startup sequence `0xB23B` returned
ACK/STARTED/DONE.

The SensESP UI subsequently replaced the saved Bluetooth action selector with
native Control-page buttons. Each operation is tracked by its exact protocol
sequence through DONE or ERROR and publishes progress/results on the Status
page. HALMET also sends a PING every two seconds and reports the S3 link down
when no valid response has arrived for five seconds. Host protocol tests pass
for the heartbeat counters and terminal-sequence tracking. The resulting
HALMET image used 54,084 bytes RAM and 1,757,216 bytes flash and was flashed
with verified hashes. End-to-end heartbeat verification remains pending
because the S3 USB serial device was not present after flashing.

The following broader integration and qualification tests remain open:

- HALMET continues Signal K/N2K alarm processing, LED/OLED, ACK and snooze with
  S3 absent, resetting, stalled and queue-full.
- Signal K/N2K source transition produces no duplicate speech.
- Italian and one other language play through MAX98357A; DONE follows physical
  PCM drain; STOP and critical preemption leave BUSY low.
- UART corruption, reset mid-frame and boot logs resynchronize without speech.
- Current/rail droop, GPIO logic, USB backfeed and isolation are measured.
- Six-language image size and a power-loss matrix are measured against the final
  partition table before update support is enabled.
- ROM recovery is exercised only after open-drain EN/GPIO0 controls exist.

No power meter or oscilloscope evidence is available, so electrical,
waveform-timing, peak-current and thermal qualification remain unverified.
