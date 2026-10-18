# Upstream audit

Audit date: 2026-10-18. No hardware was attached during this audit.

## Target checkout

The target started clean at `20d2a944cf4df9d7ca3ad97036b644037d31db67`
(`OpenFairWind/HALMET-S3-Outlook-Guardian`, `main`). It was the HALMET example
firmware rather than an Outlook Guardian port:

- `src/main.cpp:1-16` identifies a Signal K application template and enables
  Signal K plus NMEA 2000 output. It contains tank, tachometer and digital input
  examples, but no Guardian alarm table, notification listener or speech code.
- `platformio.ini:8-14` makes `halmet_espidf` the production build and documents
  the Arduino-only `halmet` environment as compile-check-only for TLS.
- `platformio.ini:21-26` used SensESP `^3.5.0`, SSD1306 `^2.5.1`, NMEA2000
  `^4.17.2`, the `NMEA2000_twai` Git dependency and ADS1X15 `^2.3.0`.
- `platformio.ini:34-38` selects the existing 8 MB, two-app HALMET partition
  table. This is unrelated to the S3 speech image layout.
- `src/halmet_const.h:8-25` assigns I2C 21/22, CAN 18/19 and digital inputs
  23/25/27/26. It provides no connector physical pin numbers or power limits.
- `src/main.cpp:101-110` uses GPIO33 as a test PWM output. GPIO33 therefore
  is now disabled so GPIO33 can be the speech BUSY input.
- The root license is Apache-2.0. There was no `NOTICE` file and no target
  `AGENTS.md`. `test/README` was the stock PlatformIO placeholder and no tests
  were present.

`pio` was not on `PATH`, but PlatformIO 6.1.19 was found at
`~/.platformio/penv/bin/platformio`. The `halmet` compile-check build passed with
SensESP 3.6.0 resolved by the declared `^3.5.0` constraint. It used 53,300 bytes
RAM and 1,781,132 bytes flash after the portable protocol objects were added.

## Guardian reference

The named repository `OpenFairWind/S3-Outlook-Guardian` was audited at
`6506995d056464802c0f8dcfc09fb31d2efb800e` (shallow clone). It is a SensESP
framework fork, Apache-2.0, with the Guardian application in
`examples/outlook_guardian.cpp` and PicoTTS as a submodule wrapper under
`lib/picotts` (the underlying DiUS engine is Apache-2.0).

Important compatibility points verified in that source:

- `GuardianConfig` begins at line 110 and retains the paths
  `/Outlook Guardian/Speech` and `/Outlook Guardian/Sources` at lines
  1236 and 1243.
- `kVoices` at line 105 maps Standard/Deep/High to pitches 100/80/120.
- `process_alarms()` at line 553 implements severity-first scheduling,
  repeat, acknowledgement and snooze. It must remain the sole alarm scheduler.
- `handle_sk_notification()` at line 841 implements Signal K state/method/status
  semantics; `handle_n2k_message()` at line 1126 implements PGN 126983/126985
  fallback. Source switching discards the inactive source, preventing duplicate
  announcements.
- `kPhrases` at line 1016 retains language-specific NMEA alert composition.
- The reference uses GPIO4/5/6 for LRC/BCLK/DIN and a single 0xA00000 partition.
  The implemented image measures 6,950,730 bytes and uses two 0x760000 A/B slots.

The upstream `AGENTS.md` applies to work in that framework repository, not to
this target. Its useful build note is that native tests cover portable headers
while Arduino/SensESP components require an embedded compile check.

## Audit conclusions

The upstream alarm application is retained in `src/guardian_main.cpp` and its
config paths, Signal K semantics, N2K fallback, phrase table and OLED/LED logic
are unchanged. Its speech call is routed through `UartSpeechBackend`. It builds
as the separate `halmet_guardian` environment (53,936 bytes RAM, 1,730,308 bytes
flash). The original `halmet` environment and `src/main.cpp` remain available,
so the prior tank/tachometer example is not silently replaced.

No HALMET schematic, PCB revision, connector drawing, S3 carrier schematic,
board photo, rail-current measurement or S3 binary/map file was present. Pin,
power, isolation, OTA and ROM-recovery claims remain conditional.
