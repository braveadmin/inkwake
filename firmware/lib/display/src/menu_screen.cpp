// SPDX-License-Identifier: GPL-3.0-only
#include "menu_screen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "font.h"
#include "fonts/text20.h"
#include "fonts/text30.h"
#include "fonts/value60.h"

namespace inkwake {

namespace {

// Button labels: two lines of 20 px text along the top edge.
constexpr int kLabelBaseline1 = 22;
constexpr int kLabelBaseline2 = 42;
constexpr int kArrowH = 8;  // arrow under each button, tip at y 0

constexpr int kTitleBaseline = 82;
constexpr int kListTop = 92;
constexpr int kRowH = 41;
constexpr int kRowTextDy = 30;  // baseline below the row top
constexpr int kRowMargin = 8;   // bar inset from the screen edges
constexpr int kRowTextX = 24;

constexpr int kDateBaseline = 145;
constexpr int kWeekdayBaseline = 172;
constexpr int kTimeBaseline = 232;
constexpr int kDigitGap = 2;  // between digits of one field
constexpr int kSepPad = 6;    // either side of "-" and ":"
constexpr int kMarkPad = 5;   // black mark around a field's digits

// Alarm editor (#55): time, days preset, then the row of seven days.
constexpr int kAlarmTimeBaseline = 145;
constexpr int kPresetBaseline = 188;
constexpr int kPresetPad = 6;  // black mark around the preset name
constexpr int kDayTop = 206;
constexpr int kDayW = 48;
constexpr int kDayH = 30;
constexpr int kDayGap = 6;
constexpr int kDayTextDy = 22;  // text baseline below the cell top
constexpr int kDayBarGap = 3;   // bar under the day being changed
constexpr int kDayBarH = 4;
// Alarm editor, second page (#43): volume digits, ten level cells, ramp.
constexpr int kVolumeBaseline = 145;
constexpr int kVolumeLabelGap = 16;  // "Volume" to the left of the digits
constexpr int kLevelTop = 162;
constexpr int kLevelW = 26;
constexpr int kLevelH = 18;
constexpr int kLevelGap = 4;
constexpr int kRampBaseline = 222;
// Save and Cancel, both editors: outlined, filled when marked.
constexpr int kButtonsBaseline = 284;
constexpr int kButtonW = 120;
constexpr int kButtonH = 34;
constexpr int kButtonCx[] = {130, 270};  // Save, Cancel
const char* const kButtonText[] = {"Save", "Cancel"};
constexpr int kDayBorder = 2;  // outline of a day that is off
const char* const kDay2[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

const char* const kWeekdays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};

// 0 = Monday. Sakamoto's method, valid for the Gregorian calendar.
int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  const int sun0 = (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
  return (sun0 + 6) % 7;
}

int cell_w() {
  int w = 0;
  for (char c = '0'; c <= '9'; ++c) w = std::max<int>(w, fonts::kValue60.glyph(c)->width);
  return w;
}

int digits_w(int n) { return n * cell_w() + (n - 1) * kDigitGap; }

int sep_w(char c) { return fonts::kValue60.glyph(static_cast<uint8_t>(c))->width + 2 * kSepPad; }

// One row of fields and separators, e.g. 2026 - 11 - 25.
struct Piece {
  char sep;    // 0 = a field
  int field;   // field number when sep == 0
  int digits;  // field width in digits
};

const Piece kDateRow[] = {{0, 0, 4}, {'-', 0, 0}, {0, 1, 2}, {'-', 0, 0}, {0, 2, 2}};
const Piece kTimeRow[] = {{0, 3, 2}, {':', 0, 0}, {0, 4, 2}};
const Piece kAlarmTimeRow[] = {{0, 0, 2}, {':', 0, 0}, {0, 1, 2}};

int piece_w(const Piece& p) { return p.sep ? sep_w(p.sep) : digits_w(p.digits); }

template <size_t N>
int row_w(const Piece (&row)[N]) {
  int w = 0;
  for (const Piece& p : row) w += piece_w(p);
  return w;
}

Rect mark_rect(int x, int w, int baseline) {
  const int asc = fonts::kValue60.ascent;
  return Rect{x - kMarkPad, baseline - asc - kMarkPad, w + 2 * kMarkPad, asc + 2 * kMarkPad};
}

// Finds a field's x and width in its row. False if `field` is not in it.
template <size_t N>
bool field_x(const Piece (&row)[N], int field, int& x, int& w) {
  x = (Framebuffer::kWidth - row_w(row)) / 2;
  for (const Piece& p : row) {
    if (!p.sep && p.field == field) {
      w = piece_w(p);
      return true;
    }
    x += piece_w(p);
  }
  return false;
}

template <size_t N>
void draw_row(Framebuffer& fb, const Piece (&row)[N], const int* values, int marked, int baseline) {
  int x = (Framebuffer::kWidth - row_w(row)) / 2;
  const int cw = cell_w();
  for (const Piece& p : row) {
    const int w = piece_w(p);
    if (p.sep) {
      draw_glyph_centered(fb, fonts::kValue60, x, w, baseline, static_cast<uint8_t>(p.sep));
    } else {
      const bool mark = p.field == marked;
      if (mark) fb.fill(mark_rect(x, w, baseline), true);
      char buf[8];
      snprintf(buf, sizeof buf, "%0*d", p.digits, values[p.field]);
      for (int i = 0; i < p.digits && buf[i]; ++i) {
        draw_glyph_centered(fb, fonts::kValue60, x + i * (cw + kDigitGap), cw, baseline,
                            static_cast<uint8_t>(buf[i]), !mark);
      }
    }
    x += w;
  }
}

// Upward arrow with its tip on the top edge at x.
void draw_arrow(Framebuffer& fb, int x) {
  for (int k = 0; k < kArrowH; ++k) fb.fill(Rect{x - k, k, 2 * k + 1, 1}, true);
}

void draw_buttons_and_title(Framebuffer& fb, const char* title, const ButtonLabels& b) {
  draw_arrow(fb, kKeyButtonX);
  draw_arrow(fb, kBootButtonX);
  const int key_right = kKeyButtonX - kButtonLabelGap;
  if (b.key != nullptr) {
    draw_text(fb, fonts::kText20, key_right - text_width(fonts::kText20, b.key), kLabelBaseline1,
              b.key);
  }
  if (b.key_hold != nullptr) {
    draw_text(fb, fonts::kText20, key_right - text_width(fonts::kText20, b.key_hold),
              kLabelBaseline2, b.key_hold);
  }
  if (b.boot != nullptr) {
    draw_text(fb, fonts::kText20, kBootButtonX + kButtonLabelGap, kLabelBaseline1, b.boot);
  }
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kTitleBaseline, title);
}

void draw_editor_buttons(Framebuffer& fb, int marked) {
  for (int b = 1; b <= 2; ++b) {
    const Rect r = editor_button_rect(b);
    const bool mark = b == marked;
    if (mark) {
      fb.fill(r, true);
    } else {
      fb.fill(Rect{r.x, r.y, r.w, 2}, true);
      fb.fill(Rect{r.x, r.y + r.h - 2, r.w, 2}, true);
      fb.fill(Rect{r.x, r.y, 2, r.h}, true);
      fb.fill(Rect{r.x + r.w - 2, r.y, 2, r.h}, true);
    }
    draw_text_centered(fb, fonts::kText30, kButtonCx[b - 1], kButtonsBaseline - 3,
                       kButtonText[b - 1], !mark);
  }
}

}  // namespace

