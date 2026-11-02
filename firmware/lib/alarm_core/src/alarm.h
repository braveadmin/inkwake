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

// Ringing / snooze state machine. Time is a monotonic minute counter so it
// survives DST jumps without special cases.
class Ringer {
 public:
  explicit Ringer(uint8_t snooze_minutes = 9, uint8_t max_snoozes = 3);

  void trigger(int64_t now_min);
  // Ignored unless ringing. Past max_snoozes the alarm keeps ringing.
  void snooze(int64_t now_min);
  void dismiss();
  // Call once per minute. Returns true when a snooze has elapsed and
  // ringing must restart.
  bool tick(int64_t now_min);

  RingState state() const { return state_; }
  uint8_t snoozes() const { return snoozes_; }

 private:
  uint8_t snooze_minutes_;
  uint8_t max_snoozes_;
  RingState state_ = RingState::Idle;
  uint8_t snoozes_ = 0;
  int64_t resume_at_ = 0;
};

}  // namespace inkwake
