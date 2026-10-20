# Installation and setup guide

This guide installs the distributed Outlook Guardian firmware on a HALMET and
an ESP32-S3, connects the two boards, configures Signal K, and validates speech
and BLE remote input. It describes the bench-tested GPIO UART topology. Read the
[hardware safety guide](hardware/index.md) before permanent or vessel use.

## 1. Requirements

- HALMET with its original ESP32 and a USB data cable.
- ESP32-S3 DevKitC-compatible board with 16 MB flash and 8 MB PSRAM.
- MAX98357A I2S amplifier, suitable speaker, protected 5 V supply, and a common
  processor-side ground.
- Three UART conductors and the I2S/audio wiring in the
  [reference schematic](hardware/schematic.md).
- Git, Python 3, PlatformIO Core, CMake 3.16+, and a C++17 compiler.
- A Signal K server reachable by HALMET over Wi-Fi.

The S3 supports Bluetooth Low Energy only. A product labelled “Bluetooth 5.x”
is not necessarily BLE HID. The tested QXS-001 appears in macOS Bluetooth
Settings but not in a CoreBluetooth BLE scan and therefore operates as
Bluetooth Classic HID; it cannot pair with ESP32-S3. Use a remote verified by a
BLE scanner to advertise over BLE, or add a separate Classic-capable ESP32.

## 2. Obtain and verify the source

```sh
git clone --recursive https://github.com/OpenFairWind/HALMET-S3-Outlook-Guardian.git
cd HALMET-S3-Outlook-Guardian
git submodule status
```

Install PlatformIO if it is not already available:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio pyserial
```

Run the portable regression suite and build both production applications:

```sh
cmake -S . -B /tmp/guardian-host -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/guardian-host --parallel
ctest --test-dir /tmp/guardian-host --output-on-failure
pio run -e halmet_guardian
pio run -e esp32s3_speech
```

Do not flash a failed or oversized build. The S3 image must fit its configured
`0x760000` application slot.

## 3. Flash the ESP32-S3

Connect only the S3 USB cable, identify its serial port, and run:

```sh
pio run -e esp32s3_speech -t upload --upload-port /dev/cu.usbmodemXXXX
```

Linux typically uses `/dev/ttyACM*`; Windows uses `COM*`. The image contains
offline PicoTTS voices for English (US/GB), Italian, German, Spanish, and
French. It intentionally leaves Wi-Fi disabled.

Optional direct USB smoke tests:

```sh
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX ping
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-status
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX test-all
```

Expect a PONG, BLE enabled with Wi-Fi off, and one complete
ACK/STARTED/DONE lifecycle for `test-all`.

## 4. Flash HALMET

Connect only HALMET USB and identify its USB-UART port:

```sh
pio run -e halmet_guardian -t upload --upload-port /dev/cu.usbserial-XXXX
```

The Guardian target embeds the custom Control and Configuration interface. Its
upload speed is fixed at 115200 because higher rates were unreliable on the
tested HALMET USB-UART bridge. This is the distributed Guardian application;
the `halmet` target is only a fast compile check for the preserved example.

## 5. Wire HALMET to the S3

Remove USB and all external power before changing wiring.

| HALMET | Direction | ESP32-S3 |
|---|---:|---|
| GPIO16 (TX) | → | GPIO2 (RX, UART1) |
| GPIO17 (RX) | ← | GPIO1 (TX, UART1) |
| processor-side GND | ↔ | GND |

Do not connect HALMET directly to S3 EN or GPIO0. Do not assume HALMET J201 is a
supported 5 V source. Power the S3/amplifier as specified in the hardware guide.
The optional BUSY signal is advisory; HALMET continues operating without it.

For audio, connect S3 GPIO4 to MAX98357A LRC, GPIO5 to BCLK, GPIO6 to DIN, and
tie the amplifier SD/EN high as required by the exact module. Connect the
speaker only across amplifier OUT+ and OUT−, never from an output to ground.

## 6. First boot and Signal K

1. Power both boards and open HALMET's SensESP setup access point if no saved
   Wi-Fi configuration exists.
2. Enter vessel Wi-Fi and Signal K server settings, then restart HALMET.
3. Open `http://<halmet-address>/status` and verify **HALMET ↔ ESP32-S3** becomes
   connected. It should become disconnected within about five seconds if the
   S3 or UART is removed.