Rect editor_button_rect(int button) {
  if (button != kEditorSave && button != kEditorCancel) return Rect{};
  const int top = kButtonsBaseline - fonts::kText30.ascent - 10;
  return Rect{kButtonCx[button - 1] - kButtonW / 2, top, kButtonW, kButtonH};
}

size_t list_first_row(size_t count, size_t selected) {
  if (count <= kListRows || selected < kListRows / 2) return 0;
  const size_t first = selected - kListRows / 2;
  return std::min(first, count - kListRows);
}

void render_list(Framebuffer& fb, const char* title, const char* const* items, size_t count,
                 size_t selected, const ButtonLabels& buttons) {
  fb.clear();
  draw_buttons_and_title(fb, title, buttons);
  const size_t first = list_first_row(count, selected);
  for (size_t row = 0; row < kListRows && first + row < count; ++row) {
    const size_t i = first + row;
    const int top = kListTop + static_cast<int>(row) * kRowH;
    const bool mark = i == selected;
    if (mark) fb.fill(Rect{kRowMargin, top, Framebuffer::kWidth - 2 * kRowMargin, kRowH - 4}, true);
    draw_text(fb, fonts::kText30, kRowTextX, top + kRowTextDy, items[i], !mark);
  }
  // More items above or below: a small triangle at the right end of the first
  // or last row. The marked row is never there while scrolled (it stays in the middle).
  const int ax = Framebuffer::kWidth - kRowMargin - 22;
  if (first > 0) {
    for (int k = 0; k < 8; ++k) fb.fill(Rect{ax + 7 - k, kListTop + 12 + k, 2 * k + 1, 1}, true);
  }
  if (first + kListRows < count) {
    const int y = kListTop + static_cast<int>(kListRows - 1) * kRowH + 12;
    for (int k = 0; k < 8; ++k) fb.fill(Rect{ax + 7 - k, y + 7 - k, 2 * k + 1, 1}, true);
  }
}

