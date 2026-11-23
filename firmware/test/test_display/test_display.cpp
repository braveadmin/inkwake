// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "clock_face.h"
#include "font.h"
#include "fonts/text30.h"
#include "framebuffer.h"
#include "panel_status.h"
#include "panel_window.h"
#include "qrcodegen.h"
#include "setup_screen.h"

using namespace inkwake;

void setUp() {}
void tearDown() {}

static Framebuffer g_fb;
static Framebuffer g_fb2;

// ---- framebuffer --------------------------------------------------------------

// Reference values from the spike (verified on the panel): index = (x>>1)*75 + ((299-y)>>2).
static void test_packing_matches_spike_formula() {
  TEST_ASSERT_EQUAL_UINT32(0, Framebuffer::index(0, 299));
  TEST_ASSERT_EQUAL_HEX8(0x80, Framebuffer::mask(0, 299));
  TEST_ASSERT_EQUAL_HEX8(0x40, Framebuffer::mask(1, 299));
  TEST_ASSERT_EQUAL_HEX8(0x20, Framebuffer::mask(0, 298));
  TEST_ASSERT_EQUAL_HEX8(0x01, Framebuffer::mask(1, 296));
  TEST_ASSERT_EQUAL_UINT32(74, Framebuffer::index(0, 0));
  TEST_ASSERT_EQUAL_UINT32(199 * 75 + 74, Framebuffer::index(399, 0));
  TEST_ASSERT_EQUAL_UINT32(Framebuffer::kBytes - 1, Framebuffer::index(399, 0));
}

static void test_set_get_and_clear() {
  g_fb.clear();
  TEST_ASSERT_EQUAL_HEX8(0xFF, g_fb.data()[0]);  // white = bits set
  g_fb.set(10, 20, true);
  TEST_ASSERT_TRUE(g_fb.get(10, 20));
  TEST_ASSERT_FALSE(g_fb.get(11, 20));
  TEST_ASSERT_FALSE(g_fb.get(10, 21));
  g_fb.set(10, 20, false);
  TEST_ASSERT_FALSE(g_fb.get(10, 20));
  g_fb.set(-1, 0, true);  // ignored, no crash
  g_fb.set(400, 300, true);
  TEST_ASSERT_FALSE(g_fb.get(-1, 0));
  g_fb.clear(true);
  TEST_ASSERT_EQUAL_HEX8(0x00, g_fb.data()[100]);
}

static void test_every_pixel_has_its_own_bit() {
  g_fb.clear();
  int count = 0;
  for (int y = 0; y < Framebuffer::kHeight; y += 7) {
    for (int x = 0; x < Framebuffer::kWidth; x += 5) {
      g_fb.set(x, y, true);
      ++count;
    }
  }
  int black = 0;
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) black += g_fb.get(x, y) ? 1 : 0;
  }
  TEST_ASSERT_EQUAL_INT(count, black);
}

static void test_rect_united_and_clipped() {
  const Rect a{10, 10, 5, 5};
  const Rect b{20, 0, 5, 5};
  const Rect u = a.united(b);
  TEST_ASSERT_EQUAL_INT(10, u.x);
  TEST_ASSERT_EQUAL_INT(0, u.y);
  TEST_ASSERT_EQUAL_INT(15, u.w);
  TEST_ASSERT_EQUAL_INT(15, u.h);
  TEST_ASSERT_EQUAL_INT(5, a.united(Rect{}).w);
  const Rect c = Rect{-5, 290, 20, 20}.clipped(400, 300);
  TEST_ASSERT_EQUAL_INT(0, c.x);
  TEST_ASSERT_EQUAL_INT(15, c.w);
  TEST_ASSERT_EQUAL_INT(10, c.h);
  TEST_ASSERT_TRUE((Rect{500, 0, 5, 5}.clipped(400, 300).empty()));
}

// ---- panel window -------------------------------------------------------------

static void test_full_window_matches_init_sequence() {
  const PanelWindow w = PanelWindow::cover({0, 0, 400, 300});
  TEST_ASSERT_TRUE(w == PanelWindow::full());
  TEST_ASSERT_EQUAL_HEX8(0x12, w.col_start_addr());
  TEST_ASSERT_EQUAL_HEX8(0x2A, w.col_end_addr());
  TEST_ASSERT_EQUAL_HEX8(0x00, w.row_first);
  TEST_ASSERT_EQUAL_HEX8(0xC7, w.row_last);
  TEST_ASSERT_EQUAL_UINT32(Framebuffer::kBytes, w.bytes());
  TEST_ASSERT_EQUAL_UINT32(0, w.row_offset(0));
}

