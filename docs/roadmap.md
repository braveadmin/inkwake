# Roadmap

Hobby pace: effort is in weekends of about 4 hours. No dates, no deadlines; a milestone closes when its exit gate passes. Rough horizon: daily-driver clock after M3 (10-14 weekends), v1.0 after M7 (22-32 weekends).

Only the shell track runs in parallel.

## M0 Hardware spike

Effort: 2-3 weekends (+ parts shipping).

Breadboard all parts, measure whole-clock sleep current, panel partial refresh, touch-pad sensitivity through a printed test piece.

**Exit gate:** Sleep under 100 uA; ghosting acceptable with per-minute partial refresh.

## M1 Repo and CI

Effort: 1 weekend.

Push the scaffold, get CI green, run scripts/bootstrap-github.sh. Mostly done.

**Exit gate:** CI green on GitHub; milestones and issues exist.

## M2 Clock core

Effort: 3-4 weekends.

RTC driver, F12 NTP sync + timezone, Wi-Fi provisioning, F11 room temperature, clock face with partial refresh.

**Exit gate:** 24 h unattended run; correct time after power cycle with Wi-Fi off.

## M3 Alarms MVP

Effort: 4-6 weekends.

F1, F2. Scheduler, snooze state machine, buzzer fallback, alarms saved across reboots.

**Exit gate:** A week of real use on the nightstand with zero missed alarms.

## M4 Sound and sunrise

Effort: 3-4 weekends.

F3, F4. I2S amp, custom sounds, volume ramp, SK6812 sunrise.

**Exit gate:** No brownout on battery with LEDs and amp at maximum together.

## M5 Web UI and OTA

Effort: 4-6 weekends.

F5, F6. Web UI, OTA updates.

**Exit gate:** Configure and update without a USB cable.

## M6 Widgets

Effort: 3-5 weekends.

F8, F9. Widget engine, layout config, weather as the first widget, simulator for UI review.

**Exit gate:** Add a widget from the web UI with no reflash.

## M7 v1.0 release

Effort: 2-3 weekends.

BOM, build guide, browser flasher, tagged v1.0.

**Exit gate:** A friend builds one from the docs alone.

## M8 Integrations (after v1.0)

Effort: as motivation allows.

F7 Home Assistant / MQTT, F10 calendar widget.

**Exit gate:** One minor release per feature.

## Shell track

- [ ] **S1: shell fit test** After M0. Measure the real panel and board, set enclosure/inkwake.scad parameters, print the front shell, check panel window alignment and board mounting.
- [ ] **S2: shell features** With M4. Speaker grille and mount, LED ring diffuser, walled-off vented pocket for the temperature sensor, battery bay, USB-C charging port, copper pads under the top and side walls, back plate with heat-set inserts.
- [ ] **S3: final shell and print guide** With M7. Cosmetics, tilt and stability check, PETG print profile, assembly photos and print guide in docs/.
