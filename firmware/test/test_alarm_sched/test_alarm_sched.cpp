// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstring>

#include "alarm_sched.h"

using namespace inkwake;
using namespace inkwake::sched;

void setUp() {}
void tearDown() {}

static tz::Zone berlin() {
  tz::Zone z;
  tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", z);
  return z;
}

// UTC seconds for a UTC civil time (Howard Hinnant's days_from_civil).
static int64_t utc(int y, int m, int d, int h, int mi) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60;
}

static Alarm make(uint8_t id, uint8_t h, uint8_t m, uint8_t days, bool enabled = true) {
  Alarm a;
  a.id = id;
  a.hour = h;
  a.minute = m;
  a.days = days;
  a.enabled = enabled;
  return a;
}

// Mon 2026-11-23 06:00 CET = 05:00 UTC.
static const int64_t kMon0600 = utc(2026, 11, 23, 5, 0);

void test_next_fire_later_today() {
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 23, 6, 0),
                          *next_fire(make(1, 7, 0, kWeekdays), berlin(), kMon0600));
}

void test_next_fire_is_strictly_after() {
  // At exactly 07:00 local the next one is tomorrow.
  const int64_t at = utc(2026, 11, 23, 6, 0);
  TEST_ASSERT_EQUAL_INT64(at + 86400, *next_fire(make(1, 7, 0, kWeekdays), berlin(), at));
}

void test_next_fire_skips_weekend() {
  const int64_t fri_eve = utc(2026, 11, 27, 21, 0);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 30, 6, 0),
                          *next_fire(make(1, 7, 0, kWeekdays), berlin(), fri_eve));
}

void test_next_fire_single_day_next_week() {
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 30, 4, 0),
                          *next_fire(make(1, 5, 0, MON), berlin(), kMon0600));
}

void test_next_fire_disabled() {
  TEST_ASSERT_FALSE(next_fire(make(1, 7, 0, kEveryDay, false), berlin(), kMon0600).has_value());
}

void test_next_fire_one_shot() {
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 24, 4, 30),
                          *next_fire(make(1, 5, 30, kOneShot), berlin(), kMon0600));
}

void test_next_fire_across_spring_forward() {
  // Sun 2027-03-28: 02:00 CET -> 03:00 CEST at 01:00 UTC. 07:00 that day is CEST.
  const int64_t sat = utc(2027, 3, 27, 12, 0);
  TEST_ASSERT_EQUAL_INT64(utc(2027, 3, 28, 5, 0),
                          *next_fire(make(1, 7, 0, kEveryDay), berlin(), sat));
}

void test_skipped_hour_fires_at_the_jump() {
  const int64_t sat = utc(2027, 3, 27, 12, 0);
  TEST_ASSERT_EQUAL_INT64(utc(2027, 3, 28, 1, 0),
                          *next_fire(make(1, 2, 30, kEveryDay), berlin(), sat));
  TEST_ASSERT_EQUAL_INT64(utc(2027, 3, 28, 1, 0),
                          *next_fire(make(1, 2, 0, kEveryDay), berlin(), sat));
}

void test_repeated_hour_fires_once() {
  // Sun 2026-10-25: 03:00 CEST -> 02:00 CET at 01:00 UTC; 02:30 happens at
  // 00:30 UTC and again at 01:30 UTC.
  const Alarm a = make(1, 2, 30, kEveryDay);
  const int64_t first = *next_fire(a, berlin(), utc(2026, 10, 24, 12, 0));
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 0, 30), first);
  // After it rang, the second 02:30 is not a fire time: next is Monday 02:30 CET.
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 26, 1, 30), *next_fire(a, berlin(), first));
}

void test_due_window() {
  std::vector<Alarm> v{make(1, 7, 0, kWeekdays)};
  const int64_t at = utc(2026, 11, 23, 6, 0);
  TEST_ASSERT_FALSE(due(v, berlin(), at - 60, at - 1).has_value());
  auto d = due(v, berlin(), at - 60, at);
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_EQUAL_INT64(at, d->fire_utc);
  // Late wake within 2 min still rings.
  TEST_ASSERT_TRUE(due(v, berlin(), at - 60, at + 120).has_value());
  // Missed (e.g. board off) is skipped.
  TEST_ASSERT_FALSE(due(v, berlin(), at - 60, at + 121).has_value());
  // A check after a long gap still finds the alarm due now.
  TEST_ASSERT_TRUE(due(v, berlin(), at - 3 * 86400, at + 30).has_value());
  // Already handled on the previous wake.
  TEST_ASSERT_FALSE(due(v, berlin(), at, at + 60).has_value());
}

void test_due_picks_earliest() {
  std::vector<Alarm> v{make(1, 7, 1, kEveryDay), make(2, 7, 0, kEveryDay)};
  const auto d = due(v, berlin(), utc(2026, 11, 23, 5, 59), utc(2026, 11, 23, 6, 1));
  TEST_ASSERT_EQUAL_UINT32(1, d->index);
}