Rect datetime_field_rect(int field) {
  int x = 0, w = 0;
  if (field_x(kDateRow, field, x, w)) return mark_rect(x, w, kDateBaseline);
  if (field_x(kTimeRow, field, x, w)) return mark_rect(x, w, kTimeBaseline);
  return Rect{};
}

void render_datetime(Framebuffer& fb, const char* title, const DateTimeView& v,
                     const ButtonLabels& buttons) {
  fb.clear();
  draw_buttons_and_title(fb, title, buttons);
  const int values[] = {v.year, v.month, v.day, v.hour, v.minute};
  draw_row(fb, kDateRow, values, v.field, kDateBaseline);
  draw_row(fb, kTimeRow, values, v.field, kTimeBaseline);
  if (v.month >= 1 && v.month <= 12) {
    draw_text_centered(fb, fonts::kText20, Framebuffer::kWidth / 2, kWeekdayBaseline,
                       kWeekdays[weekday(v.year, v.month, v.day)]);
  }
  draw_editor_buttons(fb, v.button);
}

namespace {

int day_x(int d) {
  constexpr int kRowW = 7 * kDayW + 6 * kDayGap;
  return (Framebuffer::kWidth - kRowW) / 2 + d * (kDayW + kDayGap);
}

// Black mark around a centred line of 30 px text.
Rect text30_rect(const char* text, int baseline) {
  const int w = text_width(fonts::kText30, text);
  return Rect{(Framebuffer::kWidth - w) / 2 - kPresetPad,
              baseline - fonts::kText30.ascent - kPresetPad, w + 2 * kPresetPad,
              fonts::kText30.ascent + 2 * kPresetPad + 7};  // + room for descenders
}

Rect preset_rect(const char* days) { return text30_rect(days, kPresetBaseline); }

// The volume field: always two digits wide, so the mark does not jump at 10.
Rect volume_rect() {
  return mark_rect((Framebuffer::kWidth - digits_w(2)) / 2, digits_w(2), kVolumeBaseline);
}

int level_x(int i) {
  constexpr int kRowW = 10 * kLevelW + 9 * kLevelGap;
  return (Framebuffer::kWidth - kRowW) / 2 + i * (kLevelW + kLevelGap);
}

void draw_sound_page(Framebuffer& fb, const AlarmEditView& v) {
  const Rect vr = volume_rect();
  const bool vol_marked = v.field == kAlarmFieldVolume;
  if (vol_marked) fb.fill(vr, true);
  char buf[4];
  snprintf(buf, sizeof buf, "%d", v.volume);
  const int n = static_cast<int>(strlen(buf));
  const int cw = cell_w();
  int x = (Framebuffer::kWidth - digits_w(n)) / 2;
  for (int i = 0; i < n; ++i, x += cw + kDigitGap) {
    draw_glyph_centered(fb, fonts::kValue60, x, cw, kVolumeBaseline, static_cast<uint8_t>(buf[i]),
                        !vol_marked);
  }
  const char* label = "Volume";
  draw_text(fb, fonts::kText20, vr.x - kVolumeLabelGap - text_width(fonts::kText20, label),
            kVolumeBaseline, label);
  for (int i = 0; i < 10; ++i) {
    const int lx = level_x(i);
    if (i < v.volume) {
      fb.fill(Rect{lx, kLevelTop, kLevelW, kLevelH}, true);
    } else {
      fb.fill(Rect{lx, kLevelTop, kLevelW, kDayBorder}, true);
      fb.fill(Rect{lx, kLevelTop + kLevelH - kDayBorder, kLevelW, kDayBorder}, true);
      fb.fill(Rect{lx, kLevelTop, kDayBorder, kLevelH}, true);
      fb.fill(Rect{lx + kLevelW - kDayBorder, kLevelTop, kDayBorder, kLevelH}, true);
    }
  }
  const char* ramp = v.ramp != nullptr ? v.ramp : "";
  const bool ramp_marked = v.field == kAlarmFieldRamp;
  if (ramp_marked) fb.fill(text30_rect(ramp, kRampBaseline), true);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kRampBaseline, ramp,
                     !ramp_marked);
}

}  // namespace

