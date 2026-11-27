// SPDX-License-Identifier: GPL-3.0-only
// Alarm editor values for the button menu (#55): hour, minute, a days preset,
// and with "Custom" each weekday on or off; then (#43) the volume level,
// rising or steady and (#4) the lead-in minutes. Same +1-with-wrap idea as the date & time editor.
#pragma once

#include <cstddef>
#include <cstdint>

#include "alarm.h"

namespace inkwake::menu {

// Days choices, in BOOT order. Once rings at the next hour:minute and then
// turns itself off (ADR-0017).
enum class DaysPreset : uint8_t { Once, Daily, Weekdays, Weekend, Custom, Count };

DaysPreset preset_of(uint8_t days);     // Custom unless the mask is one of the presets
const char* preset_name(DaysPreset p);  // "Once", "Daily", "Mon-Fri", "Weekend", "Custom"

// Days for the alarm list: the preset name, or two-letter days with runs of
// three or more as a range ("Mo We Fr", "Tu-Th Su"). `n` >= 24 is always enough.
void format_days_short(uint8_t days, char* buf, size_t n);

// One alarm list row: "07:00 Mon-Fri", " off" appended while it is off.
void format_alarm_row(const Alarm& a, char* buf, size_t n);

// Fields: hour, minute, days preset; with Custom, then Monday .. Sunday; then
// volume, ramp and lead-in (the editor's second page).
enum class AlarmField : uint8_t {
  Hour,
  Minute,
  Days,
  Mon,
  Tue,
  Wed,
  Thu,
  Fri,
  Sat,
  Sun,
  Volume,
  Ramp,
  Lead,
};

// "Rising" (starts quiet, gets louder) or "Steady" (full level at once).
const char* ramp_name(bool ramp);
// Lead-in (#4): "Off", "5 min" .. "20 min".
const char* lead_name(uint8_t minutes);

// Default for an alarm added with the buttons (decided 2026-11-25).
constexpr uint8_t kNewAlarmHour = 7;
constexpr uint8_t kNewAlarmMinute = 0;
constexpr uint8_t kNewAlarmDays = kWeekdays;

class AlarmEdit {
 public:
  AlarmEdit() = default;
  explicit AlarmEdit(const Alarm& a);

  uint8_t hour() const { return hour_; }
  uint8_t minute() const { return minute_; }
  DaysPreset preset() const { return preset_; }
  uint8_t days() const;  // the mask to save
  uint8_t volume() const { return volume_; }
  bool ramp() const { return ramp_; }
  uint8_t lead() const { return lead_; }
  AlarmField field() const { return field_; }
  // Volume, ramp and lead-in sit on the editor's second page.
  bool sound_page() const { return field_ >= AlarmField::Volume; }
  bool last_field() const;

  // BOOT: +1 on the field (wraps; volume 10 -> 1), next preset, a weekday
  // on/off, rising <-> steady, or the next lead-in choice (20 -> off).
  void increment();
  // KEY tap: on to the next field. False on the last field (KEY then marks Save).
  bool next_field();
  // Back to the hour (KEY past Cancel).
  void first_field() { field_ = AlarmField::Hour; }

 private:
  uint8_t hour_ = kNewAlarmHour;
  uint8_t minute_ = kNewAlarmMinute;
  DaysPreset preset_ = DaysPreset::Weekdays;
  uint8_t custom_ = kNewAlarmDays;  // days while Custom; starts from the alarm's own days
  uint8_t volume_ = kDefaultVolume;
  bool ramp_ = true;
  uint8_t lead_ = 0;
  AlarmField field_ = AlarmField::Hour;
};

}  // namespace inkwake::menu
