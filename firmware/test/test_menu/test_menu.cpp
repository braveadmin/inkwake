// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstring>

#include "button.h"
#include "datetime.h"
#include "font.h"
#include "fonts/text20.h"
#include "fonts/text30.h"
#include "framebuffer.h"
#include "menu.h"
#include "menu_screen.h"
#include "tz.h"
#include "tz_presets.h"

using namespace inkwake;
using namespace inkwake::menu;

void setUp() {}
void tearDown() {}

// ---- buttons -------------------------------------------------------------------

static const ButtonConfig kKey{kBackHoldMs, 0, 0};
static const ButtonConfig kBoot{0, kRepeatDelayMs, kRepeatMs};

// Feeds `down` from `from` to `to` (exclusive) in 10 ms steps; returns the
// gestures seen other than None, in order, as letters (P T H R r).
static const char* run(Button& b, bool down, uint32_t from, uint32_t to) {
  static char out[64];
  size_t n = 0;
  for (uint32_t t = from; t < to && n + 1 < sizeof out; t += 10) {
    const Gesture g = b.update(down, t);
    const char c = g == Gesture::Press     ? 'P'
                   : g == Gesture::Tap     ? 'T'
                   : g == Gesture::Hold    ? 'H'
                   : g == Gesture::Release ? 'R'
                   : g == Gesture::Repeat  ? 'r'
                                           : 0;
    if (c) out[n++] = c;
  }
  out[n] = '\0';
  return out;
}

static void test_tap() {
  Button b(kKey);
  TEST_ASSERT_EQUAL_STRING("P", run(b, true, 1000, 1200));
  TEST_ASSERT_EQUAL_STRING("T", run(b, false, 1200, 1300));
}

static void test_bounce_is_ignored() {
  Button b(kKey);
  // Contacts chatter for 20 ms: shorter than the debounce, no press.
  TEST_ASSERT_EQUAL_STRING("", run(b, true, 0, 20));
  TEST_ASSERT_EQUAL_STRING("", run(b, false, 20, 200));
  TEST_ASSERT_FALSE(b.down());
}

static void test_hold_then_release() {
  Button b(kKey);
  TEST_ASSERT_EQUAL_STRING("PH", run(b, true, 0, 1500));
  TEST_ASSERT_EQUAL_STRING("R", run(b, false, 1500, 1600));
}

static void test_hold_reported_once() {
  Button b(kKey);
  TEST_ASSERT_EQUAL_STRING("PH", run(b, true, 0, 6000));
}

static void test_repeat() {
  Button b(kBoot);
  // Press at 0 (seen at 30 after the debounce), repeats at 500, 650, 800, 950.
  TEST_ASSERT_EQUAL_STRING("Prrrr", run(b, true, 0, 960));
  TEST_ASSERT_EQUAL_STRING("T", run(b, false, 960, 1100));
}

static void test_repeat_does_not_burst_after_a_stall() {
  Button b(kBoot);
  run(b, true, 0, 100);
  TEST_ASSERT_EQUAL(Gesture::Repeat, b.update(true, 5000));  // caller was busy 4.9 s
  TEST_ASSERT_EQUAL(Gesture::None, b.update(true, 5010));
  TEST_ASSERT_EQUAL(Gesture::Repeat, b.update(true, 5150));
}

static void test_pressed_since_wake() {
  Button b(ButtonConfig{kMenuHoldMs, 0, 0});
  b.pressed_since(0);  // the press that woke the board
  TEST_ASSERT_TRUE(b.down());
  TEST_ASSERT_EQUAL_UINT32(300, b.held_ms(300));
  TEST_ASSERT_EQUAL_STRING("H", run(b, true, 300, 2500));
  TEST_ASSERT_EQUAL_STRING("R", run(b, false, 2500, 2600));
}

static void test_pressed_since_then_quick_release_is_tap() {
  Button b(kKey);
  b.pressed_since(0);
  TEST_ASSERT_EQUAL_STRING("T", run(b, false, 200, 300));
}