Rect alarm_field_rect(const AlarmEditView& v, int field) {
  int x = 0, w = 0;
  if (field <= 1 && field_x(kAlarmTimeRow, field, x, w)) {
    return mark_rect(x, w, kAlarmTimeBaseline);
  }
  if (field == 2) return preset_rect(v.days);
  if (field >= 3 && field <= 9) {
    const int top = kDayTop;
    return Rect{day_x(field - 3), top, kDayW, kDayH + kDayBarGap + kDayBarH};
  }
  if (field == kAlarmFieldVolume) return volume_rect();
  if (field == kAlarmFieldRamp) return text30_rect(v.ramp != nullptr ? v.ramp : "", kRampBaseline);
  return Rect{};
}

bool alarm_sound_page(const AlarmEditView& v) {
  return v.field >= kAlarmFieldVolume || v.field < 0;
}

void render_alarm_edit(Framebuffer& fb, const char* title, const AlarmEditView& v,
                       const ButtonLabels& buttons) {
  fb.clear();
  draw_buttons_and_title(fb, title, buttons);
  if (alarm_sound_page(v)) {
    draw_sound_page(fb, v);
    draw_editor_buttons(fb, v.button);
    return;
  }
  const int values[] = {v.hour, v.minute};
  draw_row(fb, kAlarmTimeRow, values, v.field, kAlarmTimeBaseline);

  const bool preset_marked = v.field == 2;
  if (preset_marked) fb.fill(preset_rect(v.days), true);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kPresetBaseline, v.days,
                     !preset_marked);

  for (int d = 0; d < 7; ++d) {
    const int x = day_x(d);
    const bool on = (v.mask >> d) & 1;
    if (on) {
      fb.fill(Rect{x, kDayTop, kDayW, kDayH}, true);
    } else {
      fb.fill(Rect{x, kDayTop, kDayW, kDayBorder}, true);
      fb.fill(Rect{x, kDayTop + kDayH - kDayBorder, kDayW, kDayBorder}, true);
      fb.fill(Rect{x, kDayTop, kDayBorder, kDayH}, true);
      fb.fill(Rect{x + kDayW - kDayBorder, kDayTop, kDayBorder, kDayH}, true);
    }
    draw_text_centered(fb, fonts::kText20, x + kDayW / 2, kDayTop + kDayTextDy, kDay2[d], !on);
    if (v.field == 3 + d) {
      fb.fill(Rect{x, kDayTop + kDayH + kDayBarGap, kDayW, kDayBarH}, true);
    }
  }
  draw_editor_buttons(fb, v.button);
}

}  // namespace inkwake
