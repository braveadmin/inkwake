// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include "alarm.h"

using namespace inkwake;

void setUp() {}
void tearDown() {}

static Alarm make(uint8_t h, uint8_t m, uint8_t days, bool enabled = true) {
  Alarm a;
  a.hour = h;
  a.minute = m;
  a.days = days;
  a.enabled = enabled;
  return a;
}

// Monday 06:00
static const LocalTime kMon0600{0, 6, 0};

void test_later_today() {
  TEST_ASSERT_EQUAL_UINT32(60, *minutes_until(make(7, 0, kWeekdays), kMon0600));
}

void test_fires_this_minute() {
  TEST_ASSERT_EQUAL_UINT32(0, *minutes_until(make(6, 0, kWeekdays), kMon0600));
}

void test_passed_today_rolls_to_tomorrow() {
  TEST_ASSERT_EQUAL_UINT32(24 * 60 - 60, *minutes_until(make(5, 0, kWeekdays), kMon0600));
}

void test_friday_evening_skips_to_monday() {
  const LocalTime fri2200{4, 22, 0};
  // Fri 22:00 -> Mon 07:00 = 2 days + 9 h
  TEST_ASSERT_EQUAL_UINT32(2 * 1440 + 9 * 60, *minutes_until(make(7, 0, kWeekdays), fri2200));
}

void test_single_day_passed_waits_a_week() {
  TEST_ASSERT_EQUAL_UINT32(7 * 1440 - 60, *minutes_until(make(5, 0, MON), kMon0600));
}

void test_one_shot() {
  TEST_ASSERT_EQUAL_UINT32(30, *minutes_until(make(6, 30, kOneShot), kMon0600));
  TEST_ASSERT_EQUAL_UINT32(1440 - 30, *minutes_until(make(5, 30, kOneShot), kMon0600));
}

void test_disabled_never_fires() {
  TEST_ASSERT_FALSE(minutes_until(make(7, 0, kEveryDay, false), kMon0600).has_value());
}

void test_next_alarm_picks_soonest() {
  std::vector<Alarm> alarms{make(9, 30, kWeekend), make(7, 0, kWeekdays),
                            make(6, 45, kEveryDay, false)};
  const auto n = next_alarm(alarms, kMon0600);
  TEST_ASSERT_TRUE(n.has_value());
  TEST_ASSERT_EQUAL_PTR(&alarms[1], n->alarm);
  TEST_ASSERT_EQUAL_UINT32(60, n->in_minutes);
}

void test_next_alarm_none_enabled() {
  std::vector<Alarm> alarms{make(7, 0, kWeekdays, false)};
  TEST_ASSERT_FALSE(next_alarm(alarms, kMon0600).has_value());
}

void test_sunrise_window() {
  Alarm a = make(6, 30, kWeekdays);
  a.sunrise_minutes = 20;
  TEST_ASSERT_FALSE(sunrise_active(a, LocalTime{0, 6, 0}));   // 30 min out
  TEST_ASSERT_TRUE(sunrise_active(a, LocalTime{0, 6, 10}));   // 20 min out
  TEST_ASSERT_TRUE(sunrise_active(a, LocalTime{0, 6, 29}));   // 1 min out
  TEST_ASSERT_FALSE(sunrise_active(a, LocalTime{0, 6, 30}));  // ringing, not sunrise
}

void test_ringer_snooze_cycle() {
  Ringer r(9, 2);
  r.trigger(100);
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state());
  r.snooze(100);
  TEST_ASSERT_EQUAL(RingState::Snoozed, r.state());
  TEST_ASSERT_FALSE(r.tick(108));
  TEST_ASSERT_TRUE(r.tick(109));
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state());
}

void test_ringer_max_snoozes_keeps_ringing() {
  Ringer r(5, 1);
  r.trigger(0);
  r.snooze(0);
  r.tick(5);
  r.snooze(5);  // over the limit
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state());
  TEST_ASSERT_EQUAL_UINT8(1, r.snoozes());
}

void test_ringer_dismiss() {
  Ringer r;
  r.trigger(0);
  r.snooze(0);
  r.dismiss();
  TEST_ASSERT_EQUAL(RingState::Idle, r.state());
  TEST_ASSERT_FALSE(r.tick(1000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_later_today);
  RUN_TEST(test_fires_this_minute);
  RUN_TEST(test_passed_today_rolls_to_tomorrow);
  RUN_TEST(test_friday_evening_skips_to_monday);
  RUN_TEST(test_single_day_passed_waits_a_week);
  RUN_TEST(test_one_shot);
  RUN_TEST(test_disabled_never_fires);
  RUN_TEST(test_next_alarm_picks_soonest);
  RUN_TEST(test_next_alarm_none_enabled);
  RUN_TEST(test_sunrise_window);
  RUN_TEST(test_ringer_snooze_cycle);
  RUN_TEST(test_ringer_max_snoozes_keeps_ringing);
  RUN_TEST(test_ringer_dismiss);
  return UNITY_END();
}
