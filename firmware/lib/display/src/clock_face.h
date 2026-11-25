// SPDX-License-Identifier: GPL-3.0-only
// Clock face for the 400x300 panel: date, HH:MM, room temperature.
// Each field has a fixed screen region so a change refreshes only that region.
#pragma once

#include <cstddef>
#include <cstdint>

#include "framebuffer.h"

namespace inkwake {

constexpr int16_t kNoTemp = INT16_MIN;  // no sensor reading yet

struct FaceState {
  uint8_t hour = 0;           // 0-23
  uint8_t minute = 0;         // 0-59
  uint8_t weekday = 0;        // 0 = Monday ... 6 = Sunday
  uint8_t day = 1;            // 1-31
  uint8_t month = 1;          // 1-12
  int16_t temp_dc = kNoTemp;  // room temperature in tenths of a degree C
  bool time_set = true;       // false: the clock lost its time; shows --:-- and "Time not set"
  bool wifi_failed = false;   // last Wi-Fi sync failed: small Wi-Fi "!" mark top right (#20)
  uint8_t battery = 0;        // 0 ok, 1 low, 2 critical (battery::Level): icon top left (#36)
  // Next alarm on the bottom line, next to the temperature (#2).
  // kAlarmNone, kAlarmNext (bell + time), kAlarmSnoozed (Zz + time), kAlarmSkipped
  // (crossed-out bell + time: BOOT skipped this ring, #55).
  uint8_t alarm = kAlarmNone;
  uint8_t alarm_hour = 0;  // local time it rings (again)
  uint8_t alarm_minute = 0;
  uint8_t alarm_weekday = kSoon;  // 0-6 shown as "Mon 07:00" when more than a day away

  static constexpr uint8_t kAlarmNone = 0, kAlarmNext = 1, kAlarmSnoozed = 2, kAlarmSkipped = 3;
  static constexpr uint8_t kSoon = 0xFF;

  bool operator==(const FaceState& o) const {
    return time_set == o.time_set && hour == o.hour && minute == o.minute && weekday == o.weekday &&
           day == o.day && month == o.month && temp_dc == o.temp_dc &&
           wifi_failed == o.wifi_failed && battery == o.battery && alarm == o.alarm &&
           alarm_hour == o.alarm_hour && alarm_minute == o.alarm_minute &&
           alarm_weekday == o.alarm_weekday;
  }
  bool operator!=(const FaceState& o) const { return !(*this == o); }
};

enum class FaceRegion : uint8_t {
  HourTens,
  HourOnes,
  MinuteTens,
  MinuteOnes,
  Date,
  Temp,
  WifiMark,
  BatteryMark,
  Count
};
constexpr size_t kFaceRegions = static_cast<size_t>(FaceRegion::Count);

// Screen area a field may draw into, inside the panel.
Rect face_region(FaceRegion region);

// Clears the framebuffer and draws the whole face.
void render_face(Framebuffer& fb, const FaceState& s);

// Regions whose content differs between two states, written to `out`
// (room for kFaceRegions). Returns how many.
size_t face_dirty(const FaceState& before, const FaceState& after, Rect* out);

// "Wednesday 18 November", or "Time not set". `n` >= 32 is always enough.
void format_date(const FaceState& s, char* buf, size_t n);
// "21.5 °C", "-3.0 °C", or "--.- °C" without a reading (UTF-8).
void format_temp(int16_t temp_dc, char* buf, size_t n);
// "07:00", "Sat 09:30", or "" when no alarm is shown.
void format_alarm(const FaceState& s, char* buf, size_t n);

// Ringing screen (#2): `title` (the alarm label, or "Alarm") on top, the
// current time large, and the KEY hint at the bottom. Full refresh.
void render_ringing(Framebuffer& fb, uint8_t hour, uint8_t minute, const char* title);
constexpr char kRingHint[] = "tap: snooze   hold: stop";

}  // namespace inkwake
