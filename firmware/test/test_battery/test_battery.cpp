// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include "battery.h"

using namespace inkwake::battery;

void setUp() {}
void tearDown() {}

void test_levels_from_ok() {
  TEST_ASSERT_EQUAL(Level::Ok, classify(Level::Ok, 4100));
  TEST_ASSERT_EQUAL(Level::Ok, classify(Level::Ok, kLowMv));
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Ok, kLowMv - 1));
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Ok, kCriticalMv));
  TEST_ASSERT_EQUAL(Level::Critical, classify(Level::Ok, kCriticalMv - 1));
}

void test_low_needs_hysteresis_to_clear() {
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Low, kLowMv));
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Low, kLowMv + kHysteresisMv - 1));
  TEST_ASSERT_EQUAL(Level::Ok, classify(Level::Low, kLowMv + kHysteresisMv));
}

void test_critical_needs_hysteresis_to_clear() {
  TEST_ASSERT_EQUAL(Level::Critical, classify(Level::Critical, kCriticalMv + kHysteresisMv - 1));
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Critical, kCriticalMv + kHysteresisMv));
  // Charging: straight back to ok.
  TEST_ASSERT_EQUAL(Level::Ok, classify(Level::Critical, 4100));
}

void test_no_reading_keeps_level() {
  TEST_ASSERT_EQUAL(Level::Low, classify(Level::Low, 0));
  TEST_ASSERT_EQUAL(Level::Ok, classify(Level::Ok, 0));
}

void test_average() {
  const uint16_t mv[] = {3600, 3601, 3602, 3603};
  TEST_ASSERT_EQUAL_UINT16(3602, average_mv(mv, 4));  // 3601.5 rounds up
  TEST_ASSERT_EQUAL_UINT16(0, average_mv(mv, 0));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("ok", level_name(Level::Ok));
  TEST_ASSERT_EQUAL_STRING("low", level_name(Level::Low));
  TEST_ASSERT_EQUAL_STRING("critical", level_name(Level::Critical));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_levels_from_ok);
  RUN_TEST(test_low_needs_hysteresis_to_clear);
  RUN_TEST(test_critical_needs_hysteresis_to_clear);
  RUN_TEST(test_no_reading_keeps_level);
  RUN_TEST(test_average);
  RUN_TEST(test_names);
  return UNITY_END();
}
