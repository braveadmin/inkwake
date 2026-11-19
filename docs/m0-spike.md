# M0 hardware spike

Procedure and results for M0 ([roadmap](roadmap.md)). Firmware: `firmware/spike/` (env `spike`), thrown away after M2.
Test piece: `enclosure/touch_test.scad`.

**Exit gate:** whole-board sleep under 1.5 mA with the screen showing (mode 1).

## Already known from Waveshare's demo code

Source: [waveshareteam/ESP32-S3-RLCD-4.2](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2) (Apache-2.0), read 2026-11-18.

| Fact | Value | Where |
| --- | --- | --- |
| Display pins | SCK 11, MOSI 12, DC 5, CS 40, RST 41 (matches wiring.md) | `10_U8G2_Test.ino`, ESPHome yaml |
| KEY button | GPIO18, active low, external pull-up | `esphome/examples/esp32-s3-rlcd-42-sensor.yaml` |
| BOOT button | GPIO0, active low | same |
| Battery sense | GPIO4 (ADC1_CH3), 12 dB, divider 1:3 | `03_ADC_Test/adc_bsp.cpp`, ESPHome yaml |
| PWR button | Hardware power switch: long press = off, click = on. No GPIO found | wiki |
| ST7305 init | Copied into `firmware/spike/rlcd.cpp`; demos end in HPM (0x38), none use LPM (0x39) | `09_LVGL_V9_Test/display_bsp.cpp` |

Still to confirm on the board or schematic: PWR circuit (does it cut the battery completely?), whether the LCD and codec rails stay powered in deep sleep, and whether the 3V3 regulator is low-Iq.

## Setup

1. Battery in, USB **unplugged** for every current reading (USB powers the board and hides the battery draw).
2. Meter in series with the 18650: break the positive lead with a battery-holder spacer or a cut wire. Use the µA/mA range; a uCurrent or Nordic PPK2 shows the wake pulses better.
3. Flash over USB, then unplug:

   ```bash
   cd firmware
   pio run -e spike -t upload && pio device monitor
   ```

4. Start a mode from the serial menu, unplug USB, wait for the sleep current to settle (at least 10 s), and read the meter. Press KEY to return to the menu (re-plug USB to see it).

The screen shows a fake clock that advances one minute per wake, the mode, battery voltage, cycle count and how long the last wake took.

## Current measurements

Average current ≈ I_sleep + (I_awake − I_sleep) × t_awake / 60 s. `t_awake` is on the screen (AWAKE … MS).

| Mode | What | I_sleep | I_awake (peak) | t_awake | Image stays? | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Deep sleep, panel LPM | | | | Yes, advances each minute, no flicker (2026-11-18) | **exit gate** |
| 2 | Deep sleep, panel HPM | | | | | |
| 3 | Light sleep, panel LPM | | | | | |
| 4 | Deep sleep, panel off | | | | | baseline without screen |
| 1 + touch | Mode 1 with `t <n>` set | | | | | cost of touch wake |

Things to look for:

- Does the image survive deep sleep in mode 1 (RST/CS/DC held)? If it blanks or flickers, use light sleep (mode 3) instead.
- Does it flash or flicker when switching LPM → HPM → LPM for each redraw?
- How much worse does LPM look than HPM (contrast, viewing angle)?
- Does the codec or amp draw anything with PA_CTRL low? Compare mode 4 with the bare ESP32-S3 deep-sleep figure (~10 µA).
- If mode 1 misses the gate, find the floor: remove the TF card, check the charger IC and LEDs, and look for the PWR circuit's quiescent draw.

Battery life at 2500 mAh: 1.0 mA ≈ 104 days, 1.5 mA ≈ 69 days. ADR-0013 targets 5-8 weeks including Wi-Fi.

## Pins and buttons

| Check | Result |
| --- | --- |
| `w`: KEY reads 1 when pressed | Pass (2026-11-18) |
| `w`: BOOT reads 1 when pressed | Pass (2026-11-18) |
| KEY wakes from deep sleep (modes 1, 2, 4) | |
| `b` matches a multimeter on the cell (± 0.05 V) | |
| PWR long press: board fully off? current with PWR off | |

## Touch pads

1. Print `enclosure/touch_test.stl` in PETG (cells with 1.0, 1.6, 2.4, 3.2 mm skin).
2. Put a 20 × 20 mm copper tape pad in each pocket. Wire one pad at a time to GPIO1 (header), keeping the wire under 10 cm.
3. Run `w`. Note the untouched raw value, the value with a finger on the front skin, and with a palm. On the S3 the value **rises** on touch.
4. Repeat on battery only (USB unplugged changes the ground reference); watch via a short USB session afterwards, or note the wake behaviour.
5. Set a deep-sleep threshold with `t <n>` (for the S3 this is a delta above the benchmark; start at half the measured finger delta). Run mode 1 and check that a touch wakes it but a hand nearby doesn't.

Bare-wire check (2026-11-18, USB powered, paper clip held against the base of each pin): untouched 25.7k-27.1k with ±20 noise; touched +117k to +433k (GPIO1), +494k to +1.05M (GPIO2), +216k to +250k (GPIO3). The pad next to a touched one drifts up to +2k (crosstalk from the hand and wires).

| Skin | Untouched | Finger | Delta | Wakes from deep sleep at threshold | Notes |
| --- | --- | --- | --- | --- | --- |
| 1.0 mm | | | | | |
| 1.6 mm | | | | | |
| 2.4 mm | | | | | shell wall |
| 3.2 mm | | | | | |

Known limit: the ESP32-S3 deep-sleep touch wake watches **one** channel. The top pad (GPIO1) wakes the board; the side pad needs light sleep or KEY. Confirm on the board.

## Outcome

Status 2026-11-18: partly done, current measurement deferred.

- Done: display, KEY, BOOT, battery sense (GPIO4, reads 4.14 V on USB), touch on bare pins (GPIO1-3).
- Deep sleep with the panel in LPM and DC/CS/RST held keeps the image, redraws each minute without flicker. Firmware assumes this strategy (deep sleep + held pins).
- Deferred to the hardware phase before v1.0: sleep current (the meter used had no fuse in its mA range and was damaged; needs a fused meter with a µA/mA input), PWR circuit, touch through the printed test plate.
- Postponed, low priority (2026-11-19): an RTC backup cell on J7 (`RTC_BAT`, rechargeable ML1220 only, never CR1220). It would keep the time through PWR off and a flat 18650, so the M2 gate could go back to "correct time after power cycle". Check the connector type and polarity on the board, how the cell is charged (straight from 3V3 through a B5819, no current limit), and its effect on sleep current. Only worth doing if losing the time becomes a real problem: in normal use the 18650 stays in and the clock is not switched off, and NTP (#13) restores the time after a power loss.

Still to fill in then: gate pass/fail, pad skin thickness, and updates to ADR-0013 and ADR-0015.