static void test_small_window() {
  // x 10..13 -> panel rows 5..6; y 0..11 -> inverted 288..299 -> bytes 72..74 -> column 24.
  const PanelWindow w = PanelWindow::cover({10, 0, 4, 12});
  TEST_ASSERT_EQUAL_UINT8(5, w.row_first);
  TEST_ASSERT_EQUAL_UINT8(6, w.row_last);
  TEST_ASSERT_EQUAL_UINT8(24, w.col_first);
  TEST_ASSERT_EQUAL_UINT8(24, w.col_last);
  TEST_ASSERT_EQUAL_UINT32(3, w.row_len());
  TEST_ASSERT_EQUAL_UINT32(6, w.bytes());
  TEST_ASSERT_EQUAL_UINT32(5 * 75 + 72, w.row_offset(5));
  // Top of the screen = last buffer column = first column address (mirrored).
  TEST_ASSERT_EQUAL_HEX8(0x12, w.col_start_addr());
  TEST_ASSERT_EQUAL_HEX8(0x12, w.col_end_addr());
}

// The `box` check on the panel: a 36x36 square at the top-left. Before the
// mirror fix it was sent to 0x28..0x2A and showed at the bottom-left.
static void test_column_addresses_are_mirrored() {
  const PanelWindow top = PanelWindow::cover({0, 0, 36, 36});
  TEST_ASSERT_EQUAL_UINT8(22, top.col_first);
  TEST_ASSERT_EQUAL_UINT8(24, top.col_last);
  TEST_ASSERT_EQUAL_HEX8(0x12, top.col_start_addr());
  TEST_ASSERT_EQUAL_HEX8(0x14, top.col_end_addr());

  const PanelWindow bottom = PanelWindow::cover({0, 288, 10, 12});
  TEST_ASSERT_EQUAL_UINT8(0, bottom.col_first);
  TEST_ASSERT_EQUAL_HEX8(0x2A, bottom.col_start_addr());
  TEST_ASSERT_EQUAL_HEX8(0x2A, bottom.col_end_addr());
}

static void test_full_columns_mode_spans_height() {
  const PanelWindow w = PanelWindow::cover({100, 100, 10, 10}, true);
  TEST_ASSERT_EQUAL_UINT8(50, w.row_first);
  TEST_ASSERT_EQUAL_UINT8(54, w.row_last);
  TEST_ASSERT_EQUAL_UINT8(0, w.col_first);
  TEST_ASSERT_EQUAL_UINT8(24, w.col_last);
  TEST_ASSERT_EQUAL_UINT32(75, w.row_len());
}

// Every pixel of a rect must sit in a byte the window sends.
static bool window_covers(const PanelWindow& w, const Rect& r) {
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      const size_t i = Framebuffer::index(x, y);
      const int row = static_cast<int>(i / Framebuffer::kRowBytes);
      if (row < w.row_first || row > w.row_last) return false;
      const size_t off = w.row_offset(row);
      if (i < off || i >= off + w.row_len()) return false;
    }
  }
  return true;
}

static void test_window_covers_rect() {
  uint32_t seed = 12345;
  auto rnd = [&seed](int n) {
    seed = seed * 1103515245u + 12345u;
    return static_cast<int>((seed >> 8) % static_cast<uint32_t>(n));
  };
  for (int i = 0; i < 300; ++i) {
    const Rect r{rnd(400), rnd(300), 1 + rnd(120), 1 + rnd(120)};
    const Rect c = r.clipped(400, 300);
    TEST_ASSERT_TRUE(window_covers(PanelWindow::cover(r), c));
    TEST_ASSERT_TRUE(window_covers(PanelWindow::cover(r, true), c));
  }
}

// ---- font ---------------------------------------------------------------------

