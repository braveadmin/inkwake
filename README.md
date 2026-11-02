# inkwake

Open source alarm clock: Waveshare ESP32-S3-RLCD-4.2, touch pads in a 3D-printed case.
Runs on a battery for weeks, plugged in only to charge. Alarms work with Wi-Fi down.

> Status: pre-alpha, milestone M0 (hardware spike). Hobby project, no dates: see docs/roadmap.md.

## Features (v1.0)

NTP sync, room temperature, multiple alarms, touch snooze/dismiss, custom sounds, sunrise light,
web UI, OTA updates, widget modules, weather widget. After v1.0: Home Assistant via MQTT, calendar widget.
Details and priorities: [docs/features.md](docs/features.md). Plan: [docs/roadmap.md](docs/roadmap.md).
Decisions: [docs/adr/](docs/adr/).

## Hardware

| Part | Notes |
| --- | --- |
| Waveshare ESP32-S3-RLCD-4.2 | 4.2" reflective LCD, RTC, temperature, audio, speaker, 18650 on one board ([ADR-0002](docs/adr/0002-board.md)) |
| Warm-white LEDs + MOSFET | sunrise light ([ADR-0007](docs/adr/0007-light.md)) |
| Copper tape | touch pads ([ADR-0015](docs/adr/0015-input.md)) |

Full list: [hardware/BOM.csv](hardware/BOM.csv).

## Build

```bash
pip install -r firmware/requirements.txt
cd firmware
pio test -e native        # host unit tests
pio run -e device       # device firmware
pio run -e device -t upload
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
