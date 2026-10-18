# Bill of materials

The BOM distinguishes a minimum speech prototype from optional recovery and
measurement items. “Reference” identifies items in the schematic; it is not a
procurement endorsement. Record manufacturer part numbers and revisions for the
actual units used because generic marketplace modules often differ electrically.

## Core assembly

| Ref. | Qty. | Item | Minimum specification | Selection/verification note |
|---|---:|---|---|---|
| A1 | 1 | HALMET | Exact PCB revision recorded; J201 fitted | Existing controller; verify connector orientation from schematic. |
| A2 | 1 | ESP32-S3 development board | ESP32-S3-DevKitC-1-N16R8V, 16 MB flash | The six-language A/B layout assumes 16 MB flash. Record module and board revision. |
| A3 | 1 | I2S amplifier module | MAX98357A, 2.5–5.5 V supply | Record module schematic/photographs; clone pinouts and gain straps vary. |
| LS1 | 1 | Loudspeaker | 4–8 ohm, power rating appropriate to selected supply/gain | Start at limited volume; enclosure changes acoustic and thermal load. |
| PS1 | 1 | Regulated bench supply or DC/DC branch | 5.0 V, adjustable current limit, adequate transient rating | Must be separately fused; capacity is established by measurement, not the nominal label. |
| F1 | 1 | Fuse or resettable fuse | Sized after measured peak and conductor rating | Place close to the supply source. |
| CBL1 | 1 | UART harness | Three 3.3 V logic conductors plus processor-side ground | Cross TX/RX; keep short during initial tests. |
| CBL2 | 1 | I2S/power harness | Conductors suitable for measured current | Route speaker current away from logic return where practical. |
| TP1–TP6 | 6 | Test points/header positions | UART TX/RX, BUSY, 5 V, 3V3, GND | Strongly recommended for repeatable measurements. |

## Optional ROM-recovery circuit

| Ref. | Qty. | Item | Recommended laboratory value | Function |
|---|---:|---|---|---|
| Q1, Q2 | 2 | N-channel MOSFET | 2N7002 or equivalent logic-level small-signal device | Open-drain pull-down for S3 EN and GPIO0. |
| R1, R2 | 2 | Gate pulldown | 100 kohm, 1% | Keeps Q1/Q2 off while HALMET boots or is disconnected. |
| R3, R4 | 2 | Gate series resistor | 1 kohm, 1% | Limits transient gate current. |
| R5, R6 | 2 | Pull-up | 10 kohm, 1%, to S3 3V3 | Safe default high; omit only if the exact board already provides a verified effective pull-up. |

GPIO32/J201-8 is the documented candidate control for EN and GPIO34/J201-10
for GPIO0. These assignments are **provisional** and firmware support is not
implemented. Fit neither circuit until pin availability, polarity, reset timing
and the exact board pull-ups are verified.

## Required laboratory equipment

| Item | Purpose |
|---|---|
| Current-limited supply and inline current measurement | Safe power-up, idle/peak current, rail droop. |
| Digital multimeter | Continuity, logic rails, shorts and isolation resistance screening. |
| Two-channel oscilloscope or logic analyzer | UART levels/timing, BUSY, I2S and reset sequencing. |
| USB isolator or isolated UART adapter, if used | Limits unintended ground paths; rating and topology must be documented. |
| Thermocouple or thermal camera | Amplifier, regulator and wiring temperature rise. |
| 4–8 ohm noninductive load of adequate power, optional | Repeatable amplifier test before using a speaker. |

Consumables include insulated wire, strain relief, heat-shrink, an enclosure or
nonconductive test fixture, and labels. Do not treat solderless breadboards as
rated for unmeasured amplifier peak current.

## Where to buy

The following links identify the intended parts rather than guaranteeing price,
stock, shipping or regional certification. Availability changes; verify the
ordering code and published documentation at purchase time.

| Ref. | Purchase link | Procurement note |
|---|---|---|
| A1 | [HALMET from Hat Labs](https://shop.hatlabs.fi/products/halmet) | Manufacturer storefront; the product is a development-kit-style board and is not supplied with this project firmware. The [HALMET selection guide](https://shop.hatlabs.fi/pages/halmet-selection-guide) covers compatible enclosures and connectors. |
| A2 | [ESP32-S3-DevKitC-1-N16R8V from Mouser](https://www.mouser.com/en/ProductDetail/Espressif-Systems/ESP32-S3-DevKitC-1-N16R8V?qs=Rp5uXu7WBW9tO4z0R%252Btd7Q%3D%3D) | Buy the exact `N16R8V` 16 MB-flash ordering code. Confirm it against [Espressif's ordering table](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html); similar 8 MB variants cannot hold this A/B layout. |
| A3 | [Adafruit MAX98357A mono I2S amplifier, product 3006](https://www.adafruit.com/product/3006) | This documented breakout is preferred over an unidentified clone because its schematic and gain configuration are published. |
| LS1 | [Adafruit 40 mm, 4-ohm, 5 W speaker, product 3968](https://www.adafruit.com/product/3968) | A reproducible example load; operate within the amplifier and measured assembly limits. |

PS1, F1, MOSFETs, resistors, wire and test equipment should be purchased from
an authorized distributor appropriate to the installation country. Their exact
manufacturer part numbers belong in the
[reproducibility record](reproducibility.md); substituting a supply, amplifier
module or speaker creates a materially different electrical experiment.
