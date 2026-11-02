// SPDX-License-Identifier: GPL-3.0-only
#include "alarm.h"

namespace inkwake {

namespace {
constexpr int32_t kMinutesPerDay = 24 * 60;
}

std::optional<uint32_t> minutes_until(const Alarm& alarm, const LocalTime& now) {
  if (!alarm.enabled) return std::nullopt;

  const int32_t now_min = now.hour * 60 + now.minute;
  const int32_t at_min = alarm.hour * 60 + alarm.minute;

  // d = 7 covers "same weekday next week" for a single-day mask.
  for (int32_t d = 0; d <= 7; ++d) {
    const int32_t delta = d * kMinutesPerDay + at_min - now_min;
    if (delta < 0) continue;
    if (alarm.days == kOneShot) return static_cast<uint32_t>(delta);
    const uint8_t day = static_cast<uint8_t>((now.weekday + d) % 7);
    if (alarm.days & (1u << day)) return static_cast<uint32_t>(delta);
  }
  return std::nullopt;
}

std::optional<NextAlarm> next_alarm(const std::vector<Alarm>& alarms, const LocalTime& now) {
  std::optional<NextAlarm> best;
  for (const Alarm& a : alarms) {
    const auto m = minutes_until(a, now);
    if (!m) continue;
    if (!best || *m < best->in_minutes) best = NextAlarm{&a, *m};
  }
  return best;
}

bool sunrise_active(const Alarm& alarm, const LocalTime& now) {
  if (alarm.sunrise_minutes == 0) return false;
  const auto m = minutes_until(alarm, now);
  return m && *m > 0 && *m <= alarm.sunrise_minutes;
}

Ringer::Ringer(uint8_t snooze_minutes, uint8_t max_snoozes)
    : snooze_minutes_(snooze_minutes), max_snoozes_(max_snoozes) {}

void Ringer::trigger(int64_t /*now_min*/) {
  state_ = RingState::Ringing;
  snoozes_ = 0;
}

void Ringer::snooze(int64_t now_min) {
  if (state_ != RingState::Ringing) return;
  if (snoozes_ >= max_snoozes_) return;
  ++snoozes_;
  resume_at_ = now_min + snooze_minutes_;
  state_ = RingState::Snoozed;
}

void Ringer::dismiss() {
  state_ = RingState::Idle;
  snoozes_ = 0;
}

bool Ringer::tick(int64_t now_min) {
  if (state_ == RingState::Snoozed && now_min >= resume_at_) {
    state_ = RingState::Ringing;
    return true;
  }
  return false;
}

}  // namespace inkwake
