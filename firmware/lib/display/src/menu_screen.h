// SPDX-License-Identifier: GPL-3.0-only
// Button menu screens (#41): a list with the chosen item marked, and the date &
// time editor with the field being changed marked. A short button hint sits
// on the bottom line. Full refresh each time.
#pragma once

#include <cstddef>

#include "framebuffer.h"

namespace inkwake {

constexpr size_t kListRows = 5;  // items visible at once; longer lists scroll

// Clears the framebuffer and draws `title`, up to kListRows of `items` around
// `selected` (white on a black bar), and `hint`.
void render_list(Framebuffer& fb, const char* title, const char* const* items, size_t count,
                 size_t selected, const char* hint);

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
// digits, the weekday under the date, and `hint`.
void render_datetime(Framebuffer& fb, const char* title, const DateTimeView& v, const char* hint);

// Screen area of a field in render_datetime, including its black mark. For tests.
Rect datetime_field_rect(int field);

}  // namespace inkwake
