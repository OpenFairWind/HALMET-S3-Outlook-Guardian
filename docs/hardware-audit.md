# Hardware audit

Sources: [HALMET documentation](https://docs.hatlabs.fi/halmet/hardware/),
[HALMET hardware commit e8bffda](https://github.com/hatlabs/HALMET-hardware),
[ESP32-S3-DevKitC-1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html), and the
[MAX98357A data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf).

HALMET J201 is a 2x10 odd/even header: GPIO17 is pin 5, GPIO16 pin 7,
GPIO33 pin 9, GPIO32 pin 8 and GPIO34 pin 10. These are documented available.

There is no 5 V output on J201. The schematic says its USB 5 V two-pin header
was downgraded to a single test point for safety. Use a separate fused 5 V
regulator for the S3 and amplifier.

ESP32-S3-DevKitC-1 exposes GPIO2 on J3 pin 5 and GPIO1 on J3 pin 4 for the
Guardian UART1 link. GPIO4/5/6/7 are on J1 pins 4/5/6/7, EN is on J1 pin 3,
GPIO0 is on J3 pin 14, and 5 V/GND are on J1 pins 21/22. Espressif calls USB,
5 V-header and 3V3-header supply mutually exclusive. Do not attach USB while
externally feeding 5 V.

MAX98357A accepts 2.5–5.5 V. At 5 V it specifies up to 3.2 W into 4 ohms and a
2.8 A current limit. Those are design bounds, not measured module load.

Hat Labs warns that unisolated connections to other ground-referenced equipment
can break NMEA2000 compliance. The combined grounding topology therefore still
requires installation-level verification.