void test_upcoming() {
  std::vector<Alarm> v{make(1, 9, 30, kWeekend), make(2, 7, 0, kWeekdays),
                       make(3, 6, 45, kEveryDay, false)};
  const auto u = upcoming(v, berlin(), kMon0600);
  TEST_ASSERT_EQUAL_UINT32(1, u->index);
  TEST_ASSERT_FALSE(upcoming({}, berlin(), kMon0600).has_value());
}

void test_due_leaves_out_the_skipped_ring() {
  std::vector<Alarm> v{make(1, 7, 0, kWeekdays), make(2, 7, 0, kWeekdays)};
  const int64_t at = utc(2026, 11, 23, 6, 0);
  // The other alarm at the same minute still rings.
  auto d = due(v, berlin(), at - 60, at, Skip{1, at});
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_EQUAL_UINT32(1, d->index);
  v.pop_back();
  TEST_ASSERT_FALSE(due(v, berlin(), at - 60, at, Skip{1, at}).has_value());
  // Only that ring: the next day's rings again.
  TEST_ASSERT_TRUE(due(v, berlin(), at + 86340, at + 86400, Skip{1, at}).has_value());
}

void test_toggle_skip() {
  std::vector<Alarm> v{make(1, 9, 30, kWeekend), make(2, 7, 0, kWeekdays)};
  const Skip s = toggle_skip(v, berlin(), Skip{}, kMon0600);
  TEST_ASSERT_EQUAL_UINT8(2, s.id);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 23, 6, 0), s.fire_utc);
  TEST_ASSERT_EQUAL(SkipState::Pending, skip_state(v, berlin(), s, kMon0600));
  // A second tap undoes it.
  TEST_ASSERT_EQUAL_UINT8(0, toggle_skip(v, berlin(), s, kMon0600).id);
  TEST_ASSERT_EQUAL(SkipState::None, skip_state(v, berlin(), Skip{}, kMon0600));
  // No alarm on: nothing to skip.
  TEST_ASSERT_EQUAL_UINT8(0, toggle_skip({}, berlin(), Skip{}, kMon0600).id);
}

void test_skip_state() {
  std::vector<Alarm> v{make(2, 7, 0, kWeekdays)};
  const Skip s{2, utc(2026, 11, 23, 6, 0)};
  TEST_ASSERT_EQUAL(SkipState::Passed, skip_state(v, berlin(), s, s.fire_utc));
  TEST_ASSERT_EQUAL(SkipState::Passed, skip_state(v, berlin(), s, s.fire_utc + 3600));
  v[0].minute = 15;  // moved
  TEST_ASSERT_EQUAL(SkipState::Stale, skip_state(v, berlin(), s, kMon0600));
  v[0].minute = 0;
  v[0].enabled = false;  // turned off
  TEST_ASSERT_EQUAL(SkipState::Stale, skip_state(v, berlin(), s, kMon0600));
  TEST_ASSERT_EQUAL(SkipState::Stale, skip_state({}, berlin(), s, kMon0600));  // deleted
}

void test_ids() {
  std::vector<Alarm> v{make(1, 7, 0, 0), make(3, 7, 0, 0)};
  TEST_ASSERT_EQUAL_UINT8(2, free_id(v));
  TEST_ASSERT_EQUAL_UINT32(1, *find(v, 3));
  TEST_ASSERT_FALSE(find(v, 2).has_value());
}

void test_check() {
  Alarm a = make(1, 7, 0, kWeekdays);
  TEST_ASSERT_NULL(check(a));
  a.label = std::string(24, 'x');
  TEST_ASSERT_NOT_NULL(check(a));
  a.label = "ok";
  a.id = 0;
  TEST_ASSERT_NOT_NULL(check(a));
  a.id = 1;
  a.hour = 24;
  TEST_ASSERT_NOT_NULL(check(a));
}

void test_codec_round_trip() {
  std::vector<Alarm> v{make(1, 7, 0, kWeekdays), make(9, 22, 15, kOneShot, false)};
  v[0].label = "Work";
  v[0].sunrise_minutes = 20;
  v[1].sound = "birds";
  const auto blob = encode(v);
  std::vector<Alarm> out;
  TEST_ASSERT_TRUE(decode(blob.data(), blob.size(), out));
  TEST_ASSERT_EQUAL_UINT32(2, out.size());
  TEST_ASSERT_EQUAL_STRING("Work", out[0].label.c_str());
  TEST_ASSERT_EQUAL_UINT8(20, out[0].sunrise_minutes);
  TEST_ASSERT_EQUAL_UINT8(9, out[1].id);
  TEST_ASSERT_FALSE(out[1].enabled);
  TEST_ASSERT_EQUAL_STRING("birds", out[1].sound.c_str());
  TEST_ASSERT_EQUAL_UINT8(22, out[1].hour);
}

