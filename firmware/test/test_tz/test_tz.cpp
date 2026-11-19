// SPDX-License-Identifier: GPL-3.0-only
// Expected instants were cross-checked against glibc localtime() with the same
// TZ strings (one known, documented difference: permanent DST, see below).
#include <unity.h>

#include "tz.h"

using namespace inkwake;
using namespace inkwake::tz;

void setUp() {}
void tearDown() {}

static const char* kBerlin = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char* kSydney = "AEST-10AEDT,M10.1.0,M4.1.0/3";
static const char* kNewYork = "EST5EDT,M3.2.0,M11.1.0";
static const char* kSantiago = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";
static const char* kLordHowe = "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0";

static Zone zone(const char* s) {
  Zone z;
  TEST_ASSERT_TRUE_MESSAGE(parse(s, z), s);
  return z;
}

// Local Y-M-D h:m:s and DST flag at `utc`.
static void check_local(const Zone& z, int64_t utc, int y, int mo, int d, int h, int mi, int s,
                        bool dst) {
  const Civil c = to_civil(z, utc);
  TEST_ASSERT_EQUAL_INT(y, c.year);
  TEST_ASSERT_EQUAL_UINT8(mo, c.month);
  TEST_ASSERT_EQUAL_UINT8(d, c.day);
  TEST_ASSERT_EQUAL_UINT8(h, c.hour);
  TEST_ASSERT_EQUAL_UINT8(mi, c.minute);
  TEST_ASSERT_EQUAL_UINT8(s, c.second);
  TEST_ASSERT_EQUAL(dst, c.dst);
  TEST_ASSERT_EQUAL(dst, is_dst(z, utc));
}

// Wall clock h:m on a given local day, on the epoch scale (local midnight + h:m).
static int64_t wall(int64_t midnight, int h, int m) { return midnight + h * 3600 + m * 60; }
static const int64_t kMar29 = 1774742400;  // 2026-03-29 00:00 on the epoch scale
static const int64_t kOct25 = 1792886400;  // 2026-10-25
static const int64_t kOct04 = 1791072000;  // 2026-10-04

