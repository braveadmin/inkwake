// SPDX-License-Identifier: GPL-3.0-only
#include "alarm_edit.h"

#include <cstdio>
#include <cstring>

namespace inkwake::menu {

namespace {

const char* const kPresetNames[] = {"Once", "Daily", "Mon-Fri", "Weekend", "Custom"};
const char* const kDay2[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

uint8_t preset_days(DaysPreset p) {
  switch (p) {
    case DaysPreset::Daily:
      return kEveryDay;
    case DaysPreset::Weekdays:
      return kWeekdays;
    case DaysPreset::Weekend:
      return kWeekend;
    default:
      return kOneShot;
  }
}

}  // namespace

DaysPreset preset_of(uint8_t days) {
  days &= kEveryDay;
  if (days == kOneShot) return DaysPreset::Once;
  if (days == kEveryDay) return DaysPreset::Daily;
  if (days == kWeekdays) return DaysPreset::Weekdays;
  if (days == kWeekend) return DaysPreset::Weekend;
  return DaysPreset::Custom;
}

const char* preset_name(DaysPreset p) {
  const auto i = static_cast<size_t>(p);
  return i < sizeof kPresetNames / sizeof kPresetNames[0] ? kPresetNames[i] : "";
}

void format_days_short(uint8_t days, char* buf, size_t n) {
  if (n == 0) return;
  buf[0] = '\0';
  const DaysPreset p = preset_of(days);
  if (p != DaysPreset::Custom) {
    snprintf(buf, n, "%s", preset_name(p));
    return;
  }
  size_t len = 0;
  for (int d = 0; d < 7;) {
    if (!(days & (1u << d))) {
      ++d;
      continue;
    }
    int end = d;
    while (end + 1 < 7 && (days & (1u << (end + 1)))) ++end;
    const char* sep = len > 0 ? " " : "";
    int w;
    if (end - d >= 2) {
      w = snprintf(buf + len, n - len, "%s%s-%s", sep, kDay2[d], kDay2[end]);
    } else if (end > d) {
      w = snprintf(buf + len, n - len, "%s%s %s", sep, kDay2[d], kDay2[end]);
    } else {
      w = snprintf(buf + len, n - len, "%s%s", sep, kDay2[d]);
    }
    if (w < 0 || static_cast<size_t>(w) >= n - len) return;  // truncated
    len += static_cast<size_t>(w);
    d = end + 1;
  }
}

void format_alarm_row(const Alarm& a, char* buf, size_t n) {
  char days[24];
  format_days_short(a.days, days, sizeof days);
  snprintf(buf, n, "%02u:%02u %s%s", a.hour, a.minute, days, a.enabled ? "" : " off");
}

AlarmEdit::AlarmEdit(const Alarm& a)
    : hour_(a.hour % 24),
      minute_(a.minute % 60),
      preset_(preset_of(a.days)),
      custom_(a.days == kOneShot ? kNewAlarmDays : static_cast<uint8_t>(a.days & kEveryDay)) {}

uint8_t AlarmEdit::days() const {
  return preset_ == DaysPreset::Custom ? custom_ : preset_days(preset_);
}

bool AlarmEdit::last_field() const {
  return field_ == (preset_ == DaysPreset::Custom ? AlarmField::Sun : AlarmField::Days);
}

void AlarmEdit::increment() {
  switch (field_) {
    case AlarmField::Hour:
      hour_ = static_cast<uint8_t>((hour_ + 1) % 24);
      break;
    case AlarmField::Minute:
      minute_ = static_cast<uint8_t>((minute_ + 1) % 60);
      break;
    case AlarmField::Days:
      preset_ = static_cast<DaysPreset>((static_cast<int>(preset_) + 1) %
                                        static_cast<int>(DaysPreset::Count));
      break;
    default: {
      const int d = static_cast<int>(field_) - static_cast<int>(AlarmField::Mon);
      custom_ = static_cast<uint8_t>(custom_ ^ (1u << d));
      break;
    }
  }
}

bool AlarmEdit::next_field() {
  if (last_field()) return false;
  field_ = static_cast<AlarmField>(static_cast<int>(field_) + 1);
  return true;
}

}  // namespace inkwake::menu
