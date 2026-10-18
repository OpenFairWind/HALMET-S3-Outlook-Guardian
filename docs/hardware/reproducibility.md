# Reproducibility record template

Create one record per physical configuration. Store photographs, oscilloscope
captures and raw measurement data beside the completed copy.

## Identity

| Field | Value |
|---|---|
| Experiment identifier / date / operator | |
| Main repository commit | |
| PicoTTS submodule commit | |
| HALMET model, PCB revision, serial | |
| ESP32-S3 board/module revision, flash size | |
| MAX98357A module vendor, revision, gain straps | |
| Speaker model, impedance, rating, enclosure | |
| Supply, fuse and wiring specification | |
| Recovery circuit populated? | No / Yes, revision: |

## Software toolchain

| Field | Value |
|---|---|
| Host OS/architecture | |
| PlatformIO version | |
| Platform/framework packages | |
| CMake/compiler versions | |
| HALMET environment and firmware SHA-256 | |
| S3 firmware SHA-256 and image size | |
| Configuration changes from repository defaults | |

## Instrumentation

| Instrument | Model/serial | Calibration date | Connection/probe | Uncertainty |
|---|---|---|---|---|
| DMM | | | | |
| Oscilloscope/logic analyzer | | | | |
| Current measurement | | | | |
| Thermal measurement | | | | |

## Results and deviations

Attach a completed copy of the tables in [verification](verification.md).
Describe every deviation from the reference BOM, schematic, build commands and
test method. Include failures and anomalous observations; reproducibility
depends on negative evidence as well as successful runs.
