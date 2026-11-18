// SPDX-License-Identifier: GPL-3.0-only
#include "clock_face.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "font.h"
#include "fonts/clock_digits.h"
#include "fonts/text30.h"

namespace inkwake {

namespace {

constexpr int kDateBaseline = 50;
constexpr int kTimeBaseline = 192;
constexpr int kTempBaseline = 266;
constexpr int kDigitGap = 6;  // between the two digits of a pair
constexpr int kMargin = 2;    // around field regions

const char* const kWeekdays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};
const char* const kMonths[] = {"January",   "February", "March",    "April",
                               "May",       "June",     "July",     "August",
                               "September", "October",  "November", "December"};

// Vertical ink extent of a font relative to the baseline: [above, below).
struct Extent {
  int top;     // negative, above the baseline
  int bottom;  // below the baseline
};

Extent font_extent(const Font& f) {
  Extent e{0, 0};
  for (uint32_t c = f.first; c <= f.last; ++c) {
    const Glyph& g = f.glyphs[c - f.first];
    if (g.width == 0) continue;
    e.top = std::min<int>(e.top, g.top);
    e.bottom = std::max<int>(e.bottom, g.top + g.height);
  }
  return e;
}

int digit_cell_width() {
  const Font& f = fonts::kClockDigits;
  int w = 0;
  for (char c = '0'; c <= '9'; ++c) w = std::max<int>(w, f.glyph(c)->width);
  return w;
}

struct TimeLayout {
  int cell_w;
  int colon_w;
  int x[5];  // HourTens, HourOnes, colon, MinuteTens, MinuteOnes
};

TimeLayout time_layout() {
  TimeLayout l{};
  l.cell_w = digit_cell_width();
  l.colon_w = fonts::kClockDigits.glyph(':')->advance;
  const int total = 4 * l.cell_w + 2 * kDigitGap + l.colon_w;
  int x = (Framebuffer::kWidth - total) / 2;
  l.x[0] = x;
  l.x[1] = x += l.cell_w + kDigitGap;
  l.x[2] = x += l.cell_w;
  l.x[3] = x += l.colon_w;
  l.x[4] = x + l.cell_w + kDigitGap;
  return l;
}

Rect text_band(const Font& f, int baseline) {
  const Extent e = font_extent(f);
  return Rect{0, baseline + e.top - kMargin, Framebuffer::kWidth, e.bottom - e.top + 2 * kMargin};
}

char digit(FaceState s, FaceRegion r) {
  if (!s.time_set) return '-';
  switch (r) {
    case FaceRegion::HourTens:
      return static_cast<char>('0' + s.hour / 10);
    case FaceRegion::HourOnes:
      return static_cast<char>('0' + s.hour % 10);
    case FaceRegion::MinuteTens:
      return static_cast<char>('0' + s.minute / 10);
    default:
      return static_cast<char>('0' + s.minute % 10);
  }
}

bool same_date(const FaceState& a, const FaceState& b) {
  if (!a.time_set || !b.time_set) return a.time_set == b.time_set;
  return a.weekday == b.weekday && a.day == b.day && a.month == b.month;
}

}  // namespace

Rect face_region(FaceRegion region) {
  switch (region) {
    case FaceRegion::Date:
      return text_band(fonts::kText30, kDateBaseline);
    case FaceRegion::Temp:
      return text_band(fonts::kText30, kTempBaseline);
    case FaceRegion::Count:
      return {};
    default: {
      const TimeLayout l = time_layout();
      const Extent e = font_extent(fonts::kClockDigits);
      const int i = static_cast<int>(region);
      const int slot = i < 2 ? i : i + 1;  // skip the colon
      return Rect{l.x[slot] - kMargin, kTimeBaseline + e.top - kMargin, l.cell_w + 2 * kMargin,
                  e.bottom - e.top + 2 * kMargin};
    }
  }
}

void render_face(Framebuffer& fb, const FaceState& s) {
  fb.clear();
  char buf[40];

  format_date(s, buf, sizeof buf);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kDateBaseline, buf);

  const TimeLayout l = time_layout();
  const FaceRegion digits[] = {FaceRegion::HourTens, FaceRegion::HourOnes, FaceRegion::MinuteTens,
                               FaceRegion::MinuteOnes};
  for (int i = 0; i < 4; ++i) {
    const int slot = i < 2 ? i : i + 1;
    draw_glyph_centered(fb, fonts::kClockDigits, l.x[slot], l.cell_w, kTimeBaseline,
                        static_cast<uint8_t>(digit(s, digits[i])));
  }
  draw_glyph_centered(fb, fonts::kClockDigits, l.x[2], l.colon_w, kTimeBaseline, ':');

  format_temp(s.temp_dc, buf, sizeof buf);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kTempBaseline, buf);
}

size_t face_dirty(const FaceState& before, const FaceState& after, Rect* out) {
  size_t n = 0;
  for (FaceRegion r : {FaceRegion::HourTens, FaceRegion::HourOnes, FaceRegion::MinuteTens,
                       FaceRegion::MinuteOnes}) {
    if (digit(before, r) != digit(after, r)) out[n++] = face_region(r);
  }
  if (!same_date(before, after)) out[n++] = face_region(FaceRegion::Date);
  if (before.temp_dc != after.temp_dc) out[n++] = face_region(FaceRegion::Temp);
  return n;
}

void format_date(const FaceState& s, char* buf, size_t n) {
  if (!s.time_set) {
    snprintf(buf, n, "Time not set");
    return;
  }
  const char* wd = kWeekdays[s.weekday % 7];
  const char* mo = kMonths[(s.month + 11) % 12];  // 1-12, anything else wraps
  snprintf(buf, n, "%s %u %s", wd, static_cast<unsigned>(s.day), mo);
}

void format_temp(int16_t temp_dc, char* buf, size_t n) {
  if (temp_dc == kNoTemp) {
    snprintf(buf, n, "--.- °C");
    return;
  }
  const int v = temp_dc;
  snprintf(buf, n, "%s%d.%d °C", v < 0 ? "-" : "", std::abs(v) / 10, std::abs(v) % 10);
}

}  // namespace inkwake
