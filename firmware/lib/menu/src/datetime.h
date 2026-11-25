// SPDX-License-Identifier: GPL-3.0-only
// Date & time editor values (#41): fields, ranges, +1 with wrap-around.
#pragma once

#include <cstdint>

namespace inkwake::menu {

// Local wall-clock time as the user edits it.
struct DateTime {
  int year = kMinYearValue;
  uint8_t month = 1;  // 1-12
  uint8_t day = 1;    // 1-days_in_month
  uint8_t hour = 0;   // 0-23
  uint8_t minute = 0;

  static constexpr int kMinYearValue = 2025;
  bool operator==(const DateTime& o) const {
    return year == o.year && month == o.month && day == o.day && hour == o.hour &&
           minute == o.minute;
  }
};

// The clock treats times before 2025 as "not set" (kMinValidTime in main.cpp).
constexpr int kMinYear = DateTime::kMinYearValue;
constexpr int kMaxYear = 2099;

// Edited in this order; KEY tap moves to the next, the last one saves.
enum class Field : uint8_t { Year, Month, Day, Hour, Minute, Count };
constexpr int kFieldCount = static_cast<int>(Field::Count);

bool is_leap(int year);
uint8_t days_in_month(int year, int month);  // 0 for a month outside 1-12

// +1 on one field, wrapping inside its range (2099 -> 2025, 12 -> 1, 31 -> 1,
// 23 -> 0, 59 -> 0). A day past the end of a changed month or year is clamped
// to the month's last day (31 Jan -> Feb gives 28 or 29 Feb).
void increment(DateTime& t, Field f);

// Brings every field into range: the editor's start value from any clock.
DateTime clamped(DateTime t);

}  // namespace inkwake::menu