void test_parse_fields() {
  const Zone z = zone(kBerlin);
  TEST_ASSERT_EQUAL_STRING("CET", z.std_name);
  TEST_ASSERT_EQUAL_STRING("CEST", z.dst_name);
  TEST_ASSERT_EQUAL_INT32(3600, z.std_offset);
  TEST_ASSERT_EQUAL_INT32(7200, z.dst_offset);  // default: one hour ahead
  TEST_ASSERT_TRUE(z.has_dst);
  TEST_ASSERT_EQUAL_UINT8(3, z.start.month);
  TEST_ASSERT_EQUAL_UINT8(5, z.start.week);
  TEST_ASSERT_EQUAL_UINT16(0, z.start.day);
  TEST_ASSERT_EQUAL_INT32(7200, z.start.time);  // default 02:00
  TEST_ASSERT_EQUAL_INT32(3 * 3600, z.end.time);

  const Zone lh = zone(kLordHowe);
  TEST_ASSERT_EQUAL_STRING("+1030", lh.std_name);
  TEST_ASSERT_EQUAL_INT32(10 * 3600 + 1800, lh.std_offset);
  TEST_ASSERT_EQUAL_INT32(11 * 3600, lh.dst_offset);  // explicit, only 30 min ahead

  const Zone ny = zone(kNewYork);
  TEST_ASSERT_EQUAL_INT32(-5 * 3600, ny.std_offset);

  const Zone nuuk = zone("<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
  TEST_ASSERT_EQUAL_INT32(-3600, nuuk.start.time);
  TEST_ASSERT_EQUAL_INT32(0, nuuk.end.time);
}

// Europe: 2026-03-29 02:00 CET jumps to 03:00 CEST at 01:00 UTC.
void test_skipped_hour() {
  const Zone z = zone(kBerlin);
  const int64_t jump = 1774746000;
  check_local(z, jump - 1, 2026, 3, 29, 1, 59, 59, false);
  check_local(z, jump, 2026, 3, 29, 3, 0, 0, true);
  TEST_ASSERT_EQUAL_INT32(3600, utc_offset(z, jump - 1));
  TEST_ASSERT_EQUAL_INT32(7200, utc_offset(z, jump));

  // 02:30 does not exist: read with the offset before the jump, lands at 03:30 CEST.
  const int64_t t = to_utc(z, wall(kMar29, 2, 30));
  TEST_ASSERT_EQUAL_INT64(jump + 1800, t);
  check_local(z, t, 2026, 3, 29, 3, 30, 0, true);
  // Either side of the gap is exact.
  TEST_ASSERT_EQUAL_INT64(jump - 60, to_utc(z, wall(kMar29, 1, 59)));
  TEST_ASSERT_EQUAL_INT64(jump, to_utc(z, wall(kMar29, 3, 0)));
}

// Europe: 2026-10-25 03:00 CEST falls back to 02:00 CET at 01:00 UTC.
void test_repeated_hour() {
  const Zone z = zone(kBerlin);
  const int64_t back = 1792890000;
  check_local(z, back - 3600, 2026, 10, 25, 2, 0, 0, true);  // first 02:00
  check_local(z, back - 1, 2026, 10, 25, 2, 59, 59, true);
  check_local(z, back, 2026, 10, 25, 2, 0, 0, false);  // second 02:00
  check_local(z, back + 3599, 2026, 10, 25, 2, 59, 59, false);
  check_local(z, back + 3600, 2026, 10, 25, 3, 0, 0, false);

  // The same wall clock twice: to_utc picks the first (daylight) instance.
  TEST_ASSERT_EQUAL_INT64(back - 1800, to_utc(z, wall(kOct25, 2, 30)));
  TEST_ASSERT_EQUAL_INT64(back + 3600, to_utc(z, wall(kOct25, 3, 0)));
  TEST_ASSERT_EQUAL_INT64(back - 3600 - 60, to_utc(z, wall(kOct25, 1, 59)));

  // An alarm sees 02:30 twice; LocalTime has no DST flag, so the alarm logic
  // must dedupe on its monotonic minute counter.
  const LocalTime a = to_local_time(z, back - 1800);
  const LocalTime b = to_local_time(z, back + 1800);
  TEST_ASSERT_EQUAL_UINT8(a.hour, b.hour);
  TEST_ASSERT_EQUAL_UINT8(a.minute, b.minute);
}

void test_transitions() {
  int64_t s = 0, e = 0;
  TEST_ASSERT_TRUE(transitions(zone(kBerlin), 2026, s, e));
  TEST_ASSERT_EQUAL_INT64(1774746000, s);
  TEST_ASSERT_EQUAL_INT64(1792890000, e);
  TEST_ASSERT_TRUE(transitions(zone(kNewYork), 2026, s, e));
  TEST_ASSERT_EQUAL_INT64(1772953200, s);  // 2026-03-08 02:00 EST
  TEST_ASSERT_EQUAL_INT64(1793512800, e);  // 2026-11-01 02:00 EDT
  TEST_ASSERT_TRUE(transitions(zone(kNewYork), 2027, s, e));
  TEST_ASSERT_EQUAL_INT64(1805007600, s);  // 2027-03-14
  TEST_ASSERT_FALSE(transitions(zone("JST-9"), 2026, s, e));
}

// Australia/Sydney: DST from October to April, across the new year.
void test_southern_hemisphere() {
  const Zone z = zone(kSydney);
  int64_t s = 0, e = 0;
  TEST_ASSERT_TRUE(transitions(z, 2026, s, e));
  TEST_ASSERT_EQUAL_INT64(1775318400, e);  // 2026-04-05 03:00 AEDT -> 02:00 AEST
  TEST_ASSERT_EQUAL_INT64(1791043200, s);  // 2026-10-04 02:00 AEST -> 03:00 AEDT
  TEST_ASSERT_TRUE(e < s);

  check_local(z, e - 1, 2026, 4, 5, 2, 59, 59, true);
  check_local(z, e, 2026, 4, 5, 2, 0, 0, false);
  check_local(z, s - 1, 2026, 10, 4, 1, 59, 59, false);
  check_local(z, s, 2026, 10, 4, 3, 0, 0, true);
  // Midsummer, both sides of new year.
  check_local(z, 1798722000, 2027, 1, 1, 0, 0, 0, true);  // 2026-12-31 13:00 UTC
  check_local(z, 1798721999, 2026, 12, 31, 23, 59, 59, true);
  check_local(z, 1784073600, 2026, 7, 15, 10, 0, 0, false);  // 2026-07-15 00:00 UTC, winter

  // Chile: rule times of 24:00 (midnight at the end of Saturday).
  const Zone cl = zone(kSantiago);
  check_local(cl, 1775357999, 2026, 4, 4, 23, 59, 59, true);
  check_local(cl, 1775358000, 2026, 4, 4, 23, 0, 0, false);
  check_local(cl, 1788667199, 2026, 9, 5, 23, 59, 59, false);
  check_local(cl, 1788667200, 2026, 9, 6, 1, 0, 0, true);

  // Lord Howe: half-hour offset and a 30-minute DST shift.
  const Zone lh = zone(kLordHowe);
  check_local(lh, 1791041399, 2026, 10, 4, 1, 59, 59, false);
  check_local(lh, 1791041400, 2026, 10, 4, 2, 30, 0, true);
  TEST_ASSERT_EQUAL_INT64(1791041400 + 900, to_utc(lh, wall(kOct04, 2, 15)));  // skipped 15 min
}

void test_no_dst() {
  const char* const zones[] = {"UTC0", "JST-9", "IST-5:30", "<+0545>-5:45", "<-12>12", "<+14>-14"};
  const int32_t offsets[] = {0, 9 * 3600, 5 * 3600 + 1800, 5 * 3600 + 2700, -12 * 3600, 14 * 3600};
  for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
    const Zone z = zone(zones[i]);
    TEST_ASSERT_FALSE(z.has_dst);
    for (int64_t t = 1767225600; t < 1798761600; t += 86400 * 7 + 3599) {  // all of 2026
      TEST_ASSERT_FALSE(is_dst(z, t));
      TEST_ASSERT_EQUAL_INT32(offsets[i], utc_offset(z, t));
      TEST_ASSERT_EQUAL_INT64(t, to_utc(z, local_seconds(z, t)));
    }
  }
  // 2026-11-19 12:34:56 UTC is 18:19:56 in Kathmandu, and Friday already at UTC+14.
  check_local(zone("<+0545>-5:45"), 1795091696, 2026, 11, 19, 18, 19, 56, false);
  TEST_ASSERT_EQUAL_UINT8(4, to_local_time(zone("<+14>-14"), 1795091696).weekday);
  TEST_ASSERT_EQUAL_UINT8(3, to_local_time(zone("<-12>12"), 1795091696).weekday);
}

