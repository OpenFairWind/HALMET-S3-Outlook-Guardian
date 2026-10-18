# Provisional wiring (legacy location)

The maintained, expanded reference schematic is now
[`docs/hardware/schematic.md`](../docs/hardware/schematic.md). This file remains
as a compact compatibility reference for earlier links.

| Net | HALMET candidate | ESP32-S3 candidate | Status |
|---|---|---|---|
| TX to S3 RX | J201 pin 7 / GPIO16 | J3 pin 3 / GPIO44/U0RXD | verified from published schematics |
| RX from S3 TX | J201 pin 5 / GPIO17 | J3 pin 2 / GPIO43/U0TXD | boot logs/console must be tolerated |
| BUSY | J201 pin 9 / GPIO33 input | J1 pin 7 / GPIO7 output | test PWM disabled; BUSY is advisory |
| LRC / BCLK / DIN | — | GPIO4 / 5 / 6 | upstream mapping, PCB unverified |
| supply / ground | no supported 5 V GPIO-header output | J1 pin 21 (5V), J1 pin 22 (GND) | use a separate fused 5 V supply |

The former GPIO33 test PWM is disabled in `src/main.cpp`; distributed Guardian
configures it as an input. The HALMET design labels its USB 5 V access as a
single test point, explicitly downgraded from a header for safety. It is not an
approved coprocessor supply output.
GPIO43/44 are S3 UART0/ROM-download pins, so boot output, console ownership and
exposure on the exact devboard must be tested.

ROM recovery needs two separate open-drain transistor pull-downs on S3 EN and
GPIO0 with pull-ups and safe defaults. No available HALMET GPIOs have been
selected in production firmware. J201 pin 8/GPIO32 and pin 10/GPIO34 are verified
available candidates. Each must drive only a transistor/MOSFET open drain;
never connect either HALMET output directly to EN or GPIO0.
