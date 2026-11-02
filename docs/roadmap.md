# Roadmap

Sequential milestones; each closes on its exit gate, not a date. The enclosure runs in parallel from M2 and is finalised in M7.

## M0 Hardware spike

Confirm the board, map spare GPIO, test touch, partial refresh, power draw.

**Exit gate:** 4+ spare GPIO for I2S and LED; ghosting acceptable with per-minute partial refresh.

## M1 Repo and CI

PlatformIO project, native unit tests, simulator, GitHub Actions, licences.

**Exit gate:** CI green on main; simulator renders a clock face to PNG.

## M2 Clock core

RTC + NTP, timezones, Wi-Fi provisioning, clock face with partial refresh.

**Exit gate:** 24 h unattended run; correct time after power cycle with Wi-Fi off.

## M3 Alarms MVP

F1, F2. Scheduler, snooze state machine, buzzer fallback, NVS persistence.

**Exit gate:** 7-day soak: zero missed alarms, including Wi-Fi off and power cycles.

## M4 Sound and sunrise

F3, F4. I2S amp, custom sounds, volume ramp, SK6812 sunrise.

**Exit gate:** No brownout with LEDs and amp at maximum together.

## M5 Connected

F5, F6, F7. Web UI, OTA, Home Assistant via MQTT discovery.

**Exit gate:** Configure and update without USB; HA sees and controls alarms.

## M6 Widgets

F8, F9, F10. Widget engine, layout config, weather, calendar.

**Exit gate:** Add a widget from the web UI with no reflash.

## M7 v1.0

Final enclosure, BOM, build guide, browser flasher, tagged release.

**Exit gate:** Someone outside the project builds one from the docs alone.
