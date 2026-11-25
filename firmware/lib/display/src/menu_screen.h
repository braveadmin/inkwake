// SPDX-License-Identifier: GPL-3.0-only
// Button menu screens (#41): a list with the chosen item marked, and the date &
// time editor with the field being changed marked. What the buttons do is
// written along the top edge, next to the buttons themselves. Full refresh each time.
#pragma once

#include <cstddef>
#include <cstdint>

#include "framebuffer.h"

namespace inkwake {

constexpr size_t kListRows = 5;  // items visible at once; longer lists scroll

// The board's buttons sit on the top edge (KEY, PWR, BOOT from the left, seen
// from the front), about 10 mm apart and a little right of centre. Panel x of
// their centres, measured from a photo of the board (2026-11-25).
constexpr int kKeyButtonX = 160;
constexpr int kPwrButtonX = 208;
constexpr int kBootButtonX = 255;
constexpr int kButtonLabelGap = 14;  // label text keeps this far from the arrow's centre

// What each button does on the current screen. KEY's text ends left of KEY,
// BOOT's starts right of BOOT; a small arrow points up at each button.
struct ButtonLabels {
  const char* key;       // KEY tap
  const char* key_hold;  // KEY held, second line
  const char* boot;
};

// Clears the framebuffer and draws `title`, up to kListRows of `items` around
// `selected` (white on a black bar), and the button labels.
void render_list(Framebuffer& fb, const char* title, const char* const* items, size_t count,
                 size_t selected, const ButtonLabels& buttons);

// First item shown, so that `selected` is visible (kept in the middle while it can be).
size_t list_first_row(size_t count, size_t selected);

// Save and Cancel along the bottom of both editors (#55): KEY marks them after
// the last field. 0 = neither marked.
constexpr int kEditorSave = 1;
constexpr int kEditorCancel = 2;

// Values of the date & time editor; `field` 0-4 = year, month, day, hour,
// minute is marked (white on black), -1 = none (`button` is marked).
struct DateTimeView {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int field;
  int button;  // 0, kEditorSave or kEditorCancel
};

// Clears the framebuffer and draws `title`, "2026-11-25" over "14:07" in large
// digits, the weekday under the date, Save and Cancel, and the button labels.
void render_datetime(Framebuffer& fb, const char* title, const DateTimeView& v,
                     const ButtonLabels& buttons);

// Screen area of a field in render_datetime, including its black mark. For tests.
Rect datetime_field_rect(int field);

// Values of the alarm editor (#55). `field`: 0 hour, 1 minute, 2 days preset,
// 3-9 Monday .. Sunday (only while the preset is Custom).
struct AlarmEditView {
  int hour;
  int minute;
  const char* days;  // preset name: "Mon-Fri", "Custom", ...
  uint8_t mask;      // days the alarm rings, bit 0 = Monday
  int field;         // -1 = none (`button` is marked)
  int button;        // 0, kEditorSave or kEditorCancel
};

// Clears the framebuffer and draws `title`, "07:00" in large digits, the days
// preset under it, a row of the seven days (filled = rings that day), Save and
// Cancel, and the button labels. The field being changed is marked: hour, minute and preset
// white on black, a day with a bar under it.
void render_alarm_edit(Framebuffer& fb, const char* title, const AlarmEditView& v,
                       const ButtonLabels& buttons);

// Screen area of a field in render_alarm_edit, including its mark. For tests.
Rect alarm_field_rect(const AlarmEditView& v, int field);

// Screen area of the Save or Cancel button in both editors. For tests.
Rect editor_button_rect(int button);

}  // namespace inkwake
