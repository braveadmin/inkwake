# Features

The scope. P0 and P1 make v1.0; P2 ships after it (M8). Each row is a GitHub issue under its milestone (see `scripts/bootstrap-github.sh`).

| # | Feature | Done means | Priority | Milestone |
| --- | --- | --- | --- | --- |
| F1 | Multiple alarms | Any number of alarms (cap 16): label, time, weekday mask, sound, sunrise on/off, enabled | P0 | M3 |
| F2 | Touch snooze/dismiss | Tap = snooze, long-press or swipe = dismiss; audio stops on touch-down, before redraw | P0 | M3 |
| F3 | Custom sounds | WAV/MP3 from microSD over I2S amp, per-alarm sound, volume ramp | P1 | M4 |
| F4 | Sunrise light | Warm-white LED ramp N minutes before the alarm, per alarm | P1 | M4 |
| F5 | Web UI over Wi-Fi | Manage alarms, sounds, layout, settings from a browser; config export/import | P1 | M5 |
| F6 | OTA updates | Upload firmware from the web UI; later pull signed builds from GitHub Releases | P1 | M5 |
| F7 | Home Assistant / MQTT | Alarms, next alarm, ringing state, room temperature via MQTT discovery; snooze/dismiss from HA | P2 | M8 |
| F8 | Widget modules | Add, remove, place widgets (clock, next alarm, temperature, weather, calendar, HA entity) from the web UI, no reflash | P1 | M6 |
| F9 | Weather widget | Current + today's forecast from Open-Meteo, refreshed every 30 min | P1 | M6 |
| F10 | Calendar widget | Next events from a HA calendar entity; direct ICS URL as follow-up | P2 | M8 |
| F11 | Room temperature | SHT40 over I2C shown on the clock face; vented pocket away from board and LED heat; offset calibration in config | P1 | M2 |
| F12 | NTP synchronization | Sync at boot and every 6 h against a configurable server, POSIX TZ with DST, written to the RTC; runs from the RTC when Wi-Fi is down | P0 | M2 |

Implied foundations (M2): RTC driver, Wi-Fi provisioning, offline alarm guarantee.
