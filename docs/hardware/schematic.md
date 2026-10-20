# Reference schematic and net specification

This is a logical reference schematic for a bench prototype. Connector numbers
refer to the published boards; verify them against the exact revisions before
wiring. The design intentionally uses a separate 5 V branch and a common
processor-side logic ground.

## Interconnect schematic

```text
 HALMET J201                          ESP32-S3-DevKitC-1             MAX98357A
 ──────────                          ───────────────────             ─────────
 pin 7  GPIO16  TX  ───────────────> J3-5  GPIO2 / UART1 RX
 pin 5  GPIO17  RX  <─────────────── J3-4  GPIO1 / UART1 TX
 pin 9  GPIO33 BUSY <─────────────── J1-7  GPIO7
 processor GND      ──────────────── J1-22 GND ──────────────────── GND
                                          J1-4 GPIO4  ───────────── LRC/WS
                                          J1-5 GPIO5  ───────────── BCLK
                                          J1-6 GPIO6  ───────────── DIN

 fused 5 V branch   ───────────────────── J1-21 5V
        │
        └────────────────────────────────────────────────────────── VIN
 amplifier output                                                OUT+ / OUT-
                                                                    │     │
                                                                    └─LS1─┘
```

The MAX98357A output is bridge-tied. Neither speaker terminal is ground; do not
connect `OUT+` or `OUT-` to ground or to measurement equipment with a grounded
probe. Use a differential probe or a properly isolated measurement method.

## Net table

| Net | Source | Destination | Electrical rule | Firmware definition |
|---|---|---|---|---|
| `H_TX_S_RX` | HALMET J201-7, GPIO16 | S3 J3-5, GPIO2 | 3.3 V UART1, 115200 8N1 | `Serial2` TX / `GUARDIAN_S3_RX_PIN` |
| `S_TX_H_RX` | S3 J3-4, GPIO1 | HALMET J201-5, GPIO17 | 3.3 V UART1, 115200 8N1 | `GUARDIAN_S3_TX_PIN` / `Serial2` RX |
| `S_BUSY` | S3 J1-7, GPIO7 | HALMET J201-9, GPIO33 | 3.3 V, advisory only | `GUARDIAN_S3_BUSY_PIN` |
| `I2S_LRC` | S3 J1-4, GPIO4 | A3 LRC/WS | 3.3 V digital | `MAX98357A_LRC_PIN` |
| `I2S_BCLK` | S3 J1-5, GPIO5 | A3 BCLK | 3.3 V digital | `MAX98357A_BCLK_PIN` |
| `I2S_DATA` | S3 J1-6, GPIO6 | A3 DIN | 3.3 V digital | `MAX98357A_DIN_PIN` |
| `5V_AUDIO` | PS1 through F1 | S3 J1-21 and A3 VIN | Verify 4.75–5.25 V under load | none |
| `LOGIC_GND` | processor-side ground | S3/A3 ground | Review isolation topology first | none |

## Optional open-drain recovery channels

Use two independent copies of this circuit, one for EN and one for GPIO0:

```text
 HALMET candidate GPIO ── R3 1k ──┬── gate Q1 (2N7002)
                                  └── R1 100k ── GND
 S3 target EN or GPIO0 ─────────────── drain Q1
 S3 GND ───────────────────────────── source Q1
 S3 3V3 ── R5 10k ───────────────── target (verify onboard pull-up first)
```

Driving the HALMET GPIO high turns the MOSFET on and pulls the target low;
reset/default/tri-state leaves the target high. This inversion must be reflected
in any future firmware. The open-drain stage prevents one controller from
driving against the S3 board's pull-up or boot circuitry. The circuit must exist
and pass timing tests before ROM recovery can be claimed.

## Power and USB topology

HALMET J201 provides no supported 5 V output. PS1/F1 therefore supplies A2 and
A3. Before connecting a USB cable to A2, disconnect external 5 V unless the
exact board schematic and a backfeed test prove the chosen simultaneous-power
arrangement safe. Repeat this analysis for USB-connected instruments and UART
adapters because their grounds can bypass intended NMEA 2000 isolation.