static void test_ignore_until_release() {
  Button b(kKey);
  run(b, true, 0, 100);
  b.ignore_until_release();
  TEST_ASSERT_EQUAL_STRING("", run(b, true, 100, 3000));
  TEST_ASSERT_EQUAL_STRING("", run(b, false, 3000, 3100));
  TEST_ASSERT_EQUAL_STRING("P", run(b, true, 3100, 3200));  // the next press counts
}

static void test_no_hold_while_release_debounces() {
  Button b(kKey);
  b.pressed_since(0);
  // Released long ago, seen only now (the caller was busy): a Tap, not a Hold.
  TEST_ASSERT_EQUAL(Gesture::None, b.update(false, 5000));
  TEST_ASSERT_EQUAL(Gesture::Tap, b.update(false, 5040));
}

static void test_millis_wrap() {
  Button b(kKey);
  const uint32_t start = 0xFFFFFF00u;
  TEST_ASSERT_EQUAL(Gesture::None, b.update(true, start));
  TEST_ASSERT_EQUAL(Gesture::Press, b.update(true, start + 40));
  TEST_ASSERT_EQUAL(Gesture::Hold, b.update(true, start + 1000));  // past the wrap
}

// ---- date & time ------------------------------------------------------------------

static void test_leap_years() {
  TEST_ASSERT_TRUE(is_leap(2028));
  TEST_ASSERT_FALSE(is_leap(2027));
  TEST_ASSERT_FALSE(is_leap(2100));
  TEST_ASSERT_TRUE(is_leap(2000));
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2028, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2027, 2));
  TEST_ASSERT_EQUAL_UINT8(30, days_in_month(2027, 4));
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2027, 12));
  TEST_ASSERT_EQUAL_UINT8(0, days_in_month(2027, 13));
}

static void test_fields_wrap() {
  DateTime t{kMaxYear, 12, 31, 23, 59};
  increment(t, Field::Year);
  TEST_ASSERT_EQUAL_INT(kMinYear, t.year);
  increment(t, Field::Month);
  TEST_ASSERT_EQUAL_UINT8(1, t.month);
  TEST_ASSERT_EQUAL_UINT8(31, t.day);  // January has 31
  increment(t, Field::Day);
  TEST_ASSERT_EQUAL_UINT8(1, t.day);
  increment(t, Field::Hour);
  TEST_ASSERT_EQUAL_UINT8(0, t.hour);
  increment(t, Field::Minute);
  TEST_ASSERT_EQUAL_UINT8(0, t.minute);
  increment(t, Field::Minute);
  TEST_ASSERT_EQUAL_UINT8(1, t.minute);
}

static void test_day_wraps_at_month_end() {
  DateTime t{2027, 2, 28, 0, 0};
  increment(t, Field::Day);
  TEST_ASSERT_EQUAL_UINT8(1, t.day);
  t = DateTime{2028, 2, 28, 0, 0};
  increment(t, Field::Day);
  TEST_ASSERT_EQUAL_UINT8(29, t.day);
  increment(t, Field::Day);
  TEST_ASSERT_EQUAL_UINT8(1, t.day);
}

static void test_day_clamped_on_month_and_year() {
  DateTime t{2027, 1, 31, 0, 0};
  increment(t, Field::Month);  // February 2027
  TEST_ASSERT_EQUAL_UINT8(28, t.day);
  t = DateTime{2028, 2, 29, 0, 0};
  increment(t, Field::Year);  // 2029 has no 29 February
  TEST_ASSERT_EQUAL_UINT8(28, t.day);
  t = DateTime{2027, 3, 31, 0, 0};
  increment(t, Field::Month);  // April
  TEST_ASSERT_EQUAL_UINT8(30, t.day);
}

