# Features

The v1.0 scope. Each row is a GitHub issue under its milestone (see `scripts/bootstrap-github.sh`).

| # | Feature | Done means | Priority | Milestone |
| --- | --- | --- | --- | --- |
| F1 | Multiple alarms | Any number of alarms (cap 16): label, time, weekday mask, sound, sunrise on/off, enabled | P0 | M3 |
| F2 | Touch snooze/dismiss | Tap = snooze, long-press or swipe = dismiss; audio stops on touch-down, before redraw | P0 | M3 |
| F3 | Custom sounds | WAV/MP3 from microSD over I2S amp, per-alarm sound, volume ramp | P1 | M4 |
| F4 | Sunrise light | Warm-white LED ramp N minutes before the alarm, per alarm | P1 | M4 |
| F5 | Web UI over Wi-Fi | Manage alarms, sounds, layout, settings from a browser; config export/import | P1 | M5 |
| F6 | OTA updates | Upload firmware from the web UI; later pull signed builds from GitHub Releases | P1 | M5 |
| F7 | Home Assistant / MQTT | Alarms, next alarm, ringing state via MQTT discovery; snooze/dismiss from HA | P2 | M5 |
| F8 | Widget modules | Add, remove, place widgets (clock, next alarm, weather, calendar, HA entity) from the web UI, no reflash | P1 | M6 |
| F9 | Weather widget | Current + today's forecast from Open-Meteo, refreshed every 30 min | P2 | M6 |
| F10 | Calendar widget | Next events from a HA calendar entity; direct ICS URL as follow-up | P2 | M6 |

Implied foundations (M2): RTC + NTP + timezone, Wi-Fi provisioning, offline alarm guarantee.
