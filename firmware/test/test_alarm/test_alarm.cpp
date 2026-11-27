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

static constexpr RingConfig kCfg{540, 2, 600};

void test_ringer_starts_idle() {
  Ringer r{};
  TEST_ASSERT_EQUAL(RingState::Idle, r.state);
  TEST_ASSERT_EQUAL(RingEvent::None, ring_tick(r, kCfg, 1000));
}

void test_ringer_snooze_cycle() {
  Ringer r{};
  ring_trigger(r, 4, 100);
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state);
  TEST_ASSERT_EQUAL_UINT8(4, r.alarm_id);
  ring_snooze(r, kCfg, 100);
  TEST_ASSERT_EQUAL(RingState::Snoozed, r.state);
  TEST_ASSERT_EQUAL(RingEvent::None, ring_tick(r, kCfg, 639));
  TEST_ASSERT_EQUAL(RingEvent::Restart, ring_tick(r, kCfg, 640));
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state);
  TEST_ASSERT_EQUAL_INT64(640, r.since);
}

void test_ringer_max_snoozes_keeps_ringing() {
  Ringer r{};
  ring_trigger(r, 1, 0);
  ring_snooze(r, kCfg, 0);
  ring_tick(r, kCfg, 540);
  ring_snooze(r, kCfg, 540);
  ring_tick(r, kCfg, 1080);
  ring_snooze(r, kCfg, 1080);  // over the limit
  TEST_ASSERT_EQUAL(RingState::Ringing, r.state);
  TEST_ASSERT_EQUAL_UINT8(2, r.snoozes);
}

void test_ringer_dismiss() {
  Ringer r{};
  ring_trigger(r, 1, 0);
  ring_snooze(r, kCfg, 0);
  ring_dismiss(r);
  TEST_ASSERT_EQUAL(RingState::Idle, r.state);
  TEST_ASSERT_EQUAL(RingEvent::None, ring_tick(r, kCfg, 100000));
}

void test_ringer_unattended_auto_snoozes_then_gives_up() {
  Ringer r{};
  ring_trigger(r, 1, 0);
  TEST_ASSERT_EQUAL(RingEvent::None, ring_tick(r, kCfg, 599));
  TEST_ASSERT_EQUAL(RingEvent::AutoSnooze, ring_tick(r, kCfg, 600));
  TEST_ASSERT_EQUAL(RingEvent::Restart, ring_tick(r, kCfg, 1140));
  TEST_ASSERT_EQUAL(RingEvent::AutoSnooze, ring_tick(r, kCfg, 1740));
  TEST_ASSERT_EQUAL(RingEvent::Restart, ring_tick(r, kCfg, 2280));
  TEST_ASSERT_EQUAL(RingEvent::GaveUp, ring_tick(r, kCfg, 2880));
  TEST_ASSERT_EQUAL(RingState::Idle, r.state);
}

void test_volume_levels() {
  TEST_ASSERT_EQUAL_UINT8(55, volume_pct(1));
  TEST_ASSERT_EQUAL_UINT8(90, volume_pct(kDefaultVolume));
  TEST_ASSERT_EQUAL_UINT8(100, volume_pct(10));
  TEST_ASSERT_EQUAL_UINT8(55, volume_pct(0));    // never silent
  TEST_ASSERT_EQUAL_UINT8(100, volume_pct(11));  // never past full
  const Alarm a;
  TEST_ASSERT_EQUAL_UINT8(kDefaultVolume, a.volume);
  TEST_ASSERT_TRUE(a.ramp);
}

