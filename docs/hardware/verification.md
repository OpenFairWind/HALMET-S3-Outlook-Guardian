# Hardware verification and acceptance record

No row below is currently claimed as passed. Copy this table into a dated test
record and attach instrument captures. “Observed” must contain a value, not only
“OK”. Instrument model, calibration status, probe topology and uncertainty are
part of the evidence.

## Electrical acceptance matrix

| ID | Measurement | Method | Provisional acceptance criterion | Observed | Result |
|---|---|---|---|---|---|
| E01 | Unpowered 5 V-to-GND resistance | DMM after capacitors settle | No low-resistance short | — | Not tested |
| E02 | UART/BUSY logic high | Oscilloscope at each receiver | Within ESP32 3.3 V input limits | — | Not tested |
| E03 | UART integrity | Logic analyzer, sustained framed traffic | No framing/CRC errors attributable to wiring | — | Not tested |
| E04 | 5 V rail droop | Scope at A2 and A3 during worst audio transient | Within both module input specifications | — | Not tested |
| E05 | Aggregate peak current | Inline shunt/current probe | Below PS1, F1, connector and conductor ratings with design margin | — | Not tested |
| E06 | USB backfeed | Measure current/voltage in every allowed power state | No unintended source-to-source current | — | Not tested |
| E07 | Temperature rise | Thermocouple/thermal camera after steady worst case | Below component and enclosure limits with margin | — | Not tested |
| E08 | Isolation topology | Continuity/insulation test with all normal cables | Meets reviewed system requirement | — | Not tested |

## Functional acceptance matrix

| ID | Experiment | Expected observation | Result |
|---|---|---|---|
| F01 | Boot S3 with HALMET UART attached | Boot text causes no speech; parser later accepts a valid frame | Not tested |
| F02 | Remove/reset S3 during active alarms | HALMET alarm processing, OLED, LED, ACK and snooze continue | Not tested |
| F03 | Speak Italian and a second language | Intelligible audio; correct request lifecycle | Not tested |
| F04 | Issue STOP during speech | Audio terminates, queue clears, BUSY returns low | Not tested |
| F05 | Submit critical speech during lower priority speech | Critical request preempts according to protocol | Not tested |
| F06 | Corrupt and fragment UART frames | Invalid frames are silent; next valid frame resynchronizes | Not tested |
| F07 | Observe final PCM drain | DONE occurs only after audible/I2S completion | Not tested |
| F08 | Enter ROM downloader via recovery circuit | Correct EN/GPIO0 waveform and reliable enumeration | Not tested; circuit absent |

## Power/thermal run sequence

Measure A2 alone, A2 synthesizing without A3, A2+A3 idle, speech into the actual
speaker at each intended volume, and the simultaneous worst-case HALMET/S3
state. For every state record minimum/maximum/mean voltage and current, ambient
temperature, component hotspot temperatures and duration. The MAX98357A's
published 2.8 A current limit and 3.2 W/4-ohm output bound are not substitute
measurements of a module or assembly.

## Interpretation

A firmware build or successful UART exchange cannot close E04–E08. A single
audio demonstration cannot establish peak current, backfeed safety, thermal
margin, isolation, multilingual behavior or recovery reliability. Only claim a
pass for the exact configuration identified by the accompanying reproducibility
record.
