# 0018. Analog and Advanced modes: the clock fully usable with KEY + BOOT

- Status: Accepted
- Date: 2026-11-25

## Context

Until now the time could only be set over USB serial (`set`) or through Wi-Fi setup and NTP (#13, #20). After a power loss with no Wi-Fi in reach the face stayed on `--:--` / "Time not set", and alarms could only be edited over serial or on the setup page (ADR-0017). An alarm clock should work out of the box, with no phone and no network. The board has three buttons: PWR has no GPIO (ECJ23001 on/off controller) and a long press cuts power and the RTC time; KEY (GPIO18) wakes from deep sleep; BOOT (GPIO0) is a strapping pin. The touch pads of ADR-0015 need the shell (hardware phase).

## Decision

- **Two modes, no setting for them (#54).** Analog (default): no Wi-Fi network saved; date, time, timezone and alarms are set with KEY + BOOT in a menu drawn on the panel. Advanced (provisional name): at least one network saved; adds NTP, the setup/web page, custom sounds, OTA and widgets. The button menu stays fully usable there; a time set by hand holds until the next NTP sync corrects it. Saving a network turns Advanced on; Wi-Fi -> Forget all in the menu returns to Analog.
- **Buttons: KEY + BOOT only.** PWR is not used. BOOT is read only while the clock is awake (a KEY press wakes it), so its untested deep-sleep wake and the download-mode trap at reset don't matter. No new buttons or GPIOs until the hardware phase.
- **Gestures (#41):**

  | Where | KEY | BOOT |
  | --- | --- | --- |
  | Clock face | held 2 s = menu (opens on release; the panel shows "Menu" at 2 s); held on to 5 s = Wi-Fi setup; tap while "Time not set" = date & time editor | (#55: tap = next alarm off/on) |
  | Menu list | tap = open item; held 1 s = back (closes the menu on the main list) | tap = next item (held = repeat) |
  | Editing a value | tap = next field, the last field saves; held 1 s = back without saving | tap = +1 (held = repeat after 0.5 s, every 0.15 s), wraps around |
  | Ringing / snoozed | tap = snooze, held 2 s = stop (ADR-0017, unchanged; while snoozed the 2 s hold stops the alarm instead of opening the menu) | - |

  The menu closes without saving after 60 s without a press, so a stray press can't keep the clock awake on battery. An alarm coming due closes the menu and rings.
- **Menu:** Date & time (#41), Alarms (#55), Timezone (#41), Volume (#43), Wi-Fi (#41: Set up Wi-Fi, Forget all networks with a confirmation that starts on Cancel). Date and time are one editor: year, month, day, hour, minute, in that order. Saving converts local time to UTC as serial `set` does (skipped spring hour moves an hour on, repeated autumn hour takes the first), writes the RTC with seconds at 0 and restarts the drift measurement (#13).
- **Timezone presets** with automatic DST, saved as the POSIX string in NVS `inkwake`/`tz` like serial `tz`: Madrid / Paris / Berlin (default), London / Lisbon, Athens / Helsinki, Moscow, New York, Chicago, Denver, Los Angeles, Tokyo, Sydney, UTC. A zone is matched by its offsets and DST rules, not its name, so `WET0WEST` set on the setup page shows as London / Lisbon. A zone not in the list shows as "Custom" and stays until another preset is picked.
- **First boot (#34):** no time and no Wi-Fi opens the date & time editor (timezone first) with a hint for the 5 s Wi-Fi hold, instead of the Wi-Fi setup page.
- Menu navigation, button timing (debounce, tap, hold, repeat), field ranges, days per month and leap years, and the timezone list are hardware-free (`firmware/lib/menu`) with native tests; the screens are in `lib/display` (`menu_screen`) and previewed with `tools/preview_face.cpp --menu`.

## Alternatives rejected

- PWR as a third button: no GPIO, and a long press cuts power and the time.
- BOOT as a deep-sleep wake: strapping pin; held at a reset it enters download mode. Reading it only while awake costs nothing, since KEY wakes the clock.
- Separate items for date and time: more menu entries for the same five fields; the editor skips a field with one KEY tap.
- Waiting for the touch pads (ADR-0015) or the web UI (F5): the clock couldn't be set without a phone until the hardware phase.

## Consequences

- The clock is usable with no Wi-Fi at all; Wi-Fi becomes an extra.
- Two buttons and hold gestures need labels on every menu screen. They are drawn along the top edge right next to the buttons (KEY, PWR, BOOT from the left, ~10 mm apart, a little right of centre: panel x 160, 208, 255, measured from a photo), with a small arrow at KEY and BOOT, instead of naming the buttons, since the board's silkscreen names mean nothing to a user. The button positions and the timings are first guesses to check on the board (#41).
- Each later setting (alarms #55, volume #43) is a menu item plus a page section on the web UI; the two edit the same NVS keys.
- Amends ADR-0015 (KEY + BOOT is the main input until the pads exist), ADR-0016 (first boot no longer opens setup, #34) and ADR-0017 (alarms editable with the buttons, #55).
