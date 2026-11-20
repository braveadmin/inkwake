// SPDX-License-Identifier: GPL-3.0-only
// POSIX TZ strings (IEEE Std 1003.1, "TZ" environment variable) and the
// conversion between UTC, which the RTC keeps, and local wall-clock time.
// Pure C++17, no Arduino or ESP-IDF includes, unit tested on the host.
//
// Why not newlib's setenv("TZ") + localtime_r: this keeps the rules testable in
// CI, has no global state, and gives local -> UTC with a defined policy for the
// skipped and repeated hours (needed when the user types a local time).
//
// Accepted form: std offset [dst [offset] ,start[/time] ,end[/time]]
//   CET-1CEST,M3.5.0,M10.5.0/3          Europe/Berlin
//   AEST-10AEDT,M10.1.0,M4.1.0/3        Australia/Sydney (DST over new year)
//   IST-5:30                            Asia/Kolkata, no DST
//   <-02>2<-01>,M3.5.0/-1,M10.5.0/0     America/Nuuk (negative rule time)
// Offsets are hours west of UTC (CET-1 is UTC+1), hh[:mm[:ss]], up to 24 h.
// Rule dates: Jn (1-365, Feb 29 never counted), n (0-365, counts Feb 29) or
// Mm.w.d (month 1-12, week 1-5 where 5 = last, weekday 0 = Sunday). Rule times
// default to 02:00 and may be -167..167 h (RFC 8536 extension). A DST name
// without rules is rejected rather than given glibc's implicit US rules.
#pragma once

#include <cstdint>

#include "alarm.h"

namespace inkwake::tz {

constexpr int kMaxName = 15;  // characters, without the quotes of <...>

struct Rule {
  enum class Kind : uint8_t { Julian1, Julian0, MonthWeekDay };
  Kind kind = Kind::MonthWeekDay;
  uint16_t day = 0;     // Julian1: 1-365, Julian0: 0-365, MonthWeekDay: weekday 0-6 (Sunday = 0)
  uint8_t month = 0;    // MonthWeekDay: 1-12
  uint8_t week = 0;     // MonthWeekDay: 1-5, 5 = last
  int32_t time = 7200;  // seconds after local midnight, in the time in effect before the change
};

struct Zone {
  char std_name[kMaxName + 1] = "UTC";
  char dst_name[kMaxName + 1] = "";
  int32_t std_offset = 0;  // seconds east of UTC (local = UTC + offset)
  int32_t dst_offset = 0;
  bool has_dst = false;
  Rule start;  // DST begins (wall clock in standard time)
  Rule end;    // DST ends (wall clock in daylight time)
};

// False on any syntax or range error; `out` is then left untouched.
bool parse(const char* posix, Zone& out);

// UTC instants at which DST starts and ends in local-standard-time `year`. On a
// southern-hemisphere rule `end` comes before `start`. False without DST.
bool transitions(const Zone& zone, int year, int64_t& start_utc, int64_t& end_utc);

bool is_dst(const Zone& zone, int64_t utc);
int32_t utc_offset(const Zone& zone, int64_t utc);  // seconds east of UTC

// Local wall-clock time as seconds on the Unix epoch scale (UTC + offset).
inline int64_t local_seconds(const Zone& zone, int64_t utc) { return utc + utc_offset(zone, utc); }

// Wall clock -> UTC. In the skipped hour the time is read with the offset in
// effect before the change, so 02:30 on a spring-forward night lands at 03:30
// DST. In the repeated hour the first (daylight) instance is returned.
int64_t to_utc(const Zone& zone, int64_t local_seconds);

struct Civil {
  int year;
  uint8_t month;    // 1-12
  uint8_t day;      // 1-31
  uint8_t weekday;  // 0 = Monday ... 6 = Sunday, as LocalTime
  uint8_t hour, minute, second;
  bool dst;
};

Civil to_civil(const Zone& zone, int64_t utc);

// The struct alarm_core evaluates alarms against.
LocalTime to_local_time(const Zone& zone, int64_t utc);

}  // namespace inkwake::tz