void test_lead_in_fade() {
  const Alarm a;
  TEST_ASSERT_EQUAL_UINT8(0, a.lead_minutes);  // off unless chosen
  TEST_ASSERT_TRUE(lead_valid(0));
  TEST_ASSERT_TRUE(lead_valid(20));
  TEST_ASSERT_FALSE(lead_valid(3));
  // Level 8 (top -4 dBFS): -60 dBFS -> -19 dBFS, in half-dB steps.
  TEST_ASSERT_EQUAL_UINT8(120, lead_atten_half_db(0, 600, kDefaultVolume));
  TEST_ASSERT_EQUAL_UINT8(38, lead_atten_half_db(600, 600, kDefaultVolume));
  TEST_ASSERT_EQUAL_UINT8(38, lead_atten_half_db(9999, 600, kDefaultVolume));
  TEST_ASSERT_EQUAL_UINT8(79, lead_atten_half_db(300, 600, kDefaultVolume));  // halfway
  // Ends 15 dB under the level's top: level 10 at -15, level 1 at -33 dBFS.
  TEST_ASSERT_EQUAL_UINT8(30, lead_atten_half_db(1200, 1200, 10));
  TEST_ASSERT_EQUAL_UINT8(66, lead_atten_half_db(300, 300, 1));
  // A rising ring then starts 16 dB under the top: about where the lead-in ended.
  TEST_ASSERT_EQUAL_UINT8(50, ring_volume(0, kDefaultVolume, true));  // -20 dBFS
  // Never louder over time, one step at a time at most per second.
  uint8_t prev = 255;
  for (uint32_t t = 0; t <= 300; ++t) {
    const uint8_t v = lead_atten_half_db(t, 300, 1);
    TEST_ASSERT_TRUE(v <= prev);
    TEST_ASSERT_TRUE(prev == 255 || prev - v <= 1);
    prev = v;
  }
  TEST_ASSERT_EQUAL_UINT8(38, lead_atten_half_db(5, 0, kDefaultVolume));  // no lead: the end
}

void test_ring_volume_ramps_and_caps() {
  // Default level 8 keeps the behaviour before #43: 50 % -> 90 %.
  TEST_ASSERT_EQUAL_UINT8(50, ring_volume(0, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(50, ring_volume(19, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(55, ring_volume(20, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(85, ring_volume(140, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(90, ring_volume(160, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(90, ring_volume(600, kDefaultVolume, true));
  TEST_ASSERT_EQUAL_UINT8(15, ring_volume(0, 1, true));
  TEST_ASSERT_EQUAL_UINT8(55, ring_volume(600, 1, true));
  TEST_ASSERT_EQUAL_UINT8(60, ring_volume(0, 10, true));
  TEST_ASSERT_EQUAL_UINT8(100, ring_volume(UINT32_MAX, 10, true));
}

void test_ring_volume_steady() {
  TEST_ASSERT_EQUAL_UINT8(70, ring_volume(0, 4, false));
  TEST_ASSERT_EQUAL_UINT8(70, ring_volume(600, 4, false));
  TEST_ASSERT_EQUAL_UINT8(100, ring_volume(0, 10, false));
}

void test_beep_pattern() {
  TEST_ASSERT_TRUE(beep_on(0));
  TEST_ASSERT_TRUE(beep_on(99));
  TEST_ASSERT_FALSE(beep_on(100));
  TEST_ASSERT_TRUE(beep_on(600));
  TEST_ASSERT_FALSE(beep_on(700));
  TEST_ASSERT_FALSE(beep_on(800));
  TEST_ASSERT_FALSE(beep_on(1199));
  TEST_ASSERT_TRUE(beep_on(1200));
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
  RUN_TEST(test_ringer_starts_idle);
  RUN_TEST(test_ringer_snooze_cycle);
  RUN_TEST(test_ringer_max_snoozes_keeps_ringing);
  RUN_TEST(test_ringer_dismiss);
  RUN_TEST(test_ringer_unattended_auto_snoozes_then_gives_up);
  RUN_TEST(test_volume_levels);
  RUN_TEST(test_lead_in_fade);
  RUN_TEST(test_ring_volume_ramps_and_caps);
  RUN_TEST(test_ring_volume_steady);
  RUN_TEST(test_beep_pattern);
  return UNITY_END();
}
