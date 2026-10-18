# Isolation constraints

This safety constraint is incorporated into the maintained
[hardware reproduction guide](../docs/hardware/index.md) and its
[verification matrix](../docs/hardware/verification.md).

HALMET's processor-side UART and power ground may be shared with the speech
coprocessor only if the exact schematic confirms those header pins are outside
the isolated NMEA2000 sensor domain. Do not bridge processor and sensor-side
grounds or rails. USB shields/grounds, an externally powered amplifier, speaker
wiring and test equipment can create an unintended second path and must be
included in the continuity/isolation audit.

Passing a 3.3 V UART test does not establish NMEA2000 compliance or isolation.
Record isolation resistance/continuity results and the complete powered topology
for each USB and vessel-power configuration before installation.

Hat Labs states that NMEA2000 compliance is retained only while unisolated
connectors are not tied to other ground-referenced equipment. The speech board
is exactly such an attachment. Treat the combined assembly as noncompliant until
the complete grounding arrangement is reviewed and tested; software cannot
certify galvanic isolation.
