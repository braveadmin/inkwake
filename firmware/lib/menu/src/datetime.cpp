// SPDX-License-Identifier: GPL-3.0-only
#include "datetime.h"

namespace inkwake::menu {

bool is_leap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t days_in_month(int year, int month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && is_leap(year) ? 29 : kDays[month - 1];
}

namespace {

void clamp_day(DateTime& t) {
  const uint8_t last = days_in_month(t.year, t.month);
  if (t.day > last) t.day = last;
  if (t.day < 1) t.day = 1;
}

}  // namespace

void increment(DateTime& t, Field f) {
  switch (f) {
    case Field::Year:
      t.year = t.year >= kMaxYear || t.year < kMinYear ? kMinYear : t.year + 1;
      clamp_day(t);
      break;
    case Field::Month:
      t.month = t.month >= 12 ? 1 : static_cast<uint8_t>(t.month + 1);
      clamp_day(t);
      break;
    case Field::Day:
      t.day = t.day >= days_in_month(t.year, t.month) ? 1 : static_cast<uint8_t>(t.day + 1);
      break;
    case Field::Hour:
      t.hour = t.hour >= 23 ? 0 : static_cast<uint8_t>(t.hour + 1);
      break;
    case Field::Minute:
      t.minute = t.minute >= 59 ? 0 : static_cast<uint8_t>(t.minute + 1);
      break;
    case Field::Count:
      break;
  }
}

DateTime clamped(DateTime t) {
  if (t.year < kMinYear) t.year = kMinYear;
  if (t.year > kMaxYear) t.year = kMaxYear;
  if (t.month < 1) t.month = 1;
  if (t.month > 12) t.month = 12;
  clamp_day(t);
  if (t.hour > 23) t.hour = 23;
  if (t.minute > 59) t.minute = 59;
  return t;
}

}  // namespace inkwake::menu
