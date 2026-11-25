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

void ring_trigger(Ringer& r, uint8_t alarm_id, int64_t now) {
  r = Ringer{RingState::Ringing, alarm_id, 0, now, 0};
}

void ring_snooze(Ringer& r, const RingConfig& c, int64_t now) {
  if (r.state != RingState::Ringing || r.snoozes >= c.max_snoozes) return;
  ++r.snoozes;
  r.resume_at = now + c.snooze_s;
  r.state = RingState::Snoozed;
}

void ring_dismiss(Ringer& r) { r = Ringer{}; }

RingEvent ring_tick(Ringer& r, const RingConfig& c, int64_t now) {
  if (r.state == RingState::Snoozed && now >= r.resume_at) {
    r.state = RingState::Ringing;
    r.since = now;
    return RingEvent::Restart;
  }
  if (r.state == RingState::Ringing && now - r.since >= c.ring_timeout_s) {
    if (r.snoozes < c.max_snoozes) {
      ring_snooze(r, c, now);
      return RingEvent::AutoSnooze;
    }
    ring_dismiss(r);
    return RingEvent::GaveUp;
  }
  return RingEvent::None;
}

uint8_t volume_pct(uint8_t level) {
  if (level < kMinVolume) level = kMinVolume;
  if (level > kMaxVolume) level = kMaxVolume;
  return static_cast<uint8_t>(50 + 5 * level);
}

uint8_t ring_volume(uint32_t elapsed_s, uint8_t level, bool ramp) {
  const uint32_t top = volume_pct(level);
  if (!ramp) return static_cast<uint8_t>(top);
  // From 40 % below the top, +5 % every 20 s (the steps cap first, so a long
  // ring cannot overflow).
  const uint32_t steps = elapsed_s / 20;
  const uint32_t v = top - 40 + (steps > 8 ? 8 : steps) * 5;
  return static_cast<uint8_t>(v > top ? top : v);
}

bool beep_on(uint32_t ms) {
  const uint32_t t = ms % kBeepCycleMs;
  return t < 800 && (t / 100) % 2 == 0;
}

}  // namespace inkwake
