// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include "pcf85063.h"

using namespace inkwake::pcf85063;

void setUp() {}
void tearDown() {}

static void check_encode(int64_t t, const uint8_t (&expected)[kTimeRegs]) {
  uint8_t regs[kTimeRegs] = {};
  TEST_ASSERT_TRUE(encode(t, regs));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, regs, kTimeRegs);
  int64_t back = 0;
  TEST_ASSERT_TRUE(decode(regs, back));
  TEST_ASSERT_EQUAL_INT64(t, back);
}

void test_known_times() {
  // seconds, minutes, hours, day, weekday (0 = Sunday), month, year
  check_encode(946684800, {0x00, 0x00, 0x00, 0x01, 6, 0x01, 0x00});   // 2000-01-01 Sat
  check_encode(1795005296, {0x56, 0x34, 0x12, 0x18, 3, 0x11, 0x26});  // 2026-11-18 12:34:56 Wed
  check_encode(1709251199, {0x59, 0x59, 0x23, 0x29, 4, 0x02, 0x24});  // 2024-02-29 Thu
  check_encode(4102444799, {0x59, 0x59, 0x23, 0x31, 4, 0x12, 0x99});  // 2099-12-31 Thu
}

void test_out_of_range_not_encoded() {
  uint8_t regs[kTimeRegs] = {1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_FALSE(encode(kMinUnix - 1, regs));
  TEST_ASSERT_FALSE(encode(kMaxUnix + 1, regs));
  TEST_ASSERT_EQUAL_UINT8(1, regs[0]);
}

void test_oscillator_stopped_is_invalid() {
  const uint8_t regs[kTimeRegs] = {0x80 | 0x56, 0x34, 0x12, 0x18, 3, 0x11, 0x26};
  int64_t t = 0;
  TEST_ASSERT_FALSE(decode(regs, t));
}

void test_bad_fields_are_invalid() {
  const uint8_t good[kTimeRegs] = {0x56, 0x34, 0x12, 0x18, 3, 0x11, 0x26};
  struct Case {
    size_t reg;
    uint8_t value;
  };
  const Case cases[] = {
      {0, 0x5A},  // not BCD
      {0, 0x60},  // 60 seconds
      {1, 0x60},  // 60 minutes
      {2, 0x24},  // hour 24
      {3, 0x00},  // day 0
      {3, 0x31},  // 31 November
      {5, 0x00},  // month 0
      {5, 0x13},  // month 13
      {6, 0xA0},  // not BCD
  };
  for (const Case& c : cases) {
    uint8_t regs[kTimeRegs];
    for (size_t i = 0; i < kTimeRegs; i++) regs[i] = good[i];
    regs[c.reg] = c.value;
    int64_t t = 0;
    TEST_ASSERT_FALSE_MESSAGE(decode(regs, t), "bad field accepted");
  }
}

void test_february_29() {
  int64_t t = 0;
  const uint8_t leap[kTimeRegs] = {0, 0, 0, 0x29, 0, 0x02, 0x28};
  TEST_ASSERT_TRUE(decode(leap, t));
  const uint8_t not_leap[kTimeRegs] = {0, 0, 0, 0x29, 0, 0x02, 0x27};
  TEST_ASSERT_FALSE(decode(not_leap, t));
}

void test_unused_bits_ignored() {
  // Bit 7 of minutes and the top bits of hours/days/months read as undefined.
  const uint8_t regs[kTimeRegs] = {0x56,     0x80 | 0x34, 0xC0 | 0x12, 0xC0 | 0x18,
                                   0xF8 | 3, 0xE0 | 0x11, 0x26};
  int64_t t = 0;
  TEST_ASSERT_TRUE(decode(regs, t));
  TEST_ASSERT_EQUAL_INT64(1795005296, t);
}

void test_round_trip_every_day() {
  // Each day of the range at 23:59:59, plus the weekday sequence.
  uint8_t prev_wday = 5;  // 1999-12-31 was a Friday
  for (int64_t t = kMinUnix + 86399; t <= kMaxUnix; t += 86400) {
    uint8_t regs[kTimeRegs];
    TEST_ASSERT_TRUE(encode(t, regs));
    TEST_ASSERT_EQUAL_UINT8((prev_wday + 1) % 7, regs[4]);
    prev_wday = regs[4];
    int64_t back = 0;
    TEST_ASSERT_TRUE(decode(regs, back));
    TEST_ASSERT_EQUAL_INT64(t, back);
  }
}

void test_civil_days() {
  TEST_ASSERT_EQUAL_INT64(0, days_from_civil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(10957, days_from_civil(2000, 1, 1));
  int y;
  unsigned m, d;
  civil_from_days(-1, y, m, d);
  TEST_ASSERT_EQUAL_INT(1969, y);
  TEST_ASSERT_EQUAL_UINT(12, m);
  TEST_ASSERT_EQUAL_UINT(31, d);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_times);
  RUN_TEST(test_out_of_range_not_encoded);
  RUN_TEST(test_oscillator_stopped_is_invalid);
  RUN_TEST(test_bad_fields_are_invalid);
  RUN_TEST(test_february_29);
  RUN_TEST(test_unused_bits_ignored);
  RUN_TEST(test_round_trip_every_day);
  RUN_TEST(test_civil_days);
  return UNITY_END();
}