static void test_clamped() {
  const DateTime t = clamped(DateTime{1970, 0, 0, 30, 70});
  TEST_ASSERT_EQUAL_INT(kMinYear, t.year);
  TEST_ASSERT_EQUAL_UINT8(1, t.month);
  TEST_ASSERT_EQUAL_UINT8(1, t.day);
  TEST_ASSERT_EQUAL_UINT8(23, t.hour);
  TEST_ASSERT_EQUAL_UINT8(59, t.minute);
  TEST_ASSERT_EQUAL_INT(kMaxYear, clamped(DateTime{2150, 6, 31, 0, 0}).year);
  TEST_ASSERT_EQUAL_UINT8(30, clamped(DateTime{2027, 6, 31, 0, 0}).day);
}

// ---- timezone presets -----------------------------------------------------------

static void test_presets_parse() {
  for (size_t i = 0; i < kTzPresetCount; ++i) {
    tz::Zone z;
    TEST_ASSERT_TRUE_MESSAGE(tz::parse(kTzPresets[i].posix, z), kTzPresets[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(i), find_tz_preset(kTzPresets[i].posix),
                                  kTzPresets[i].name);
  }
  TEST_ASSERT_EQUAL_STRING(kDefaultTz, kTzPresets[0].posix);
}

static void test_find_preset_by_rules() {
  TEST_ASSERT_EQUAL_INT(1, find_tz_preset("WET0WEST,M3.5.0/1,M10.5.0"));  // Lisbon
  TEST_ASSERT_EQUAL_INT(1, find_tz_preset("GMT0IST,M3.5.0/1,M10.5.0"));   // Dublin
  TEST_ASSERT_EQUAL_INT(8, find_tz_preset("JST-9"));
  TEST_ASSERT_EQUAL_INT(10, find_tz_preset("GMT0"));
  TEST_ASSERT_EQUAL_INT(-1, find_tz_preset("CET-1CEST,M3.4.0,M10.5.0/3"));  // other rule
  TEST_ASSERT_EQUAL_INT(-1, find_tz_preset("IST-5:30"));
  TEST_ASSERT_EQUAL_INT(-1, find_tz_preset("not a zone"));
  TEST_ASSERT_EQUAL_INT(-1, find_tz_preset(nullptr));
}

// ---- menu navigation --------------------------------------------------------------

static Context ctx(int tz_preset = 0, bool wifi = false) {
  Context c;
  c.now = DateTime{2026, 11, 25, 14, 7};
  c.tz_preset = tz_preset;
  c.wifi_saved = wifi;
  return c;
}

static void test_main_list_wraps_and_closes() {
  Menu m;
  m.open(ctx());
  TEST_ASSERT_EQUAL(Screen::Main, m.screen());
  TEST_ASSERT_EQUAL(3, m.count());
  TEST_ASSERT_EQUAL_STRING("Date & time", m.item(0));
  m.handle(Input::Next);
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(2, m.selected());
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(0, m.selected());
  m.handle(Input::Back);
  TEST_ASSERT_TRUE(m.closed());
}

static void test_datetime_edit_and_save() {
  Menu m;
  m.open(ctx());
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(Screen::DateTime, m.screen());
  TEST_ASSERT_EQUAL(Field::Year, m.field());
  TEST_ASSERT_TRUE(DateTime({2026, 11, 25, 14, 7}) == m.value());
  m.handle(Input::Next);  // 2027
  m.handle(Input::Select);
  m.handle(Input::Next);  // December
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));
  TEST_ASSERT_EQUAL(Field::Minute, m.field());
  TEST_ASSERT_EQUAL_STRING("Save", m.key_label());
  TEST_ASSERT_EQUAL_STRING("+1", m.boot_label());
  m.handle(Input::Next);  // 14:08
  TEST_ASSERT_EQUAL(Action::SaveDateTime, m.handle(Input::Select));
  TEST_ASSERT_TRUE(m.closed());
  TEST_ASSERT_TRUE(DateTime({2027, 12, 25, 14, 8}) == m.value());
}

