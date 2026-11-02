#!/usr/bin/env bash
# Creates labels, milestones M0-M7 and one issue per feature on the GitHub repo.
# Usage: scripts/bootstrap-github.sh <owner/repo>   (needs gh, authenticated)
# Idempotent: skips labels/milestones/issues that already exist.
set -euo pipefail
REPO="${1:?usage: $0 owner/repo}"

label() { gh label create "$1" --repo "$REPO" --color "$2" --force >/dev/null; }
milestone() {
  local title="$1" desc="$2"
  if ! gh api "repos/$REPO/milestones?state=all&per_page=100" --jq ".[].title" | grep -Fxq "$title"; then
    gh api "repos/$REPO/milestones" -f title="$title" -f description="$desc" >/dev/null
    echo "milestone: $title"
  fi
}
issue() {
  local title="$1" body="$2" ms="$3"; shift 3
  if gh issue list --repo "$REPO" --state all --search "in:title \"$title\"" --json title --jq ".[].title" | grep -Fxq "$title"; then
    return
  fi
  local args=(); for l in "$@"; do args+=(--label "$l"); done
  gh issue create --repo "$REPO" --title "$title" --body "$body" --milestone "$ms" "${args[@]}" >/dev/null
  echo "issue: $title"
}

label "type:feature" a2eeef; label "type:bug" d73a4a; label "type:chore" cfd3d7; label "deps" 0366d6
label "P0" b60205; label "P1" d93f0b; label "P2" fbca04; label "area:shell" 5319e7

milestone "M0 Hardware spike" "Breadboard all parts, measure whole-clock sleep current, panel partial refresh, touch-pad sensitivity through a printed test piece. Effort: 2-3 weekends (+ parts shipping). Exit gate: Sleep under 100 uA; ghosting acceptable with per-minute partial refresh."
milestone "M1 Repo and CI" "Push the scaffold, get CI green, run scripts/bootstrap-github.sh. Mostly done. Effort: 1 weekend. Exit gate: CI green on GitHub; milestones and issues exist."
milestone "M2 Clock core" "RTC driver, F12 NTP sync + timezone, Wi-Fi provisioning, F11 room temperature, clock face with partial refresh. Effort: 3-4 weekends. Exit gate: 24 h unattended run; correct time after power cycle with Wi-Fi off."
milestone "M3 Alarms MVP" "F1, F2. Scheduler, snooze state machine, buzzer fallback, alarms saved across reboots. Effort: 4-6 weekends. Exit gate: A week of real use on the nightstand with zero missed alarms."
milestone "M4 Sound and sunrise" "F3, F4. I2S amp, custom sounds, volume ramp, SK6812 sunrise. Effort: 3-4 weekends. Exit gate: No brownout with LEDs and amp at maximum together."
milestone "M5 Web UI and OTA" "F5, F6. Web UI, OTA updates. Effort: 4-6 weekends. Exit gate: Configure and update without a USB cable."
milestone "M6 Widgets" "F8, F9. Widget engine, layout config, weather as the first widget, simulator for UI review. Effort: 3-5 weekends. Exit gate: Add a widget from the web UI with no reflash."
milestone "M7 v1.0 release" "BOM, build guide, browser flasher, tagged v1.0. Effort: 2-3 weekends. Exit gate: A friend builds one from the docs alone."
milestone "M8 Integrations (after v1.0)" "F7 Home Assistant / MQTT, F10 calendar widget. Effort: as motivation allows. Exit gate: One minor release per feature."

issue "F1: Multiple alarms" "Done means: Any number of alarms (cap 16): label, time, weekday mask, sound, sunrise on/off, enabled" "M3 Alarms MVP" "type:feature" "P0"
issue "F2: Touch snooze/dismiss" "Done means: Capacitive pads in the shell: tap top = snooze, hold 2 s = dismiss, side pad = night glow; audio stops on touch-down" "M3 Alarms MVP" "type:feature" "P0"
issue "F3: Custom sounds" "Done means: WAV/MP3 from flash over I2S amp, per-alarm sound, volume ramp" "M4 Sound and sunrise" "type:feature" "P1"
issue "F4: Sunrise light" "Done means: Warm-white LED ramp N minutes before the alarm, per alarm" "M4 Sound and sunrise" "type:feature" "P1"
issue "F5: Web UI over Wi-Fi" "Done means: Woken on demand (hold side pad 5 s) for a few minutes; manage alarms, sounds, layout, settings; config export/import" "M5 Web UI and OTA" "type:feature" "P1"
issue "F6: OTA updates" "Done means: Upload firmware from the web UI; later pull signed builds from GitHub Releases" "M5 Web UI and OTA" "type:feature" "P1"
issue "F7: Home Assistant / MQTT" "Done means: Check-in every 15-30 min: next alarm, battery level, room temperature via MQTT discovery; no live snooze" "M8 Integrations (after v1.0)" "type:feature" "P2"
issue "F8: Widget modules" "Done means: Add, remove, place widgets (clock, next alarm, temperature, weather, calendar, HA entity) from the web UI, no reflash" "M6 Widgets" "type:feature" "P1"
issue "F9: Weather widget" "Done means: Current + today's forecast from Open-Meteo, refreshed every 30 min" "M6 Widgets" "type:feature" "P1"
issue "F10: Calendar widget" "Done means: Next events from a HA calendar entity; direct ICS URL as follow-up" "M8 Integrations (after v1.0)" "type:feature" "P2"
issue "F11: Room temperature" "Done means: SHT40 over I2C shown on the clock face; vented pocket away from board and LED heat; offset calibration in config" "M2 Clock core" "type:feature" "P1"
issue "F12: NTP synchronization" "Done means: Sync at boot and every 6 h against a configurable server, POSIX TZ with DST, written to the RTC; runs from the RTC when Wi-Fi is down" "M2 Clock core" "type:feature" "P0"
issue "S1: shell fit test" "After M0. Measure the real board, set enclosure/inkwake.scad parameters, print the front shell, check board fit, display window alignment and touch access." "M2 Clock core" "type:chore" "area:shell" "P1"
issue "S2: shell features" "With M4. Speaker grille and mount, LED ring diffuser, walled-off vented pocket for the temperature sensor, battery bay, USB-C charging port, copper pads, back plate with heat-set inserts." "M4 Sound and sunrise" "type:chore" "area:shell" "P1"
issue "S3: final shell and print guide" "With M7. Cosmetics, tilt and stability check, PETG print profile, assembly photos and print guide in docs/." "M7 v1.0 release" "type:chore" "area:shell" "P1"
issue "M0: measure sleep current and pick the board" "Breadboard FeatherS3 or FireBeetle 2 S3 with panel, RV-3028, SHT40, amp and LED ring behind a MOSFET. Measure whole-clock sleep current (gate: under 100 uA), partial-refresh ghosting and pad sensitivity. Record the result in docs/adr/0002-board.md and hardware/wiring.md." "M0 Hardware spike" "type:chore" "P0"
issue "M1: pin pioarduino platform release" "Replace the stable URL in firmware/platformio.ini with a tagged release so builds are reproducible." "M1 Repo and CI" "type:chore" "P0"
issue "M6: simulator renders screens to PNG" "Native env renders the UI to PNG; CI attaches it to PRs." "M6 Widgets" "type:chore" "P1"
