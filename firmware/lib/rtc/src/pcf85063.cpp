// SPDX-License-Identifier: GPL-3.0-only
#include "pcf85063.h"

namespace inkwake::pcf85063 {

namespace {

constexpr int64_t kSecondsPerDay = 86400;

bool from_bcd(uint8_t v, unsigned lo, unsigned hi, unsigned& out) {
  const unsigned tens = v >> 4, ones = v & 0x0F;
  if (tens > 9 || ones > 9) return false;
  out = tens * 10 + ones;
  return out >= lo && out <= hi;
}

uint8_t to_bcd(unsigned v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

unsigned days_in_month(int year, unsigned month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : kDays[month - 1];
}

}  // namespace

int64_t days_from_civil(int year, unsigned month, unsigned day) {
  const int64_t y = static_cast<int64_t>(year) - (month <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, unsigned& month, unsigned& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (month <= 2));
}

bool decode(const uint8_t regs[kTimeRegs], int64_t& unix_seconds) {
  if (regs[0] & kSecondsOscStopped) return false;
  unsigned sec, min, hour, day, month, year;
  // Weekday (regs[4]) is derived from the date, so it is not checked.
  if (!from_bcd(regs[0] & 0x7F, 0, 59, sec) || !from_bcd(regs[1] & 0x7F, 0, 59, min) ||
      !from_bcd(regs[2] & 0x3F, 0, 23, hour) || !from_bcd(regs[3] & 0x3F, 1, 31, day) ||
      !from_bcd(regs[5] & 0x1F, 1, 12, month) || !from_bcd(regs[6], 0, 99, year)) {
    return false;
  }
  if (day > days_in_month(2000 + static_cast<int>(year), month)) return false;
  unix_seconds = days_from_civil(2000 + static_cast<int>(year), month, day) * kSecondsPerDay +
                 hour * 3600 + min * 60 + sec;
  return true;
}

bool encode(int64_t unix_seconds, uint8_t regs[kTimeRegs]) {
  if (unix_seconds < kMinUnix || unix_seconds > kMaxUnix) return false;
  const int64_t days = unix_seconds / kSecondsPerDay;
  const unsigned secs = static_cast<unsigned>(unix_seconds % kSecondsPerDay);
  int year;
  unsigned month, day;
  civil_from_days(days, year, month, day);
  regs[0] = to_bcd(secs % 60);  // OS flag cleared
  regs[1] = to_bcd(secs / 60 % 60);
  regs[2] = to_bcd(secs / 3600);
  regs[3] = to_bcd(day);
  regs[4] = static_cast<uint8_t>((days + 4) % 7);  // 1970-01-01 was a Thursday
  regs[5] = to_bcd(month);
  regs[6] = to_bcd(static_cast<unsigned>(year - 2000));
  return true;
}

}  // namespace inkwake::pcf85063