static void test_datetime_back_keeps_nothing() {
  Menu m;
  m.open(ctx());
  m.handle(Input::Select);
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Back));
  TEST_ASSERT_EQUAL(Screen::Main, m.screen());
  TEST_ASSERT_EQUAL(0, m.selected());
  m.handle(Input::Select);  // opens again with the clock's time, not the edit
  TEST_ASSERT_EQUAL_INT(2026, m.value().year);
}

static void test_datetime_alone_closes() {
  Menu m;
  m.open_datetime(ctx());
  TEST_ASSERT_EQUAL(Screen::DateTime, m.screen());
  TEST_ASSERT_EQUAL_STRING("Next", m.key_label());
  TEST_ASSERT_EQUAL_STRING("hold: close", m.key_hold_label());
  m.handle(Input::Back);
  TEST_ASSERT_TRUE(m.closed());
}

static void test_timezone_pick() {
  Menu m;
  m.open(ctx(0));
  m.handle(Input::Next);
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(Screen::Timezone, m.screen());
  TEST_ASSERT_EQUAL(kTzPresetCount, m.count());
  TEST_ASSERT_EQUAL(0, m.selected());  // the current zone
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL_STRING("London / Lisbon", m.item(m.selected()));
  TEST_ASSERT_EQUAL(Action::SaveTimezone, m.handle(Input::Select));
  TEST_ASSERT_EQUAL(1, m.tz_choice());
  TEST_ASSERT_EQUAL(Screen::Main, m.screen());
  TEST_ASSERT_EQUAL(1, m.selected());  // back on "Timezone"
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(1, m.selected());                        // shows the new zone
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));  // same zone: nothing to save
}

static void test_timezone_custom() {
  Menu m;
  m.open(ctx(-1));
  m.handle(Input::Next);
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(kTzPresetCount + 1, m.count());
  TEST_ASSERT_EQUAL_STRING("Custom", m.item(0));
  TEST_ASSERT_EQUAL_STRING("Madrid / Paris / Berlin", m.item(1));
  TEST_ASSERT_EQUAL(0, m.selected());
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));  // keeps the custom zone
  m.handle(Input::Select);
  for (size_t i = 0; i < kTzPresetCount; ++i) m.handle(Input::Next);
  TEST_ASSERT_EQUAL_STRING("UTC", m.item(m.selected()));
  TEST_ASSERT_EQUAL(Action::SaveTimezone, m.handle(Input::Select));
  TEST_ASSERT_EQUAL(10, m.tz_choice());
  m.handle(Input::Select);  // a preset now: no "Custom" entry any more
  TEST_ASSERT_EQUAL(kTzPresetCount, m.count());
}

static void test_wifi_setup() {
  Menu m;
  m.open(ctx(0, false));
  m.handle(Input::Next);
  m.handle(Input::Next);
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(Screen::Wifi, m.screen());
  TEST_ASSERT_EQUAL(1, m.count());  // nothing saved: no "Forget all"
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(0, m.selected());
  TEST_ASSERT_EQUAL(Action::WifiSetup, m.handle(Input::Select));
  TEST_ASSERT_TRUE(m.closed());
}

static void test_wifi_forget_needs_confirmation() {
  Menu m;
  m.open(ctx(0, true));
  m.handle(Input::Next);
  m.handle(Input::Next);
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(2, m.count());
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));
  TEST_ASSERT_EQUAL(Screen::WifiForget, m.screen());
  TEST_ASSERT_EQUAL_STRING("Cancel", m.item(m.selected()));
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));  // Cancel
  TEST_ASSERT_EQUAL(Screen::Wifi, m.screen());
  m.handle(Input::Select);
  m.handle(Input::Back);
  TEST_ASSERT_EQUAL(Screen::Wifi, m.screen());
  m.handle(Input::Select);
  m.handle(Input::Next);
  TEST_ASSERT_EQUAL(Action::WifiForget, m.handle(Input::Select));
  TEST_ASSERT_EQUAL(Screen::Main, m.screen());
  TEST_ASSERT_EQUAL(2, m.selected());
  m.handle(Input::Select);
  TEST_ASSERT_EQUAL(1, m.count());  // nothing left to forget
}

