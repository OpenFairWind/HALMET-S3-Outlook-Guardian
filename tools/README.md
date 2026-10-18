# Guardian command-line tools

`guardian_cli.py` is a Python 3 client for testing and interacting with the
ESP32-S3 speech server over its 115200-baud USB serial connection. It creates
and validates Guardian v1 frames, including CRC-16/CCITT-FALSE, and exits
nonzero if the expected command lifecycle does not complete.

Install the dependency and show the available commands:

```sh
python3 -m pip install pyserial
python3 tools/guardian_cli.py --help
```

Examples:

```sh
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX ping
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX volume 60
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX rgb 0 32 0
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX \
  speak --language it-IT "Prova audio"
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX test-all
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX stop
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-status
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-scan 5
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX \
  bt-connect aa:bb:cc:dd:ee:ff --address-type 1
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-disconnect
python3 tools/guardian_cli.py --port /dev/cu.usbmodemXXXX bt-forget
```

Replace the device path with `/dev/ttyACM…` on Linux or the appropriate `COM`
port on Windows. Run only one serial client at a time. Global options such as
`--timeout`, `--baud`, and `--sequence` go before the subcommand.

## USB developer router

Build and flash `halmet_guardian` to test HALMET and S3 without GPIO UART
wiring. Select **Auto** or **USB** as the S3 protocol transport in the SensESP
Speech configuration. With both boards connected, route CRC-valid
Guardian frames between their USB serial ports:

```sh
pio run -e halmet_guardian -t upload --upload-port /dev/cu.usbserial-1140
python3 tools/guardian_usb_router.py \
  --halmet /dev/cu.usbserial-1140 \
  --s3 /dev/cu.usbmodem5B8F0986921 \
  --reset-halmet --show-logs
```

HALMET logs share its USB stream but are displayed locally and never forwarded
to the S3. Use `--duration 30 --require-speech` for an automated boot/welcome
speech check. Stop the router before flashing or monitoring either board.
