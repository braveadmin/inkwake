# 0018. Analog and Advanced modes: the clock fully usable with KEY + BOOT

- Status: Accepted
- Date: 2026-11-25
- Amended: 2026-11-25 (#55: alarm editor; BOOT skips the next ring and wakes the board; one role per button in the menu, Back rows, Save / Cancel in the editors)
- Amended: 2026-11-25 (#43: volume is set per alarm in the alarm editor, not as its own menu item)
- Amended: 2026-11-27 (#59: a hold screen shows progress from 2 s to 5 s towards Wi-Fi setup)

## Context

Until now the time could only be set over USB serial (`set`) or through Wi-Fi setup and NTP (#13, #20). After a power loss with no Wi-Fi in reach the face stayed on `--:--` / "Time not set", and alarms could only be edited over serial or on the setup page (ADR-0017). An alarm clock should work out of the box, with no phone and no network. The board has three buttons: PWR has no GPIO (ECJ23001 on/off controller) and a long press cuts power and the RTC time; KEY (GPIO18) wakes from deep sleep; BOOT (GPIO0) is a strapping pin. The touch pads of ADR-0015 need the shell (hardware phase).

## Decision

- **Two modes, no setting for them (#54).** Analog (default): no Wi-Fi network saved; date, time, timezone and alarms are set with KEY + BOOT in a menu drawn on the panel. Advanced (provisional name): at least one network saved; adds NTP, the setup/web page, custom sounds, OTA and widgets. The button menu stays fully usable there; a time set by hand holds until the next NTP sync corrects it. Saving a network turns Advanced on; Wi-Fi -> Forget all in the menu returns to Analog.
- **Buttons: KEY + BOOT only.** PWR is not used. No new buttons or GPIOs until the hardware phase. BOOT wakes the clock from deep sleep too (#55, ext1 with KEY and the RTC interrupt; external pull-up, so no RTC pull-up): the ESP32-S3 latches its strapping pins only at a chip reset (power-on, brown-out, watchdog), not on a deep-sleep wake, so a BOOT press on the sleeping clock does not enter download mode. To verify on the board. BOOT is left out of the wake pins while held down when the clock goes to sleep, so a held button cannot wake it over and over.
- **Gestures (#41, roles made uniform in #55):** in the whole menu KEY moves the mark and BOOT acts on what is marked. The first version had them the other way round in lists (BOOT = next item, KEY = open) and as now in the editors (KEY = next field, BOOT = +1); the board test showed the switch between the two confused.

  | Where | KEY | BOOT |
  | --- | --- | --- |
  | Clock face | held 2 s = menu (opens on release); held on to 5 s = Wi-Fi setup (from 2 s a hold screen says what letting go does and fills six segments, one per 0.5 s; at 5 s it reads "Wi-Fi setup", "Let go to start"; not after a hold that stopped a snoozed alarm, #59); tap while "Time not set" = date & time editor | tap = skip the next ring of the next alarm; tap again = undo (#55) |
  | Menu list | tap = next item (wraps); held 1 s = back (closes the menu on the main list) | tap = open / choose the marked item, including the last row Back (Close on the main list) |
  | Editor | tap = next field, then Save, then Cancel, then the first field again; held 1 s = back without saving | tap = +1 on a field (held = repeat after 0.5 s, every 0.15 s, only on numbers), wraps around; next days preset; a weekday on/off; volume +1 (10 wraps to 1, no repeat, plays a short beep at the new level); rising/steady; on Save saves, on Cancel leaves |
  | Ringing / snoozed | tap = snooze, held 2 s = stop (ADR-0017, unchanged; while snoozed the 2 s hold stops the alarm instead of opening the menu) | - |

  Every list but the first-start timezone list and the two confirmations (which start on Cancel) ends in a **Back** row (**Close** on the main list), so going back needs no hidden gesture; KEY held stays as a shortcut. Both editors end in **Save** and **Cancel** buttons drawn along the bottom, so saving is an explicit press, not "KEY on the last field". Cancel in the first-start date & time editor leaves the "Time not set" face. The menu closes without saving after 60 s without a press, so a stray press can't keep the clock awake on battery. An alarm coming due closes the menu and rings.
- **Menu:** Date & time (#41), Alarms (#55), Timezone (#41), Wi-Fi (#41: Set up Wi-Fi, Forget all networks with a confirmation that starts on Cancel). Date and time are one editor: year, month, day, hour, minute, in that order. Saving converts local time to UTC as serial `set` does (skipped spring hour moves an hour on, repeated autumn hour takes the first), writes the RTC with seconds at 0 and restarts the drift measurement (#13).
- **Alarms (#55):** a list with one row per alarm ("07:00 Mon-Fri", "off" appended when off) and "Add alarm" at the end (hidden at 16, `sched::kMaxAlarms`). An alarm opens Edit, Turn off / Turn on, Delete (with a confirmation that starts on Cancel). The editor has hour, minute and days; days cycle Once, Daily, Mon-Fri, Weekend, Custom, and Custom adds one field per weekday (BOOT turns it on or off), starting from the alarm's own days. A second page (#43) follows with the volume (1-10, large digits over ten level cells) and Rising / Steady; Save and Cancel sit under it. Volume was planned as its own menu item; it became per alarm (ADR-0017), so it lives in the alarm editor. A Custom alarm with no day left on is saved as Once. Saving an edit turns the alarm on; label and sound are kept (labels can't be typed with two buttons: they stay with serial `alarm label` and the setup page). A new alarm starts at 07:00, Mon-Fri, on, volume 8, rising, without a label. All three editors write the same NVS blob (`inkwake`/`alarms`) and RTC copy.
- **Skip the next ring (#55):** a BOOT tap on the face skips only the next ring of the soonest alarm, which stays on and rings again on its next day; a second tap undoes it. The face shows a crossed-out bell with the skipped time until that time has passed. The skip lives in RTC memory only (lost at a reset or power loss, as the time is). It is dropped when the alarm is moved, turned off or deleted, or the timezone moves its ring; a skipped Once alarm turns off at its time as if it had rung. Nothing happens while the time is not set or an alarm rings or snoozes. Serial `alarm skip` does the same. Chosen over turning the alarm off: a weekday alarm cannot be forgotten off.
- **Timezone presets** with automatic DST, saved as the POSIX string in NVS `inkwake`/`tz` like serial `tz`: Madrid / Paris / Berlin (default), London / Lisbon, Athens / Helsinki, Moscow, New York, Chicago, Denver, Los Angeles, Tokyo, Sydney, UTC. A zone is matched by its offsets and DST rules, not its name, so `WET0WEST` set on the setup page shows as London / Lisbon. A zone not in the list shows as "Custom" and stays until another preset is picked.
- **First start (#34):** no time and no Wi-Fi saved: power-on shows "Welcome to inkwake", then the timezone list and the date & time editor, instead of the Wi-Fi setup page. There is no "back" on these screens; KEY held 5 s opens Wi-Fi setup (the label under KEY says "hold 5 s: Wi-Fi"; from 2 s the same hold screen as on the face shows the progress, and letting go before 5 s returns to the screen without moving the mark, #59), and a connect + NTP sync sets the time. 60 s without a press leaves the "Time not set" face, where a KEY tap reopens the same screens. With a network saved and the time lost, the boot NTP sync sets it; if that fails, a KEY tap opens the date & time editor.
- Menu navigation, button timing (debounce, tap, hold, repeat), field ranges, days per month and leap years, and the timezone list are hardware-free (`firmware/lib/menu`) with native tests; the screens are in `lib/display` (`menu_screen`) and previewed with `tools/preview_face.cpp --menu`.

## Alternatives rejected

- PWR as a third button: no GPIO, and a long press cuts power and the time.
- BOOT read only while awake (the first version of this ADR): a skip would then need a KEY tap first to wake the clock. A deep-sleep wake does not sample the strapping pins, so BOOT now wakes it directly (#55). Held at a real reset BOOT still enters download mode, as on any ESP32 board.
- BOOT tap turning the next alarm off instead of skipping one ring: easy to forget it off for the rest of the week.
- Separate items for date and time: more menu entries for the same five fields; the editor skips a field with one KEY tap.
- Waiting for the touch pads (ADR-0015) or the web UI (F5): the clock couldn't be set without a phone until the hardware phase.

## Consequences

- The clock is usable with no Wi-Fi at all; Wi-Fi becomes an extra.
- Two buttons and hold gestures need labels on every menu screen. They are drawn along the top edge right next to the buttons (KEY, PWR, BOOT from the left, ~10 mm apart, a little right of centre: panel x 160, 208, 255, measured from a photo), with a small arrow at KEY and BOOT, instead of naming the buttons, since the board's silkscreen names mean nothing to a user. The button positions and the timings are first guesses to check on the board (#41).
- Each later setting (alarms #55) is a menu item plus a page section on the web UI; the two edit the same NVS keys.
- Amends ADR-0015 (KEY + BOOT is the main input until the pads exist), ADR-0016 (first boot no longer opens setup, #34) and ADR-0017 (alarms editable with the buttons, #55).