static void test_text_width_and_drawing() {
  const Font& f = fonts::kText30;
  const Glyph* a = f.glyph('A');
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_INT(a->advance * 2, text_width(f, "AA"));
  TEST_ASSERT_NULL(f.glyph(0x2603));  // snowman not in the font
  TEST_ASSERT_EQUAL_INT(text_width(f, "AA"), text_width(f,
                                                        "A\xE2\x98\x83"
                                                        "A"));
  TEST_ASSERT_GREATER_THAN(0, text_width(f, "\xC2\xB0"));  // degree sign is present

  g_fb.clear();
  const int end = draw_text(g_fb, f, 10, 50, "H");
  TEST_ASSERT_EQUAL_INT(10 + f.glyph('H')->advance, end);
  int black = 0;
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      if (!g_fb.get(x, y)) continue;
      ++black;
      TEST_ASSERT_TRUE(y < 50 && y >= 50 - f.ascent - 1);  // H sits on the baseline
      TEST_ASSERT_TRUE(x >= 10 && x < end);
    }
  }
  TEST_ASSERT_GREATER_THAN(50, black);
}

static void test_truncated_utf8_does_not_overrun() {
  const char s[] = {'A', static_cast<char>(0xE2), '\0', 'Z', '\0'};
  TEST_ASSERT_EQUAL_INT(fonts::kText30.glyph('A')->advance, text_width(fonts::kText30, s));
}

// ---- clock face ---------------------------------------------------------------

