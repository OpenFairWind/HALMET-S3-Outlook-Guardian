# Prototype assembly procedure

## 1. Identify and record the hardware

Photograph both sides of A1–A3, record all silkscreen revisions, and attach the
exact schematics/data sheets to the experiment record. Confirm that A2 has
16 MB flash. Compare connector orientation—not only pin labels—with the
[reference schematic](schematic.md).

## 2. Configure the current-limited supply

Leave all boards disconnected. Set PS1 to 5.0 V and an initially conservative
current limit appropriate to the development board alone. Install F1 near the
supply. Do not infer a final limit until peak current is measured with the
amplifier and real load.

## 3. Perform no-power checks

1. Confirm no short exists between `5V_AUDIO` and `LOGIC_GND`.
2. Identify HALMET processor-side ground by schematic and continuity.
3. Verify each UART/BUSY conductor end-to-end and verify no adjacent-pin short.
4. Verify the speaker is isolated from ground and connected only across A3
   `OUT+` and `OUT-`.
5. If recovery circuitry is populated, verify Q1/Q2 orientation, gate pulldowns
   and high default voltage at EN/GPIO0 without HALMET drive.

## 4. Bring up the S3 without the amplifier

Power A2 alone from PS1. Record idle and boot peak current, 5 V rail minimum,
3.3 V rail, and device temperature. Flash `esp32s3_speech` only through the
method approved for the exact board. Disconnect USB before restoring external
5 V unless simultaneous powering has been separately verified.

## 5. Add UART and BUSY

With all power removed, connect crossed UART, BUSY and processor-side ground.
Power the system and observe both UART directions at 115200 8N1. The production
wiring is HALMET GPIO16 (TX) to S3 GPIO2 (RX), HALMET GPIO17 (RX) to S3 GPIO1
(TX), and processor-side ground to S3 ground. The S3 link uses UART1 so USB
Serial/JTAG activity cannot steal protocol traffic. Confirm GPIO33 remains an
input and that a missing/resetting S3
does not stop HALMET OLED, LED, ACK, snooze or alarm processing.

## 6. Add I2S and amplifier

Remove power, connect LRC/BCLK/DIN, amplifier VIN/GND and the speaker. Restore
power with a conservative current limit and minimum configured volume. Observe
I2S clocks and audio output, then increase volume in controlled steps while
recording peak current, 5 V droop and temperature.

## 7. Optional recovery circuit

Populate and test open-drain recovery only after normal speech operation is
stable. First exercise each MOSFET manually while monitoring EN/GPIO0 and rail
voltage. Firmware-controlled ROM entry remains unavailable until a dedicated
implementation and power-loss/reset matrix have been reviewed.

## 8. Freeze the experiment

Apply strain relief, label every conductor, photograph the final wiring, export
firmware hashes and complete the [reproducibility record](reproducibility.md).
Any wiring, speaker, supply, firmware, enclosure or gain-setting change creates
a new hardware configuration and invalidates affected measurements.
