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
label "P0" b60205; label "P1" d93f0b; label "P2" fbca04

milestone "M0 Hardware spike" "Confirm the board, map spare GPIO, test touch, partial refresh, power draw. Exit gate: 4+ spare GPIO for I2S and LED; ghosting acceptable with per-minute partial refresh."
milestone "M1 Repo and CI" "PlatformIO project, native unit tests, simulator, GitHub Actions, licences. Exit gate: CI green on main; simulator renders a clock face to PNG."
milestone "M2 Clock core" "RTC + NTP, timezones, Wi-Fi provisioning, clock face with partial refresh. Exit gate: 24 h unattended run; correct time after power cycle with Wi-Fi off."
milestone "M3 Alarms MVP" "F1, F2. Scheduler, snooze state machine, buzzer fallback, NVS persistence. Exit gate: 7-day soak: zero missed alarms, including Wi-Fi off and power cycles."
milestone "M4 Sound and sunrise" "F3, F4. I2S amp, custom sounds, volume ramp, SK6812 sunrise. Exit gate: No brownout with LEDs and amp at maximum together."
milestone "M5 Connected" "F5, F6, F7. Web UI, OTA, Home Assistant via MQTT discovery. Exit gate: Configure and update without USB; HA sees and controls alarms."
milestone "M6 Widgets" "F8, F9, F10. Widget engine, layout config, weather, calendar. Exit gate: Add a widget from the web UI with no reflash."
milestone "M7 v1.0" "Final enclosure, BOM, build guide, browser flasher, tagged release. Exit gate: Someone outside the project builds one from the docs alone."

issue "F1: Multiple alarms" "Done means: Any number of alarms (cap 16): label, time, weekday mask, sound, sunrise on/off, enabled" "M3 Alarms MVP" "type:feature" "P0"
issue "F2: Touch snooze/dismiss" "Done means: Tap = snooze, long-press or swipe = dismiss; audio stops on touch-down, before redraw" "M3 Alarms MVP" "type:feature" "P0"
issue "F3: Custom sounds" "Done means: WAV/MP3 from microSD over I2S amp, per-alarm sound, volume ramp" "M4 Sound and sunrise" "type:feature" "P1"
issue "F4: Sunrise light" "Done means: Warm-white LED ramp N minutes before the alarm, per alarm" "M4 Sound and sunrise" "type:feature" "P1"
issue "F5: Web UI over Wi-Fi" "Done means: Manage alarms, sounds, layout, settings from a browser; config export/import" "M5 Connected" "type:feature" "P1"
issue "F6: OTA updates" "Done means: Upload firmware from the web UI; later pull signed builds from GitHub Releases" "M5 Connected" "type:feature" "P1"
issue "F7: Home Assistant / MQTT" "Done means: Alarms, next alarm, ringing state via MQTT discovery; snooze/dismiss from HA" "M5 Connected" "type:feature" "P2"
issue "F8: Widget modules" "Done means: Add, remove, place widgets (clock, next alarm, weather, calendar, HA entity) from the web UI, no reflash" "M6 Widgets" "type:feature" "P1"
issue "F9: Weather widget" "Done means: Current + today's forecast from Open-Meteo, refreshed every 30 min" "M6 Widgets" "type:feature" "P2"
issue "F10: Calendar widget" "Done means: Next events from a HA calendar entity; direct ICS URL as follow-up" "M6 Widgets" "type:feature" "P2"
issue "M0: confirm board and spare GPIO" "Measure spare GPIO, touch latency, partial-refresh ghosting and power draw. Record the result in docs/adr/0002-board.md and hardware/wiring.md." "M0 Hardware spike" "type:chore" "P0"
issue "M1: pin pioarduino platform release" "Replace the stable URL in firmware/platformio.ini with a tagged release so builds are reproducible." "M1 Repo and CI" "type:chore" "P0"
issue "M1: simulator renders clock face to PNG" "Native env renders the clock screen to PNG; CI attaches it to PRs." "M1 Repo and CI" "type:chore" "P1"