void test_julian_rules() {
  int64_t s = 0, e = 0;
  // Leap year 2024: J60 skips Feb 29 (= Mar 1), zero-based 59 counts it (= Feb 29).
  TEST_ASSERT_TRUE(transitions(zone("XST3XDT,J60/1,J300/1"), 2024, s, e));
  TEST_ASSERT_EQUAL_INT64(1709265600, s);  // 2024-03-01 01:00 -03
  TEST_ASSERT_TRUE(transitions(zone("XST3XDT,59,299"), 2024, s, e));
  TEST_ASSERT_EQUAL_INT64(1709182800, s);  // 2024-02-29 02:00 -03
  // Non-leap 2025: both are Mar 1.
  int64_t s1 = 0, s2 = 0;
  TEST_ASSERT_TRUE(transitions(zone("XST3XDT,J60/2,J300"), 2025, s1, e));
  TEST_ASSERT_TRUE(transitions(zone("XST3XDT,59,299"), 2025, s2, e));
  TEST_ASSERT_EQUAL_INT64(s1, s2);
}

// RFC 8536 spelling of all-year DST. glibc drops to standard time for the
// first hours of each UTC year here; this module stays in DST throughout.
void test_permanent_dst() {
  const Zone z = zone("EST5EDT,0/0,J365/25");
  for (int64_t t = 1767225600 - 86400; t < 1767225600 + 86400; t += 600) {  // around 2026-01-01
    TEST_ASSERT_TRUE(is_dst(z, t));
  }
}

void test_week_five_is_last() {
  int64_t a = 0, b = 0, e = 0;
  // March 2027 has four Sundays (7..28): week 5 falls back to the 4th, the 28th.
  TEST_ASSERT_TRUE(transitions(zone("CET-1CEST,M3.5.0,M10.5.0/3"), 2027, a, e));
  TEST_ASSERT_TRUE(transitions(zone("CET-1CEST,M3.4.0,M10.5.0/3"), 2027, b, e));
  TEST_ASSERT_EQUAL_INT64(a, b);
  TEST_ASSERT_EQUAL_INT64(1806195600, a);  // 2027-03-28 01:00 UTC
  // March 2026 has five (1..29): week 5 is the 29th, a week after week 4.
  TEST_ASSERT_TRUE(transitions(zone("CET-1CEST,M3.5.0,M10.5.0/3"), 2026, a, e));
  TEST_ASSERT_TRUE(transitions(zone("CET-1CEST,M3.4.0,M10.5.0/3"), 2026, b, e));
  TEST_ASSERT_EQUAL_INT64(7 * 86400, a - b);
}