4. Verify HALMET remains connected to Signal K and that local OLED, alarm LED,
   acknowledge, and snooze functions remain responsive.

The default S3 transport is **Auto** for migration compatibility. For the
three-wire production connection choose **GPIO** under **Speech and alarms → S3
protocol transport**, save, and restart. **USB** is only for the developer
router described in [Getting started](getting_started.md).

## 7. Pair a BLE HID remote

1. Confirm the remote appears in a phone/computer BLE scanner—not merely the
   operating system's combined Classic/BLE Bluetooth page.
2. Forget the remote from other hosts and disable their Bluetooth temporarily.
3. Put the remote into discoverable pairing mode and keep it close to the S3.
4. Open HALMET **Control → Nearby Devices**, select **Scan**, then select
   **Connect** beside the named BLE device.
5. The device moves to **My Devices** and shows **Connected**. Use Disconnect
   and Connect without deleting its saved mappings.

Active-scan advertisements and scan responses are merged by address so a local
name delivered in the later scan response is retained. Unnamed BLE devices may
still appear; do not guess by address because privacy addresses rotate.

If the expected name never appears but a Mac/phone's normal Bluetooth settings
shows it, verify with a BLE-specific scanner. A Classic-only device cannot be
fixed by S3 firmware.

## 8. Test input and configure Signal K PUT mappings

Press a remote button while connected. Its hexadecimal report appears as
**Last input**. **Test input** replays that report through the mapping path
without another physical press.

Open HALMET **Configuration → Bluetooth remotes**. Each saved device has its
own mapping list. A mapping is:

```text
HID_CODE=signal.k.path|JSON_VALUE|optional words to speak
```

Example:

```text
0100=electrical.switches.anchorLight.state|true|Anchor light on
```

`JSON_VALUE` must be valid JSON (`true`, `12.5`, `"text"`, an object, or an
array). Save the mapping, press the button or choose **Test input**, and verify
the PUT at the Signal K server. The optional final text is spoken after the PUT
is sent.

## 9. Acceptance checklist

- Host tests pass and both target images build without errors.
- Both flashes finish with hash verification.
- Status reports the HALMET–S3 link connected over GPIO UART.
- S3 reports BLE enabled and Wi-Fi off.
- A speech command is audible and reaches DONE.
- A genuine BLE HID device is named, connects, emits input, and reconnects.
- With a reachable Signal K server, Test input reproduces the saved PUT.
- HALMET alarm processing remains responsive with the S3 disconnected/reset.

Electrical, thermal, peak-current, rail-droop, backfeed, isolation, and
long-duration vessel qualification still require the measurements listed in
[the test plan](test-plan.md). A successful software acceptance test is not a
marine safety certification.

## 10. Recovery and troubleshooting

- **No S3 link:** check crossed TX/RX, common ground, GPIO transport selection,
  and 115200 8N1. GPIO2 is S3 RX; GPIO1 is S3 TX.
- **No BLE names:** keep the device in pairing mode throughout the scan; forget
  it from other hosts. Confirm it with a BLE-specific scanner.
- **QXS-001 not found:** the tested unit is Classic HID and is incompatible with
  the ESP32-S3 BLE-only controller.
- **Connect times out:** do not repeatedly try rotating unnamed addresses.
  Rescan for a stable name/service and verify BLE HID support.
- **No audio:** start at low volume, verify MAX98357A SD/EN is high, confirm I2S
  GPIO4/5/6, and check that the speaker is across OUT+/OUT−.
- **HALMET upload fails:** close serial monitors/routers and retry at 115200.
- **S3 upload fails:** close anything holding the USB CDC port, then retry.

Record source revision, firmware hashes, board revisions, wiring photographs,
tool versions, and acceptance results using the
[reproducibility record](hardware/reproducibility.md).
