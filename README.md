# inkwake

Open source alarm clock: ESP32-S3, touch e-ink screen, 3D-printed case.
Alarms work with Wi-Fi down; everything else (web UI, Home Assistant, widgets) sits on top.

> Status: pre-alpha, milestone M0 (hardware spike). Hobby project, no dates: see docs/roadmap.md.

## Features (v1.0)

NTP sync, room temperature, multiple alarms, touch snooze/dismiss, custom sounds, sunrise light,
web UI, OTA updates, widget modules, weather widget. After v1.0: Home Assistant via MQTT, calendar widget.
Details and priorities: [docs/features.md](docs/features.md). Plan: [docs/roadmap.md](docs/roadmap.md).
Decisions: [docs/adr/](docs/adr/).

## Hardware

| Part | Notes |
| --- | --- |
| ESP32-S3 touch e-ink board | M5Stack PaperS3 candidate, confirmed at M0 ([ADR-0002](docs/adr/0002-board.md)) |
| MAX98357A I2S amp + speaker | custom sounds |
| SK6812 RGBW ring | sunrise light |
| SHT40 breakout | room temperature, I2C |
| 5 V / 2 A USB-C supply | LEDs and amp peak together at alarm time |

Full list: [hardware/BOM.csv](hardware/BOM.csv).

## Build

```bash
pip install -r firmware/requirements.txt
cd firmware
pio test -e native        # host unit tests
pio run -e paper_s3       # device firmware
pio run -e paper_s3 -t upload
```

Enclosure (OpenSCAD 2021+):

```bash
make -C enclosure         # writes enclosure/build/*.stl
```

## Layout

```
firmware/    PlatformIO project; lib/ = hardware-free logic, unit tested on the host
sim/         host renderer: screen to PNG for UI review in PRs (M6)
web/         web UI sources, packed into LittleFS (M5)
enclosure/   parametric OpenSCAD case
hardware/    BOM, wiring
docs/        features, roadmap, ADRs
scripts/     repo bootstrap and tooling
```

## Licences

Firmware and software: GPL-3.0-only ([LICENSE](LICENSE)).
Enclosure: CERN-OHL-S-2.0 ([enclosure/LICENSE](enclosure/LICENSE)).
Documentation: CC BY-SA 4.0 ([docs/LICENSE](docs/LICENSE)).
