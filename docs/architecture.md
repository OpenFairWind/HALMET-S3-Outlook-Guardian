# Distributed Guardian architecture

HALMET remains the marine application authority: Signal K and NMEA2000 source
arbitration, alarm table, severity, deduplication, acknowledgement, snooze,
repeat policy, configuration, display/LED and final language-specific text stay
there. `SpeechBackend` is the boundary; `UartSpeechBackend` only transports an
already composed request and tracks remote state. Failure or absence of S3 must
never stop HALMET alarm processing or its local indicators.

S3 owns frame validation, a bounded priority queue, PicoTTS, I2S/MAX98357A and
status/fault reports. It has no Signal K or NMEA2000 stack and does not reproduce
the Guardian alarm state machine. The portable `SpeechServer` implements command
semantics; `s3_main.cpp` connects it to UART0, PicoTTS and MAX98357A I2S.

Current implementation status:

- complete and host-tested: protocol, CRC/parser, SPEAK validation, queue,
  duplicate history, STOP, priority and portable HALMET/S3 interfaces;
- complete and compile-tested: S3 UART adapter, six-language PicoTTS,
  MAX98357A output and BUSY signaling;
- complete as a compile-tested HALMET environment: the upstream Guardian alarm
  application, existing configuration paths and UART backend integration;
- pending on hardware: runtime Signal K/N2K/OLED/LED/ACK compatibility and the
  physical HALMET pin/connector choices;
- A/B feasibility is measured. Updates remain fail-closed until a production
  signing key exists; ROM recovery requires the documented open-drain circuit.
