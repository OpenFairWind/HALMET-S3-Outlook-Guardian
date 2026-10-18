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

## 7. Make a repeatability record

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

## 8. Proceed to hardware only after review

Read the complete [hardware guide](hardware/index.md), populate its assembly
record, and perform the no-power continuity checks before connecting USB or a
5 V supply. Do not connect HALMET directly to S3 EN or GPIO0. Do not power the
S3/amplifier from HALMET J201; J201 exposes no supported 5 V output.

At the end of this tutorial the defensible result is “host tests passed and
target images built.” A hardware/audio pass additionally requires an attached
HALMET, S3, MAX98357A, speaker, UART wiring, current-limited supply, oscilloscope
or logic analyzer, and electrical measurement equipment.
