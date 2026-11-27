# 0017. Alarms: scheduling, ringing and input before the touch pads

- Status: Accepted
- Date: 2026-11-23
- Amended by ADR-0018 (2026-11-25): alarms also edited with KEY + BOOT in the button menu (#55); KEY held 2 s on the face opens the menu unless an alarm is snoozed.
- Amended 2026-11-25 (#43): the beep volume is set per alarm (level 1-10, rising or steady).
- Amended 2026-11-27 (#44): `sound` picks a WAV file per alarm (ADR-0006 amendment); `default` stays the beep, and the beep is the fallback whenever the file can't be played.
- Amended 2026-11-27 (#4): optional lead-in per alarm, see below.

## Context

F1 (#2) brings any number of alarms (cap 16) with label, time, weekday mask, sound, sunrise and on/off. M3's gate is a week on the nightstand with zero missed alarms. Custom sounds (F3), sunrise light (F4), the touch pads (F2) and the web UI (F5) come later, so the alarm needs a sound, a way to stop it and a way to edit it that work now. The clock deep-sleeps between minutes on battery (ADR-0013), and local time has DST jumps (#13).

## Decision

- **Time line:** the scheduler works in UTC (lib/alarm_sched): each alarm's next fire instant is its local wall time turned into UTC with the zone's rules. Every wake checks which alarm came due between the last check and now.
- **Clock changes:** an alarm in the skipped spring hour rings at the jump (02:30 rings at 03:00). In the repeated autumn hour it rings once, at the first instance.
- **Missed alarms** are skipped, never rung late: an alarm counts as due only up to 2 min after its time. This covers time not set, the board switched off, or a jump from `set`.
- **One-shot alarms** (no weekdays) turn themselves off after ringing.
- **Sound until F3:** a generated 2 kHz beep pattern (four short beeps, a pause) through the on-board ES8311 codec and speaker (ADR-0006). It starts at about -20 dBFS and rises 5 % every 20 s to about -4 dBFS. Since #43 each alarm has a volume level 1-10 and a ramp switch: the top is 50 + 5 x level % on the 0-100 % scale (-40..0 dBFS, so level 1 is about -18 dBFS, 8 = the -4 dBFS above, 10 = 0 dBFS); "rising" starts 40 % below the top and climbs 5 % every 20 s, "steady" rings at the top at once. New alarms and alarms saved before #43 (NVS blob version 1) get level 8, rising, so they ring as before. There is no level 0, so an alarm can't be made silent; values outside 1-10 are refused, not clamped. Volume is per alarm (blob version 2 adds the byte) because F3's per-alarm sound will carry it too. Edited in the alarm editor's second page (button menu), with serial `alarm volume ID N` / `alarm ramp ID on|off`, and on the setup page; trying a level without saving: `alarm beep N`, the page's Test button, and a short beep on each change in the button menu. F3 replaces it with the wake sequence and keeps it as the fallback if the TF card fails.
- **Input until F2:** KEY tap = snooze 9 min (at most 3), KEY held 2 s = stop. While snoozed, KEY held 2 s stops it too. F2 puts the same gestures on the top pad.
- **Unattended:** 10 min ringing without input auto-snoozes. After the third snooze it rings once more for 10 min, then gives up.
- **The board stays awake while ringing**, with the minute's display update and beeps in the same loop. A snooze sleeps with a timer that ends it on time, not on the next minute wake.
- **Battery:** alarms ring even at critical battery; only Wi-Fi is skipped there (#36).
- **Storage:** a compact versioned blob in NVS (`inkwake`/`alarms`), copied into RTC memory so wakes don't read NVS. The LittleFS JSON config (ADR-0005) keeps NVS as its mirror for alarms.
- **Editing:** serial `alarm` commands, and an Alarms section on the Wi-Fi setup page (ADR-0016), reached with KEY held 5 s. The page's API (`GET`/`POST /api/alarms`) carries over to the F5 web UI. An alarm coming due closes setup and rings.
- **Face:** the next alarm (bell + time, weekday if more than a day away), or "Zz" + the snooze end, shares the temperature's bottom line.

## Amendment 2026-11-27: lead-in (#4, decided with the user)

- **What:** per alarm, Off (default, also for alarms saved before) or 5, 10, 15, 20 min. The alarm's own sound (beep or file) starts that much before the alarm time and keeps getting louder; at the alarm time it rings on without a gap with the alarm's volume and rising/steady setting. One sound per alarm (rejected: a separate, softer lead-in sound).
- **Loudness:** from -60 dBFS, linear in dB, to 15 dB under the level's top at the alarm time (`lead_atten_half_db`). A rising ring starts 16 dB under the top, so it carries on from about where the lead-in ended; a steady one steps up to the top. The fade uses the ES8311's DAC volume (0.5 dB steps) with full-scale samples, so the quiet end stays smooth; ringing goes back to 0 dB on the codec and scales the samples as before. Starting points, to tune by ear.
- **Buttons during a lead-in:** KEY tap quiets the lead-in only, the alarm still rings at its time (kept in RTC memory, `lead_quiet`, so the next minute wake does not restart it). KEY held 2 s or a BOOT tap skips this ring, as BOOT on the face does.
- **When:** checked on every wake and in the awake loop; the board stays awake while it plays (the amp is on anyway). A lead-in that starts late (menu or setup open, power-on) picks up at the loudness of that moment. Not while the time is not set, another alarm rings or snoozes, or the battery is critical (the alarm itself still rings). If another alarm comes due during it, the lead-in stops and that alarm rings with its own sound.
- **Storage:** alarm blob version 3 adds a lead-in byte (`sched::kBlobFixed` 10); versions 1 and 2 decode with no lead-in. Firmware from before #4 starts with no alarms after a downgrade.
- **Editing:** the alarm editor's second page (button menu), serial `alarm lead ID 0|5|10|15|20`, the setup page's Lead-in select (`lead` on `POST /api/alarms`, kept when left out).
- **Sunrise (F4, #5)** joins the same sequence later: the light leads for 20-30 min, the sound's lead-in covers the last part.

## Alternatives rejected

- Minute counters in local time (the original scaffold): breaks at DST jumps and after missed wakes.
- Ringing late after a missed alarm: a 07:00 alarm going off at 11:00 after a power cut is worse than none.
- PWM tone on a GPIO: the speaker is wired only through the codec and amp.
- Waiting for F2/F5 for stop and editing: the clock couldn't wake anyone in M3.

## Consequences

- The scheduler is unit-tested on the host, including DST nights; the ES8311 register sequence (taken from Espressif's esp_codec_dev, as in Waveshare's audio example) is checked on the board only.
- Sunrise minutes are stored and edited later (F4); they have no effect yet. The sound name plays a file since #44 (serial `alarm sound`, the page's Sound field; not in the button menu yet).
- The level scale (55-100 %) and the beep pattern are starting points, to tune by ear on the board. Firmware from before #43 can't read a version 2 blob and starts with no alarms after a downgrade.
