# 0017. Alarms: scheduling, ringing and input before the touch pads

- Status: Accepted
- Date: 2026-11-23
- Amended by ADR-0018 (2026-11-25): alarms also edited with KEY + BOOT in the button menu (#55); KEY held 2 s on the face opens the menu unless an alarm is snoozed.

## Context

F1 (#2) brings any number of alarms (cap 16) with label, time, weekday mask, sound, sunrise and on/off. M3's gate is a week on the nightstand with zero missed alarms. Custom sounds (F3), sunrise light (F4), the touch pads (F2) and the web UI (F5) come later, so the alarm needs a sound, a way to stop it and a way to edit it that work now. The clock deep-sleeps between minutes on battery (ADR-0013), and local time has DST jumps (#13).

## Decision

- **Time line:** the scheduler works in UTC (lib/alarm_sched): each alarm's next fire instant is its local wall time turned into UTC with the zone's rules. Every wake checks which alarm came due between the last check and now.
- **Clock changes:** an alarm in the skipped spring hour rings at the jump (02:30 rings at 03:00). In the repeated autumn hour it rings once, at the first instance.
- **Missed alarms** are skipped, never rung late: an alarm counts as due only up to 2 min after its time. This covers time not set, the board switched off, or a jump from `set`.
- **One-shot alarms** (no weekdays) turn themselves off after ringing.
- **Sound until F3:** a generated 2 kHz beep pattern (four short beeps, a pause) through the on-board ES8311 codec and speaker (ADR-0006). It starts at about -20 dBFS and rises 5 % every 20 s to about -4 dBFS. F3 replaces it with the wake sequence and keeps it as the fallback if the TF card fails.
- **Input until F2:** KEY tap = snooze 9 min (at most 3), KEY held 2 s = stop. While snoozed, KEY held 2 s stops it too. F2 puts the same gestures on the top pad.
- **Unattended:** 10 min ringing without input auto-snoozes. After the third snooze it rings once more for 10 min, then gives up.
- **The board stays awake while ringing**, with the minute's display update and beeps in the same loop. A snooze sleeps with a timer that ends it on time, not on the next minute wake.
- **Battery:** alarms ring even at critical battery; only Wi-Fi is skipped there (#36).
- **Storage:** a compact versioned blob in NVS (`inkwake`/`alarms`), copied into RTC memory so wakes don't read NVS. The LittleFS JSON config (ADR-0005) keeps NVS as its mirror for alarms.
- **Editing:** serial `alarm` commands, and an Alarms section on the Wi-Fi setup page (ADR-0016), reached with KEY held 5 s. The page's API (`GET`/`POST /api/alarms`) carries over to the F5 web UI. An alarm coming due closes setup and rings.
- **Face:** the next alarm (bell + time, weekday if more than a day away), or "Zz" + the snooze end, shares the temperature's bottom line.

## Alternatives rejected

- Minute counters in local time (the original scaffold): breaks at DST jumps and after missed wakes.
- Ringing late after a missed alarm: a 07:00 alarm going off at 11:00 after a power cut is worse than none.
- PWM tone on a GPIO: the speaker is wired only through the codec and amp.
- Waiting for F2/F5 for stop and editing: the clock couldn't wake anyone in M3.

## Consequences

- The scheduler is unit-tested on the host, including DST nights; the ES8311 register sequence (taken from Espressif's esp_codec_dev, as in Waveshare's audio example) is checked on the board only.
- Sound name and sunrise minutes are stored and edited later (F3, F4); they have no effect yet.
- Volume levels and the beep pattern are starting points, to tune by ear on the board.
