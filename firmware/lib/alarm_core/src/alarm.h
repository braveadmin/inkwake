// SPDX-License-Identifier: GPL-3.0-only
// Pure C++17 alarm logic. No Arduino or ESP-IDF includes: this file must
// compile on the host so it can be unit tested in CI (pio test -e native).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace inkwake {

// Weekday bitmask, Monday first (ISO 8601).
enum Weekday : uint8_t {
  MON = 1 << 0,
  TUE = 1 << 1,
  WED = 1 << 2,
  THU = 1 << 3,
  FRI = 1 << 4,
  SAT = 1 << 5,
  SUN = 1 << 6,
};
constexpr uint8_t kWeekdays = MON | TUE | WED | THU | FRI;
constexpr uint8_t kWeekend = SAT | SUN;
constexpr uint8_t kEveryDay = kWeekdays | kWeekend;
constexpr uint8_t kOneShot = 0;  // days == 0: fire once at the next hour:minute

struct Alarm {
  uint8_t id = 0;
  std::string label;
  uint8_t hour = 7;
  uint8_t minute = 0;
  uint8_t days = kWeekdays;
  bool enabled = true;
  std::string sound = "default";
  uint8_t sunrise_minutes = 0;  // 0 = no sunrise light
};

// Local wall-clock time, already timezone-adjusted by the caller.
struct LocalTime {
  uint8_t weekday;  // 0 = Monday ... 6 = Sunday
  uint8_t hour;     // 0-23
  uint8_t minute;   // 0-59
};

// Minutes from `now` until `alarm` next fires. 0 = fires this minute.
// std::nullopt when the alarm is disabled.
std::optional<uint32_t> minutes_until(const Alarm& alarm, const LocalTime& now);

struct NextAlarm {
  const Alarm* alarm;
  uint32_t in_minutes;
};

// Soonest enabled alarm. Ties resolve to the lowest index.
std::optional<NextAlarm> next_alarm(const std::vector<Alarm>& alarms, const LocalTime& now);

// True inside the sunrise window: 1..sunrise_minutes before the alarm fires.
bool sunrise_active(const Alarm& alarm, const LocalTime& now);

enum class RingState : uint8_t { Idle, Ringing, Snoozed };

struct RingConfig {
  uint16_t snooze_s;        // KEY tap / auto-snooze length
  uint8_t max_snoozes;      // after this many, a tap is ignored and the timeout gives up
  uint16_t ring_timeout_s;  // unattended ringing auto-snoozes after this
};
constexpr RingConfig kDefaultRing{9 * 60, 3, 10 * 60};

// Ringing / snooze state. Plain data (trivially copyable, all-zero = Idle) so
// it can live in RTC memory across deep sleep. Times are UTC seconds, which
// never jump at a DST change.
struct Ringer {
  RingState state;
  uint8_t alarm_id;  // which alarm rang
  uint8_t snoozes;
  int64_t since;      // Ringing: when this ring started
  int64_t resume_at;  // Snoozed: when to ring again
};

enum class RingEvent : uint8_t {
  None,
  Restart,     // snooze over, ring again
  AutoSnooze,  // rang ring_timeout_s unattended, now snoozed
  GaveUp,      // rang unattended after the last snooze, now idle
};

void ring_trigger(Ringer& r, uint8_t alarm_id, int64_t now);
// Ignored unless ringing. Past max_snoozes the alarm keeps ringing (hold KEY to stop).
void ring_snooze(Ringer& r, const RingConfig& c, int64_t now);
void ring_dismiss(Ringer& r);
// Call on every wake / loop pass.
RingEvent ring_tick(Ringer& r, const RingConfig& c, int64_t now);

// Beep volume in percent after `elapsed_s` of ringing: starts quiet and
// rises in steps (ADR-0006: wake-up volume sits around 5-20 %).
uint8_t ring_volume(uint32_t elapsed_s, uint8_t start_pct, uint8_t max_pct);

// Beep pattern, 1.2 s: four 100 ms beeps 100 ms apart, then 400 ms quiet.
constexpr uint32_t kBeepCycleMs = 1200;
bool beep_on(uint32_t ms);

}  // namespace inkwake