// Every title and item fits the 400 px panel (items next to the scroll arrow);
// KEY's labels fit left of KEY, BOOT's right of BOOT.
static void check_fits(const Menu& m) {
  TEST_ASSERT_TRUE_MESSAGE(text_width(fonts::kText30, m.title()) <= Framebuffer::kWidth - 8,
                           m.title());
  const int key_room = kKeyButtonX - kButtonLabelGap - 4;
  TEST_ASSERT_TRUE_MESSAGE(text_width(fonts::kText20, m.key_label()) <= key_room, m.key_label());
  TEST_ASSERT_TRUE_MESSAGE(text_width(fonts::kText20, m.key_hold_label()) <= key_room,
                           m.key_hold_label());
  TEST_ASSERT_TRUE_MESSAGE(text_width(fonts::kText20, m.boot_label()) <=
                               Framebuffer::kWidth - kBootButtonX - kButtonLabelGap - 4,
                           m.boot_label());
  for (size_t i = 0; i < m.count(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(text_width(fonts::kText30, m.item(i)) <= Framebuffer::kWidth - 24 - 40,
                             m.item(i));
  }
}

static void test_text_fits_screen() {
  Menu m;
  m.open(ctx(-1, true));
  check_fits(m);
  for (int item = 0; item < 3; ++item) {
    m.open(ctx(-1, true));
    for (int k = 0; k < item; ++k) m.handle(Input::Next);
    m.handle(Input::Select);
    check_fits(m);
    for (int f = 0; f < kFieldCount && m.screen() == Screen::DateTime; ++f) {
      check_fits(m);
      if (f + 1 < kFieldCount) m.handle(Input::Select);
    }
    if (m.screen() == Screen::Wifi) {
      m.handle(Input::Next);
      m.handle(Input::Select);
      check_fits(m);
    }
  }
  m.open_datetime(ctx());
  for (int f = 0; f < kFieldCount; ++f) {
    check_fits(m);
    if (f + 1 < kFieldCount) m.handle(Input::Select);
  }
}

static void test_closed_ignores_input() {
  Menu m;
  TEST_ASSERT_TRUE(m.closed());
  TEST_ASSERT_EQUAL(Action::None, m.handle(Input::Select));
  TEST_ASSERT_TRUE(m.closed());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tap);
  RUN_TEST(test_bounce_is_ignored);
  RUN_TEST(test_hold_then_release);
  RUN_TEST(test_hold_reported_once);
  RUN_TEST(test_repeat);
  RUN_TEST(test_repeat_does_not_burst_after_a_stall);
  RUN_TEST(test_pressed_since_wake);
  RUN_TEST(test_pressed_since_then_quick_release_is_tap);
  RUN_TEST(test_ignore_until_release);
  RUN_TEST(test_no_hold_while_release_debounces);
  RUN_TEST(test_millis_wrap);
  RUN_TEST(test_leap_years);
  RUN_TEST(test_fields_wrap);
  RUN_TEST(test_day_wraps_at_month_end);
  RUN_TEST(test_day_clamped_on_month_and_year);
  RUN_TEST(test_clamped);
  RUN_TEST(test_presets_parse);
  RUN_TEST(test_find_preset_by_rules);
  RUN_TEST(test_main_list_wraps_and_closes);
  RUN_TEST(test_datetime_edit_and_save);
  RUN_TEST(test_datetime_back_keeps_nothing);
  RUN_TEST(test_datetime_alone_closes);
  RUN_TEST(test_timezone_pick);
  RUN_TEST(test_timezone_custom);
  RUN_TEST(test_wifi_setup);
  RUN_TEST(test_wifi_forget_needs_confirmation);
  RUN_TEST(test_text_fits_screen);
  RUN_TEST(test_closed_ignores_input);
  return UNITY_END();
}
