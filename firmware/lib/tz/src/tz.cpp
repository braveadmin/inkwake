// SPDX-License-Identifier: GPL-3.0-only
#include "tz.h"

namespace inkwake::tz {

namespace {

constexpr int64_t kSecondsPerDay = 86400;
constexpr int32_t kMaxOffsetHours = 24;
constexpr int32_t kMaxRuleHours = 167;

bool is_leap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

unsigned days_in_month(int year, unsigned month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 && is_leap(year) ? 29 : kDays[month - 1];
}

// Days since 1970-01-01, proleptic Gregorian (H. Hinnant's algorithms). Kept
// local so lib/tz does not depend on the RTC codec.
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

int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Unsigned decimal of 1..max_digits digits.
bool parse_number(const char*& p, int max_digits, int32_t& out) {
  int n = 0;
  int32_t v = 0;
  while (is_digit(*p)) {
    if (++n > max_digits) return false;
    v = v * 10 + (*p++ - '0');
  }
  out = v;
  return n > 0;
}

// Zone abbreviation: 3+ letters, or <...> with letters, digits, '+' and '-'.
bool parse_name(const char*& p, char (&out)[kMaxName + 1]) {
  int n = 0;
  if (*p == '<') {
    ++p;
    while (is_alpha(*p) || is_digit(*p) || *p == '+' || *p == '-') {
      if (n == kMaxName) return false;
      out[n++] = *p++;
    }
    if (*p++ != '>') return false;
  } else {
    while (is_alpha(*p)) {
      if (n == kMaxName) return false;
      out[n++] = *p++;
    }
  }
  out[n] = '\0';
  return n >= 3;
}

// [+-]hh[:mm[:ss]] as signed seconds, hours up to max_hours.
bool parse_time(const char*& p, int32_t max_hours, int32_t& out) {
  int32_t sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  int32_t h = 0, m = 0, s = 0;
  if (!parse_number(p, 3, h) || h > max_hours) return false;
  if (*p == ':') {
    ++p;
    if (!parse_number(p, 2, m) || m > 59) return false;
    if (*p == ':') {
      ++p;
      if (!parse_number(p, 2, s) || s > 59) return false;
    }
  }
  out = sign * (h * 3600 + m * 60 + s);
  return true;
}

bool parse_rule(const char*& p, Rule& r) {
  int32_t a = 0, b = 0, c = 0;
  if (*p == 'J') {
    ++p;
    if (!parse_number(p, 3, a) || a < 1 || a > 365) return false;
    r.kind = Rule::Kind::Julian1;
    r.day = static_cast<uint16_t>(a);
  } else if (*p == 'M') {
    ++p;
    if (!parse_number(p, 2, a) || a < 1 || a > 12 || *p++ != '.') return false;
    if (!parse_number(p, 1, b) || b < 1 || b > 5 || *p++ != '.') return false;
    if (!parse_number(p, 1, c) || c > 6) return false;
    r.kind = Rule::Kind::MonthWeekDay;
    r.month = static_cast<uint8_t>(a);
    r.week = static_cast<uint8_t>(b);
    r.day = static_cast<uint16_t>(c);
  } else {
    if (!parse_number(p, 3, a) || a > 365) return false;
    r.kind = Rule::Kind::Julian0;
    r.day = static_cast<uint16_t>(a);
  }
  r.time = 7200;
  if (*p == '/') {
    ++p;
    if (!parse_time(p, kMaxRuleHours, r.time)) return false;
  }
  return true;
}

// Days since the epoch of the date the rule names in `year`.
int64_t rule_day(const Rule& r, int year) {
  const int64_t jan1 = days_from_civil(year, 1, 1);
  switch (r.kind) {
    case Rule::Kind::Julian1:
      return jan1 + r.day - 1 + (is_leap(year) && r.day >= 60);
    case Rule::Kind::Julian0:
      return jan1 + r.day;
    case Rule::Kind::MonthWeekDay:
    default: {
      const int64_t first = days_from_civil(year, r.month, 1);
      const int64_t first_wday = ((first + 4) % 7 + 7) % 7;  // 1970-01-01 was a Thursday
      int64_t d = (r.day - first_wday + 7) % 7 + (r.week - 1) * 7;
      if (d >= static_cast<int64_t>(days_in_month(year, r.month)))
        d -= 7;  // week 5 in a month with 4 such days
      return first + d;
    }
  }
}

int64_t rule_utc(const Rule& r, int year, int32_t offset_before) {
  return rule_day(r, year) * kSecondsPerDay + r.time - offset_before;
}

int civil_year(int64_t local) {
  int y;
  unsigned m, d;
  civil_from_days(floor_div(local, kSecondsPerDay), y, m, d);
  return y;
}

}  // namespace

bool parse(const char* posix, Zone& out) {
  if (posix == nullptr) return false;
  Zone z;
  const char* p = posix;
  if (!parse_name(p, z.std_name)) return false;
  int32_t west = 0;
  if (!parse_time(p, kMaxOffsetHours, west)) return false;
  z.std_offset = -west;
  z.dst_offset = z.std_offset;
  if (*p != '\0') {
    if (!parse_name(p, z.dst_name)) return false;
    z.dst_offset = z.std_offset + 3600;
    if (*p != ',') {
      if (!parse_time(p, kMaxOffsetHours, west)) return false;
      z.dst_offset = -west;
    }
    if (*p++ != ',' || !parse_rule(p, z.start)) return false;
    if (*p++ != ',' || !parse_rule(p, z.end)) return false;
    if (*p != '\0') return false;
    z.has_dst = true;
  }
  out = z;
  return true;
}

bool transitions(const Zone& zone, int year, int64_t& start_utc, int64_t& end_utc) {
  if (!zone.has_dst) return false;
  start_utc = rule_utc(zone.start, year, zone.std_offset);
  end_utc = rule_utc(zone.end, year, zone.dst_offset);
  return true;
}

bool is_dst(const Zone& zone, int64_t utc) {
  int64_t start, end;
  if (!transitions(zone, civil_year(utc + zone.std_offset), start, end)) return false;
  if (start < end) return utc >= start && utc < end;
  if (start > end) return utc < end || utc >= start;  // southern hemisphere
  return false;
}

int32_t utc_offset(const Zone& zone, int64_t utc) {
  return is_dst(zone, utc) ? zone.dst_offset : zone.std_offset;
}

int64_t to_utc(const Zone& zone, int64_t local) {
  const int64_t a = local - zone.std_offset;
  const int64_t b = local - zone.dst_offset;
  const bool a_ok = local_seconds(zone, a) == local;
  const bool b_ok = local_seconds(zone, b) == local;
  if (a_ok && b_ok) return a < b ? a : b;  // repeated: first instance
  if (a_ok) return a;
  if (b_ok) return b;
  // Skipped: read with the offset in effect just before the jump.
  const int64_t before = a < b ? a : b;
  return local - utc_offset(zone, before);
}

Civil to_civil(const Zone& zone, int64_t utc) {
  const bool dst = is_dst(zone, utc);
  const int64_t local = utc + (dst ? zone.dst_offset : zone.std_offset);
  const int64_t days = floor_div(local, kSecondsPerDay);
  const int64_t secs = local - days * kSecondsPerDay;
  Civil c{};
  int y;
  unsigned m, d;
  civil_from_days(days, y, m, d);
  c.year = y;
  c.month = static_cast<uint8_t>(m);
  c.day = static_cast<uint8_t>(d);
  c.weekday = static_cast<uint8_t>(((days + 3) % 7 + 7) % 7);  // 1970-01-01 = Thursday = 3
  c.hour = static_cast<uint8_t>(secs / 3600);
  c.minute = static_cast<uint8_t>(secs / 60 % 60);
  c.second = static_cast<uint8_t>(secs % 60);
  c.dst = dst;
  return c;
}

LocalTime to_local_time(const Zone& zone, int64_t utc) {
  const Civil c = to_civil(zone, utc);
  return LocalTime{c.weekday, c.hour, c.minute};
}

}  // namespace inkwake::tz
