# Test plan and results

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

PlatformIO 6.1.19 was available by absolute path. `pio project config` passed.
Both `pio run -e halmet` (preserved example) and `pio run -e halmet_guardian`
(distributed Guardian) passed. The former used 53,300 bytes RAM / 1,781,132
bytes flash; the latter 53,936 / 1,730,308. These Arduino builds are compile
checks: the repository warns that the production HALMET build needs the
ESP-IDF-from-source TLS configuration.

`pio run -e esp32s3_speech` passed with all six languages. PlatformIO measured
6,950,730 bytes flash and 22,676 bytes internal RAM. Each A/B application slot
is 0x760000 (7,733,248 bytes), leaving 782,518 bytes of headroom per slot.

## Hardware-required tests (not run)

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

No serial port, S3 board, amplifier, speaker, power meter or oscilloscope was
available, so none of these bench tests is reported passed.
