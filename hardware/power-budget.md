# Conditional power budget worksheet

Do not connect power until the blanks below are filled from the exact board
revisions and confirmed with measurements.

| Item | Minimum | Typical | Peak | Evidence |
|---|---:|---:|---:|---|
| HALMET GPIO-header 5 V available current | 0 | 0 | 0 | no 5 V pin exists |
| S3 devboard at 5 V/VIN | TBD | TBD | TBD | exact board schematic |
| S3 Wi-Fi disabled, PicoTTS active | TBD | TBD | TBD | inline measurement |
| MAX98357A at selected speaker/load | TBD | TBD | TBD | datasheet + measurement |
| Other loads on the rail | TBD | TBD | TBD | installation audit |
| Wiring/regulator margin | 30% target | | | calculation |

The published HALMET design has a 2 A switching supply, but its USB 5 V node is
only exposed as a test point explicitly downgraded from a header for safety.
Therefore the S3 and amplifier require a separate, fused, regulated 5 V branch;
the 2 A board-regulator rating is not permission to load the test point.

The design is acceptable only if measured aggregate peak plus margin is below
the header, regulator, connector and wiring ratings. Never feed a 5 V/VIN input
from 3.3 V. Logic is 3.3 V only.

## No-power-first bring-up

1. Record HALMET PCB revision, S3 devboard revision and amplifier module model.
2. Identify processor-side header ground and power pins by schematic and
   continuity; do not use sensor-side isolated ground or 3V3.
3. With everything unpowered, check for shorts and verify UART/BUSY continuity.
4. Audit USB 5 V, devboard VIN diodes/regulator, audio ground and any second
   supply for backfeed. Choose one power topology before attaching USB.
5. Power each board independently with current limiting. Measure idle and load
   current, rail droop and amplifier peak current into the real speaker.
6. Join grounds and UART only after logic levels are confirmed. Test receive and
   framing before attaching the amplifier, then test low-volume audio.
7. Confirm NMEA2000 galvanic isolation remains intact in every normal and USB
   service configuration.