static void test_format() {
  char buf[40];
  FaceState s{7, 5, 2, 18, 11, 215};
  format_date(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("Wednesday 18 November", buf);
  format_temp(215, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING(
      "21.5 \xC2\xB0"
      "C",
      buf);
  format_temp(-5, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING(
      "-0.5 \xC2\xB0"
      "C",
      buf);
  format_temp(-123, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING(
      "-12.3 \xC2\xB0"
      "C",
      buf);
  format_temp(kNoTemp, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING(
      "--.- \xC2\xB0"
      "C",
      buf);
}

static void test_regions_inside_screen_and_disjoint() {
  for (size_t i = 0; i < kFaceRegions; ++i) {
    const Rect a = face_region(static_cast<FaceRegion>(i));
    TEST_ASSERT_FALSE(a.empty());
    TEST_ASSERT_TRUE(a.x >= 0 && a.y >= 0 && a.x + a.w <= 400 && a.y + a.h <= 300);
    for (size_t j = i + 1; j < kFaceRegions; ++j) {
      const Rect b = face_region(static_cast<FaceRegion>(j));
      const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
      TEST_ASSERT_FALSE(overlap);
    }
  }
}

static void test_dirty_regions() {
  Rect out[kFaceRegions];
  const FaceState a{7, 5, 2, 18, 11, 215};
  TEST_ASSERT_EQUAL_UINT32(0, face_dirty(a, a, out));

  FaceState b = a;
  b.minute = 6;
  TEST_ASSERT_EQUAL_UINT32(1, face_dirty(a, b, out));
  TEST_ASSERT_EQUAL_INT(face_region(FaceRegion::MinuteOnes).x, out[0].x);

  const FaceState c{9, 59, 2, 18, 11, 215};
  const FaceState d{10, 0, 2, 18, 11, 215};
  TEST_ASSERT_EQUAL_UINT32(4, face_dirty(c, d, out));

  const FaceState e{23, 59, 2, 18, 11, 215};
  const FaceState f{0, 0, 3, 19, 11, 220};
  TEST_ASSERT_EQUAL_UINT32(6, face_dirty(e, f, out));
}

// Pixels that differ between two rendered faces must all lie in the dirty
// regions, or a partial refresh would leave stale pixels on the panel.
static void check_diff_covered(const FaceState& a, const FaceState& b) {
  Rect dirty[kFaceRegions];
  const size_t n = face_dirty(a, b, dirty);
  render_face(g_fb, a);
  render_face(g_fb2, b);
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      if (g_fb.get(x, y) == g_fb2.get(x, y)) continue;
      bool covered = false;
      for (size_t i = 0; i < n; ++i) covered = covered || dirty[i].contains(x, y);
      if (!covered) {
        char msg[64];
        snprintf(msg, sizeof msg, "stale pixel at %d,%d", x, y);
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

static void test_dirty_regions_cover_pixel_diff() {
  // Every minute of a day, plus date and temperature changes with long strings.
  FaceState prev{0, 0, 0, 1, 1, kNoTemp};
  for (int m = 1; m < 24 * 60; m += 7) {
    FaceState next = prev;
    next.hour = static_cast<uint8_t>(m / 60);
    next.minute = static_cast<uint8_t>(m % 60);
    check_diff_covered(prev, next);
    prev = next;
  }
  check_diff_covered(FaceState{23, 59, 6, 30, 9, kNoTemp}, FaceState{0, 0, 2, 1, 10, -123});
  check_diff_covered(FaceState{11, 11, 4, 11, 5, 111}, FaceState{18, 48, 2, 28, 12, 888});
}

static void test_time_not_set() {
  FaceState unset{7, 5, 2, 18, 11, 215};
  unset.time_set = false;
  char buf[40];
  format_date(unset, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("Time not set", buf);

  // Losing the time redraws the four digits and the date, not the temperature.
  const FaceState set{7, 5, 2, 18, 11, 215};
  Rect out[kFaceRegions];
  TEST_ASSERT_EQUAL_UINT32(5, face_dirty(set, unset, out));
  TEST_ASSERT_EQUAL_UINT32(5, face_dirty(unset, set, out));
  // While unset, the hidden time ticking on changes nothing.
  FaceState later = unset;
  later.minute = 6;
  later.day = 19;
  TEST_ASSERT_EQUAL_UINT32(0, face_dirty(unset, later, out));

  check_diff_covered(set, unset);
  check_diff_covered(unset, set);
  check_diff_covered(unset, later);
}

static void test_wifi_mark() {
  const FaceState ok{7, 5, 2, 18, 11, 215};
  FaceState failed = ok;
  failed.wifi_failed = true;
  Rect out[kFaceRegions];
  TEST_ASSERT_EQUAL_UINT32(1, face_dirty(ok, failed, out));
  const Rect mark = face_region(FaceRegion::WifiMark);
  TEST_ASSERT_EQUAL_INT(mark.x, out[0].x);
  check_diff_covered(ok, failed);
  check_diff_covered(failed, ok);
  // The mark draws something, and only inside its region.
  render_face(g_fb, failed);
  render_face(g_fb2, ok);
  int changed = 0;
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      if (g_fb.get(x, y) != g_fb2.get(x, y)) {
        ++changed;
        TEST_ASSERT_TRUE(mark.contains(x, y));
      }
    }
  }
  TEST_ASSERT_TRUE(changed > 30);
}

static void test_battery_mark() {
  const FaceState ok{7, 5, 2, 18, 11, 215};
  const Rect mark = face_region(FaceRegion::BatteryMark);
  FaceState low = ok, critical = ok;
  low.battery = 1;
  critical.battery = 2;
  Rect out[kFaceRegions];
  TEST_ASSERT_EQUAL_UINT32(1, face_dirty(ok, low, out));
  TEST_ASSERT_EQUAL_INT(mark.x, out[0].x);
  TEST_ASSERT_EQUAL_UINT32(1, face_dirty(low, critical, out));
  check_diff_covered(ok, low);
  check_diff_covered(low, critical);
  check_diff_covered(critical, ok);
  // Each level draws something different, only inside the region.
  for (const FaceState* s : {&low, &critical}) {
    render_face(g_fb, *s);
    render_face(g_fb2, ok);
    int changed = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
      for (int x = 0; x < Framebuffer::kWidth; ++x) {
        if (g_fb.get(x, y) != g_fb2.get(x, y)) {
          ++changed;
          TEST_ASSERT_TRUE(mark.contains(x, y));
        }
      }
    }
    TEST_ASSERT_TRUE(changed > 30);
  }
  // Clear of the Wi-Fi mark and the date line.
  const Rect wifi = face_region(FaceRegion::WifiMark);
  const Rect date = face_region(FaceRegion::Date);
  TEST_ASSERT_TRUE(mark.x + mark.w <= wifi.x);
  TEST_ASSERT_TRUE(mark.y + mark.h <= date.y);
}

static void test_setup_screen() {
  // Longest footer: "or join inkwake-WWWW" must fit the panel width.
  TEST_ASSERT_TRUE(text_width(fonts::kText30, "or join inkwake-WWWW") <= Framebuffer::kWidth - 8);
  TEST_ASSERT_TRUE(text_width(fonts::kText30, kSetupTitle) <= Framebuffer::kWidth - 8);
  TEST_ASSERT_TRUE(text_width(fonts::kText30, kRetryTitle) <= Framebuffer::kWidth - 8);
  // Version 2 and 3 codes: at least 6 px per module, inside the panel.
  for (int modules : {25, 29}) {
    const QrPlacement p = setup_qr_placement(modules);
    TEST_ASSERT_TRUE(p.scale >= 6);
    TEST_ASSERT_TRUE(p.x >= 0 && p.x + modules * p.scale <= Framebuffer::kWidth);
    TEST_ASSERT_TRUE(p.y >= 40 && p.y + modules * p.scale <= 262);
  }
  // Each module lands where expected.
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
  TEST_ASSERT_TRUE(qrcodegen_encodeText("WIFI:T:nopass;S:inkwake-A10B;;", tmp, qr,
                                        qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO, true));
  render_setup(g_fb, qr, "inkwake-A10B");
  const int n = qrcodegen_getSize(qr);
  const QrPlacement p = setup_qr_placement(n);
  for (int my = 0; my < n; ++my) {
    for (int mx = 0; mx < n; ++mx) {
      const int cx = p.x + mx * p.scale + p.scale / 2;
      const int cy = p.y + my * p.scale + p.scale / 2;
      TEST_ASSERT_EQUAL(qrcodegen_getModule(qr, mx, my), g_fb.get(cx, cy));
    }
  }
}

void test_panel_status() {
  using namespace panel_status;
  TEST_ASSERT_EQUAL_HEX8(0xDC, kParked);
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Ok), static_cast<int>(classify(kParked)));
  // High power mode during a write is fine too.
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Ok), static_cast<int>(classify(kParked & ~kLowPower)));
  // Booster off, asleep, display off, or the post-reset default: needs recovery.
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Bad), static_cast<int>(classify(kParked & ~kBooster)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Bad),
                    static_cast<int>(classify(kParked & ~kSleepOut)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Bad),
                    static_cast<int>(classify(kParked & ~kDisplayOn)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::Bad), static_cast<int>(classify(0x08)));
  // Undriven line (pull-up), stuck low, or a low bit set: not the controller.
  TEST_ASSERT_EQUAL(static_cast<int>(Health::NoAnswer), static_cast<int>(classify(0xFF)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::NoAnswer), static_cast<int>(classify(0x00)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::NoAnswer), static_cast<int>(classify(0xDD)));
  TEST_ASSERT_EQUAL(static_cast<int>(Health::NoAnswer), static_cast<int>(classify(0xDE)));

  char text[80];
  describe(kParked, text, sizeof text);
  TEST_ASSERT_EQUAL_STRING("booster on, low power, sleep out, normal, display on", text);
  describe(0x08, text, sizeof text);
  TEST_ASSERT_EQUAL_STRING("booster OFF, high power, SLEEP IN, normal, display OFF", text);
  describe(0xFF, text, sizeof text);
  TEST_ASSERT_EQUAL_STRING("no answer", text);
  char tiny[5];
  describe(kParked, tiny, sizeof tiny);
  TEST_ASSERT_EQUAL_STRING("boos", tiny);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_packing_matches_spike_formula);
  RUN_TEST(test_set_get_and_clear);
  RUN_TEST(test_every_pixel_has_its_own_bit);
  RUN_TEST(test_rect_united_and_clipped);
  RUN_TEST(test_full_window_matches_init_sequence);
  RUN_TEST(test_small_window);
  RUN_TEST(test_column_addresses_are_mirrored);
  RUN_TEST(test_full_columns_mode_spans_height);
  RUN_TEST(test_window_covers_rect);
  RUN_TEST(test_text_width_and_drawing);
  RUN_TEST(test_truncated_utf8_does_not_overrun);
  RUN_TEST(test_format);
  RUN_TEST(test_regions_inside_screen_and_disjoint);
  RUN_TEST(test_dirty_regions);
  RUN_TEST(test_dirty_regions_cover_pixel_diff);
  RUN_TEST(test_time_not_set);
  RUN_TEST(test_wifi_mark);
  RUN_TEST(test_battery_mark);
  RUN_TEST(test_setup_screen);
  RUN_TEST(test_panel_status);
  return UNITY_END();
}