void test_codec_rejects_bad_blobs() {
  std::vector<Alarm> v{make(1, 7, 0, kWeekdays)};
  auto blob = encode(v);
  std::vector<Alarm> out{make(5, 1, 1, 0)};
  auto bad = blob;
  bad[2] = 2;  // version
  TEST_ASSERT_FALSE(decode(bad.data(), bad.size(), out));
  bad = blob;
  bad.pop_back();  // truncated
  TEST_ASSERT_FALSE(decode(bad.data(), bad.size(), out));
  bad = blob;
  bad.push_back(0);  // trailing byte
  TEST_ASSERT_FALSE(decode(bad.data(), bad.size(), out));
  bad = blob;
  bad[6] = 25;  // hour
  TEST_ASSERT_FALSE(decode(bad.data(), bad.size(), out));
  std::vector<Alarm> dup{make(1, 7, 0, 0), make(1, 8, 0, 0)};
  bad = encode(dup);
  TEST_ASSERT_FALSE(decode(bad.data(), bad.size(), out));
  TEST_ASSERT_EQUAL_UINT8(5, out[0].id);  // untouched
  const auto empty = encode({});
  TEST_ASSERT_TRUE(decode(empty.data(), empty.size(), out));
  TEST_ASSERT_EQUAL_UINT32(0, out.size());
}

void test_parse_time() {
  uint8_t h = 0, m = 0;
  TEST_ASSERT_TRUE(parse_time("07:30", h, m));
  TEST_ASSERT_EQUAL_UINT8(7, h);
  TEST_ASSERT_EQUAL_UINT8(30, m);
  TEST_ASSERT_TRUE(parse_time("7:05", h, m));
  TEST_ASSERT_EQUAL_UINT8(5, m);
  TEST_ASSERT_FALSE(parse_time("24:00", h, m));
  TEST_ASSERT_FALSE(parse_time("7:5", h, m));
  TEST_ASSERT_FALSE(parse_time("07:300", h, m));
  TEST_ASSERT_FALSE(parse_time("0730", h, m));
  TEST_ASSERT_FALSE(parse_time("", h, m));
}

void test_parse_days() {
  uint8_t d = 0;
  TEST_ASSERT_TRUE(parse_days("Weekdays", d));
  TEST_ASSERT_EQUAL_UINT8(kWeekdays, d);
  TEST_ASSERT_TRUE(parse_days("once", d));
  TEST_ASSERT_EQUAL_UINT8(kOneShot, d);
  TEST_ASSERT_TRUE(parse_days("mon,wed,fri", d));
  TEST_ASSERT_EQUAL_UINT8(MON | WED | FRI, d);
  TEST_ASSERT_TRUE(parse_days("mon-thu,sun", d));
  TEST_ASSERT_EQUAL_UINT8(MON | TUE | WED | THU | SUN, d);
  TEST_ASSERT_TRUE(parse_days("fri-mon", d));
  TEST_ASSERT_EQUAL_UINT8(FRI | SAT | SUN | MON, d);
  TEST_ASSERT_FALSE(parse_days("mon,,tue", d));
  TEST_ASSERT_FALSE(parse_days("funday", d));
  TEST_ASSERT_FALSE(parse_days("", d));
}

void test_format_days() {
  TEST_ASSERT_EQUAL_STRING("weekdays", format_days(kWeekdays).c_str());
  TEST_ASSERT_EQUAL_STRING("once", format_days(kOneShot).c_str());
  TEST_ASSERT_EQUAL_STRING("mon,wed,fri", format_days(MON | WED | FRI).c_str());
  TEST_ASSERT_EQUAL_STRING("mon-thu,sun", format_days(MON | TUE | WED | THU | SUN).c_str());
  TEST_ASSERT_EQUAL_STRING("mon,tue", format_days(MON | TUE).c_str());
  for (int m = 0; m < 128; ++m) {
    uint8_t back = 0xff;
    TEST_ASSERT_TRUE(parse_days(format_days(static_cast<uint8_t>(m)).c_str(), back));
    TEST_ASSERT_EQUAL_UINT8(m, back);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_next_fire_later_today);
  RUN_TEST(test_next_fire_is_strictly_after);
  RUN_TEST(test_next_fire_skips_weekend);
  RUN_TEST(test_next_fire_single_day_next_week);
  RUN_TEST(test_next_fire_disabled);
  RUN_TEST(test_next_fire_one_shot);
  RUN_TEST(test_next_fire_across_spring_forward);
  RUN_TEST(test_skipped_hour_fires_at_the_jump);
  RUN_TEST(test_repeated_hour_fires_once);
  RUN_TEST(test_due_window);
  RUN_TEST(test_due_picks_earliest);
  RUN_TEST(test_upcoming);
  RUN_TEST(test_due_leaves_out_the_skipped_ring);
  RUN_TEST(test_toggle_skip);
  RUN_TEST(test_skip_state);
  RUN_TEST(test_ids);
  RUN_TEST(test_check);
  RUN_TEST(test_codec_round_trip);
  RUN_TEST(test_codec_rejects_bad_blobs);
  RUN_TEST(test_parse_time);
  RUN_TEST(test_parse_days);
  RUN_TEST(test_format_days);
  return UNITY_END();
}