void test_weekday_and_local_time() {
  // 2026-11-19 12:34:56 UTC is Thursday 13:34 in Berlin.
  const LocalTime lt = to_local_time(zone(kBerlin), 1795091696);
  TEST_ASSERT_EQUAL_UINT8(3, lt.weekday);  // Monday = 0
  TEST_ASSERT_EQUAL_UINT8(13, lt.hour);
  TEST_ASSERT_EQUAL_UINT8(34, lt.minute);
  // Monday 2026-11-23 03:00 UTC is still Sunday 22:00 in New York.
  const LocalTime ny = to_local_time(zone(kNewYork), 1795402800);
  TEST_ASSERT_EQUAL_UINT8(6, ny.weekday);
  TEST_ASSERT_EQUAL_UINT8(22, ny.hour);
}

void test_malformed() {
  const char* const bad[] = {
      nullptr,
      "",
      "CET",                           // no offset
      "CE-1",                          // name too short
      "<AB>-1",                        // quoted name too short
      "<CET-1",                        // unterminated <
      "<C T>-1",                       // space in quoted name
      "ABCDEFGHIJKLMNOP-1",            // name longer than 15
      "CET-25",                        // offset over 24 h
      "CET-1:60",                      // minutes 60
      "CET-1:00:60",                   // seconds 60
      "CET-1:",                        // missing minutes
      "CET-1CEST",                     // DST without rules
      "CET-1CEST,M3.5.0",              // missing end rule
      "CET-1CEST,M3.5.0,",             // empty end rule
      "CET-1CEST,M13.5.0,M10.5.0/3",   // month 13
      "CET-1CEST,M0.5.0,M10.5.0/3",    // month 0
      "CET-1CEST,M3.6.0,M10.5.0/3",    // week 6
      "CET-1CEST,M3.0.0,M10.5.0/3",    // week 0
      "CET-1CEST,M3.5.7,M10.5.0/3",    // weekday 7
      "CET-1CEST,M3.5,M10.5.0/3",      // missing weekday
      "CET-1CEST,J0,J300",             // Julian day 0
      "CET-1CEST,J366,J300",           // Julian day 366
      "CET-1CEST,366,300",             // zero-based day 366
      "CET-1CEST,M3.5.0/168,M10.5.0",  // rule time over 167 h
      "CET-1CEST,M3.5.0/,M10.5.0",     // empty rule time
      "CET-1CEST,M3.5.0,M10.5.0/3x",   // trailing garbage
      "CET-1 ",                        // trailing space
      "CET-1CEST-2x,M3.5.0,M10.5.0",   // garbage after DST offset
      "CET-0001",                      // too many hour digits
      "CET+-1",                        // double sign
  };
  for (const char* s : bad) {
    Zone z = zone("JST-9");
    TEST_ASSERT_FALSE_MESSAGE(parse(s, z), s ? s : "(null)");
    TEST_ASSERT_EQUAL_STRING("JST", z.std_name);  // untouched on failure
    TEST_ASSERT_EQUAL_INT32(9 * 3600, z.std_offset);
  }
}

void test_accepted_variants() {
  const char* const ok[] = {
      "UTC0",
      "GMT+0",
      "NST3:30NDT,M3.2.0,M11.1.0",
      "ABC-2:30:15DEF-3:45:30,M2.5.4/23:59:59,M11.4.3/-23",
      "XST3XDT,J60/1,J300/1",
      "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
      "<-02>2<-01>,M3.5.0/-1,M10.5.0/0",
      "ABCDEFGHIJKLMNO-1",  // 15 characters
      "EST5EDT,0/0,J365/25",
      "XST3XDT,M1.1.0/-167,M12.5.6/167",
  };
  for (const char* s : ok) zone(s);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_fields);
  RUN_TEST(test_skipped_hour);
  RUN_TEST(test_repeated_hour);
  RUN_TEST(test_transitions);
  RUN_TEST(test_southern_hemisphere);
  RUN_TEST(test_no_dst);
  RUN_TEST(test_julian_rules);
  RUN_TEST(test_permanent_dst);
  RUN_TEST(test_week_five_is_last);
  RUN_TEST(test_weekday_and_local_time);
  RUN_TEST(test_malformed);
  RUN_TEST(test_accepted_variants);
  return UNITY_END();
}
