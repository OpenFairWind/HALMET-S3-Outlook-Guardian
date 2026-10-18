# Getting started: reproducible development playground

This tutorial creates a clean software playground, verifies the pinned PicoTTS
source, runs portable tests and builds all firmware variants. It deliberately
stops before flashing hardware. Commands assume a POSIX shell; use PowerShell
equivalents on Windows and keep the checkout path short and free of spaces.

## 1. Install prerequisites

Install Git, CMake 3.16 or newer, a C++17 compiler, Python 3, and PlatformIO
Core. One reproducible Python installation method is:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip platformio
```

Confirm the tools rather than assuming the active shell is configured:

```sh
git --version
cmake --version
python --version
pio --version
```

PlatformIO downloads compiler packages during the first embedded build, so the
first run requires network access and may take several minutes.

## 2. Clone the complete source graph

PicoTTS is a pinned Git submodule. Omitting `--recursive` produces an incomplete
six-language image.

```sh
git clone --recursive https://github.com/OpenFairWind/HALMET-S3-Outlook-Guardian.git
cd HALMET-S3-Outlook-Guardian
git submodule status
```

An existing non-recursive checkout can be repaired with:

```sh
git submodule update --init --recursive
```

The submodule status must begin with a space, not `-` or `+`. Record the main
and submodule revisions in experimental notes:

```sh
git rev-parse HEAD
git submodule status
```

## 3. Run the hardware-independent tests

Use an out-of-tree build so generated files do not contaminate the checkout:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

The expected suite contains `guardian_protocol_test`,
`guardian_speech_server_test`, and `guardian_uart_backend_test`. These establish
framing/CRC behavior, resynchronization, UTF-8 validation, queue ordering,
duplicate idempotence, critical preemption and HALMET retry behavior. They do
not exercise a UART peripheral or loudspeaker.

## 4. Build the preserved HALMET example

```sh
pio run -e halmet
```

This fast Arduino-only environment ensures that existing HALMET functionality
still compiles. It is not the TLS-qualified image to flash; see the root README
for the `halmet_espidf` production distinction.

## 5. Build the distributed HALMET application

```sh
pio run -e halmet_guardian
```

This environment compiles the upstream Guardian alarm state machine with the
nonblocking UART speech backend. The configured HALMET UART is RX GPIO17 and TX
GPIO16. GPIO33 is an input used only as advisory BUSY; it no longer emits the
legacy 380 Hz test PWM.

## 6. Build the six-language S3 image

```sh
pio run -e esp32s3_speech
```

The build embeds `en-US`, `en-GB`, `it-IT`, `de-DE`, `es-ES`, and `fr-FR`
resources. Verify that the final size is below one `0x760000` (7,733,248-byte)
A/B application slot. Compiler warnings inside the pinned PicoTTS C source must
be recorded; they are not evidence of a hardware failure, nor should they be
silently described as a warning-free build.

## 7. Exercise the S3 from the command line

The `tools/guardian_cli.py` Python 3 client encodes and validates the same
CRC-framed protocol used by HALMET. Install its only external dependency:

```sh
python3 -m pip install pyserial
```

Find the S3 serial device (`/dev/cu.usbmodem…` on macOS, `/dev/ttyACM…` on
Linux, or a `COM` port on Windows), then run commands such as:

```sh
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX ping
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX volume 60
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX rgb 0 0 64
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX speak \
  --language en-GB --priority normal "Guardian speech test"
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX test-all
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX stop
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-status
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-scan 5
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX \
  bt-connect aa:bb:cc:dd:ee:ff --address-type 1
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-disconnect
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-forget
```

The S3 never starts Wi-Fi. BLE starts automatically as a central named
`Guardian-S3`; `bt-status` reports both facts. Use the address type printed by
`bt-scan` when connecting, with the media remote awake and in pairing mode.

The HALMET SensESP **Configuration** page stores the Bluetooth scan duration,
selected remote and HID mappings. One-click Bluetooth actions are buttons on
the **Control** page: refresh, scan, pair, disconnect, test and forget. Their
in-progress and final result is shown under **Outlook Guardian** on the
**Status** page. Configuration contains persistent setup only, so actions and
status are not duplicated there.
After scanning, reload Configuration and select the named remote before using
the pair button. Press a remote button and reload Configuration to read its HID
hex code. Each mapping is a separate editable list item and has this form:

```text
0100=electrical.switches.anchorLight.state|true|Anchor light on
```

The left side is the HID report shown by the page. The middle fields are a
Signal K path and any valid JSON value. The optional final field is spoken by
the S3 after the PUT request is sent. Use **Bluetooth: Test last button** on
Control to replay the most recently received HID report through the same
mapping without pressing the remote again. The Status page also shows a live
**HALMET ↔ ESP32-S3** indicator maintained by periodic protocol PING/PONG
traffic; it changes to disconnected if no valid S3 frame arrives for five
seconds.

The ESP32-S3 supports Bluetooth Low Energy but not Bluetooth Classic. Reports
for the QXS-001 show the device being discovered through ESP32 Classic inquiry
while BLE scanner applications do not see it. If an S3 scan remains empty with
the QXS-001 awake and its blue pairing light flashing, that unit is advertising
its Classic HID mode and cannot be paired by the S3 firmware. Supporting that
mode requires a Classic-capable host such as HALMET's original ESP32, with its
Bluetooth controller enabled alongside Wi-Fi.
HALMET's own Bluetooth controller is stopped at boot; its Wi-Fi remains active
for SensESP and Signal K.

### USB-only developer connection

The normal HALMET Guardian image supports both the production GPIO UART and the
USB developer route. To test both boards without GPIO16/17 wiring, select
**USB** or **Auto** under **Speech and alarms → S3 protocol transport**, restart
HALMET, and run the host router:

```sh
pio run -e halmet_guardian -t upload --upload-port /dev/cu.usbserial-1140
python3 tools/guardian_usb_router.py \
  --halmet /dev/cu.usbserial-1140 \
  --s3 /dev/cu.usbmodem5B8F0986921 \
  --reset-halmet --show-logs
```

The computer is part of this developer topology and must keep the router
running. Stop it before flashing either board. **Auto** is the default and
transmits on both links; **GPIO** and **USB** restrict traffic to the selected
link. Protocol idempotence makes duplicate delivery harmless if both routes
are connected.

Global options such as `--timeout`, `--baud`, and `--sequence` must appear
before the subcommand. `speak` validates its per-request volume, pitch and speed
locally. `volume` sets the S3 master level; the effective output is the product
of master and per-request volume. Commands exit nonzero on timeout, protocol
ERROR or an incomplete lifecycle.

## 8. Make a repeatability record

Save, at minimum, the operating system, tool versions, Git revisions, all three
build results and the reported image sizes. A compact record can be produced by:

```sh
git status --short
git rev-parse HEAD
git submodule status
pio --version
ctest --test-dir build-host --output-on-failure
```

For a clean experiment, `git status --short` should be empty after tests. The
`.pio/` and `build-host/` directories are generated artifacts and should not be
treated as source inputs.

## 9. Proceed to hardware only after review

Read the complete [hardware guide](hardware/index.md), populate its assembly
record, and perform the no-power continuity checks before connecting USB or a
5 V supply. Do not connect HALMET directly to S3 EN or GPIO0. Do not power the
S3/amplifier from HALMET J201; J201 exposes no supported 5 V output.

At the end of this tutorial the defensible result is “host tests passed and
target images built.” A hardware/audio pass additionally requires an attached
HALMET, S3, MAX98357A, speaker, UART wiring, current-limited supply, oscilloscope
or logic analyzer, and electrical measurement equipment.
