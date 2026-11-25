// SPDX-License-Identifier: GPL-3.0-only
// Button menu screens (#41): a list with the chosen item marked, and the date &
// time editor with the field being changed marked. What the buttons do is
// written along the top edge, next to the buttons themselves. Full refresh each time.
#pragma once

#include <cstddef>

#include "framebuffer.h"

namespace inkwake {

constexpr size_t kListRows = 5;  // items visible at once; longer lists scroll

// The board's buttons sit on the top edge, centred, about 6 mm apart (KEY,
// PWR, BOOT from the left, seen from the front). Panel x of their centres at
// ~4.7 px/mm; adjust here if the arrows don't line up.
constexpr int kKeyButtonX = 172;
constexpr int kPwrButtonX = 200;
constexpr int kBootButtonX = 228;
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

// Values of the date & time editor; `field` 0-4 = year, month, day, hour,
// minute is marked (white on black).
struct DateTimeView {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int field;
};

// Clears the framebuffer and draws `title`, "2026-11-25" over "14:07" in large
// digits, the weekday under the date, and the button labels.
void render_datetime(Framebuffer& fb, const char* title, const DateTimeView& v,
                     const ButtonLabels& buttons);

// Screen area of a field in render_datetime, including its black mark. For tests.
Rect datetime_field_rect(int field);

}  // namespace inkwake
