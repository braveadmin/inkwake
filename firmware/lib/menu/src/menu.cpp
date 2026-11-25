// SPDX-License-Identifier: GPL-3.0-only
#include "menu.h"

#include <cstdio>

#include "tz_presets.h"

namespace inkwake::menu {

namespace {

const char* const kMainItems[] = {"Date & time", "Alarms", "Timezone", "Wi-Fi"};
constexpr size_t kMainCount = sizeof kMainItems / sizeof kMainItems[0];
enum MainItem : size_t { kItemDateTime, kItemAlarms, kItemTimezone, kItemWifi };

const char* const kWifiItems[] = {"Set up Wi-Fi", "Forget all networks"};
const char* const kForgetItems[] = {"Cancel", "Forget all"};
constexpr char kCustom[] = "Custom";
constexpr char kAddAlarm[] = "Add alarm";
constexpr char kBack[] = "Back";
constexpr char kClose[] = "Close";
const char* const kDeleteItems[] = {"Cancel", "Delete"};
enum AlarmItem : size_t { kAlarmEdit, kAlarmToggle, kAlarmDelete, kAlarmItemCount };

}  // namespace

void Menu::open(const Context& c) {
  ctx_ = c;
  datetime_alone_ = false;
  first_time_ = false;
  main_sel_ = 0;
  show(Screen::Main, 0);
}

void Menu::open_datetime(const Context& c) {
  ctx_ = c;
  datetime_alone_ = true;
  first_time_ = false;
  start_datetime();
}

void Menu::open_first_time(const Context& c) {
  ctx_ = c;
  datetime_alone_ = true;
  first_time_ = true;
  show(Screen::Timezone, has_custom() ? 0 : static_cast<size_t>(c.tz_preset));
}

void Menu::show(Screen s, size_t sel) {
  screen_ = s;
  sel_ = sel;
  button_ = EditorButton::None;
}

void Menu::start_datetime() {
  value_ = clamped(ctx_.now);
  field_ = Field::Year;
  show(Screen::DateTime, 0);
}

bool Menu::has_back_row() const {
  switch (screen_) {
    case Screen::Main:
    case Screen::Wifi:
    case Screen::Alarms:
    case Screen::AlarmItem:
      return true;
    case Screen::Timezone:
      return !first_time_;  // first start: no way back, only on
    default:
      return false;  // editors (Cancel) and confirmations (Cancel first)
  }
}

bool Menu::on_back_row() const { return has_back_row() && sel_ + 1 == count(); }

size_t Menu::count() const {
  size_t n = 0;
  switch (screen_) {
    case Screen::Main:
      n = kMainCount;
      break;
    case Screen::Timezone:
      n = kTzPresetCount + (has_custom() ? 1 : 0);
      break;
    case Screen::Wifi:
      n = ctx_.wifi_saved ? 2 : 1;
      break;
    case Screen::WifiForget:
    case Screen::AlarmDelete:
      n = 2;
      break;
    case Screen::Alarms:
      n = ctx_.alarms.size() + (can_add() ? 1 : 0);
      break;
    case Screen::AlarmItem:
      n = kAlarmItemCount;
      break;
    default:
      return 0;
  }
  return n + (has_back_row() ? 1 : 0);
}

const char* Menu::item(size_t i) const {
  if (i >= count()) return "";
  if (has_back_row() && i + 1 == count()) return screen_ == Screen::Main ? kClose : kBack;
  switch (screen_) {
    case Screen::Main:
      return kMainItems[i];
    case Screen::Timezone:
      if (has_custom()) {
        if (i == 0) return kCustom;
        --i;
      }
      return kTzPresets[i].name;
    case Screen::Wifi:
      return kWifiItems[i];
    case Screen::WifiForget:
      return kForgetItems[i];
    case Screen::Alarms:
      if (i == ctx_.alarms.size()) return kAddAlarm;
      format_alarm_row(ctx_.alarms[i], row_[i], kItemLen);
      return row_[i];
    case Screen::AlarmItem:
      if (i == kAlarmToggle) return ctx_.alarms[alarm_].enabled ? "Turn off" : "Turn on";
      return i == kAlarmEdit ? "Edit" : "Delete";
    case Screen::AlarmDelete:
      return kDeleteItems[i];
    default:
      return "";
  }
}

const char* Menu::title() const {
  switch (screen_) {
    case Screen::Main:
      return "Menu";
    case Screen::DateTime:
      return "Date & time";
    case Screen::Timezone:
      return "Timezone";
    case Screen::Wifi:
      return "Wi-Fi";
    case Screen::WifiForget:
      return "Forget all networks?";
    case Screen::Alarms:
      return "Alarms";
    case Screen::AlarmItem:
    case Screen::AlarmDelete: {
      const Alarm& a = ctx_.alarms[alarm_];
      snprintf(title_, sizeof title_,
               screen_ == Screen::AlarmItem ? "Alarm %02u:%02u" : "Delete %02u:%02u?", a.hour,
               a.minute);
      return title_;
    }
    case Screen::AlarmEdit:
      return adding_ ? "New alarm" : "Edit alarm";
    default:
      return "";
  }
}

const char* Menu::key_label() const { return screen_ == Screen::Closed ? "" : "Next"; }

const char* Menu::key_hold_label() const {
  switch (screen_) {
    case Screen::Closed:
      return "";
    case Screen::Main:
      return "hold: close";
    case Screen::Timezone:
      return first_time_ ? "hold 5 s: Wi-Fi" : "hold: back";
    case Screen::DateTime:
      if (first_time_) return "hold 5 s: Wi-Fi";
      return datetime_alone_ ? "hold: close" : "hold: back";
    default:
      return "hold: back";
  }
}

const char* Menu::boot_label() const {
  if (screen_ == Screen::Closed) return "";
  if (on_back_row()) return item(sel_);  // "Back" or "Close"
  if (button_ == EditorButton::Save) return "Save";
  if (button_ == EditorButton::Cancel) return "Cancel";
  switch (screen_) {
    case Screen::Main:
    case Screen::Alarms:
      return "Open";
    case Screen::Timezone:
      return first_time_ ? "OK" : "Save";
    case Screen::DateTime:
      return "+1";
    case Screen::AlarmEdit:
      switch (edit_.field()) {
        case AlarmField::Hour:
        case AlarmField::Minute:
        case AlarmField::Volume:
          return "+1";
        case AlarmField::Days:
        case AlarmField::Ramp:
          return "Change";
        default:
          return "On/off";
      }
    default:
      return "Choose";
  }
}

bool Menu::repeats() const {
  if (button_ != EditorButton::None) return false;
  if (screen_ == Screen::DateTime) return true;
  return screen_ == Screen::AlarmEdit &&
         (edit_.field() == AlarmField::Hour || edit_.field() == AlarmField::Minute);
}

Action Menu::handle(Input in) {
  if (screen_ == Screen::Closed) return Action::None;
  if (screen_ == Screen::DateTime || screen_ == Screen::AlarmEdit) return handle_editor(in);
  if (in == Input::Move) {
    sel_ = (sel_ + 1) % count();
    return Action::None;
  }
  if (in == Input::Back || on_back_row()) return back();
  return act();
}

// KEY: through the fields, then Save and Cancel, then the first field again.
// BOOT: +1 on a field, or the marked button.
Action Menu::handle_editor(Input in) {
  const bool dt = screen_ == Screen::DateTime;
  if (in == Input::Back) return back();
  if (in == Input::Move) {
    if (button_ == EditorButton::Save) {
      button_ = EditorButton::Cancel;
    } else if (button_ == EditorButton::Cancel) {
      button_ = EditorButton::None;
      if (dt) {
        field_ = Field::Year;
      } else {
        edit_.first_field();
      }
    } else if (dt && static_cast<int>(field_) + 1 < kFieldCount) {
      field_ = static_cast<Field>(static_cast<int>(field_) + 1);
    } else if (dt || !edit_.next_field()) {
      button_ = EditorButton::Save;
    }
    return Action::None;
  }
  if (button_ == EditorButton::Cancel) {
    if (dt && first_time_) {  // first start: leaves the "Time not set" face
      show(Screen::Closed, 0);
      return Action::None;
    }
    return back();
  }
  if (button_ == EditorButton::Save) {
    if (!dt) return save_edit();
    show(Screen::Closed, 0);
    return Action::SaveDateTime;
  }
  if (dt) {
    increment(value_, field_);
    return Action::None;
  }
  edit_.increment();
  return edit_.field() == AlarmField::Volume ? Action::PlayVolume : Action::None;
}

// KEY held, a Back / Close row, or an editor's Cancel: one level up, nothing saved.
Action Menu::back() {
  switch (screen_) {
    case Screen::Main:
      show(Screen::Closed, 0);
      break;
    case Screen::DateTime:
    case Screen::Timezone:
      if (first_time_) {  // KEY held 5 s on the first-start screens
        show(Screen::Closed, 0);
        return Action::WifiSetup;
      }
      show(datetime_alone_ ? Screen::Closed : Screen::Main, main_sel_);
      break;
    case Screen::Wifi:
    case Screen::Alarms:
      show(Screen::Main, main_sel_);
      break;
    case Screen::WifiForget:
      show(Screen::Wifi, 1);
      break;
    case Screen::AlarmItem:
      show(Screen::Alarms, alarm_);
      break;
    case Screen::AlarmEdit:
      if (adding_) {
        show(Screen::Alarms, ctx_.alarms.size());  // back on "Add alarm"
      } else {
        show(Screen::AlarmItem, kAlarmEdit);
      }
      break;
    case Screen::AlarmDelete:
      show(Screen::AlarmItem, kAlarmDelete);
      break;
    case Screen::Closed:
      break;
  }
  return Action::None;
}

// BOOT on a list item (not the Back row).
Action Menu::act() {
  switch (screen_) {
    case Screen::Main:
      main_sel_ = sel_;
      if (sel_ == kItemDateTime) {
        start_datetime();
      } else if (sel_ == kItemAlarms) {
        show(Screen::Alarms, 0);
      } else if (sel_ == kItemTimezone) {
        show(Screen::Timezone, has_custom() ? 0 : static_cast<size_t>(ctx_.tz_preset));
      } else {
        show(Screen::Wifi, 0);
      }
      return Action::None;

    case Screen::Timezone: {
      const size_t pick = sel_;
      if (first_time_) {
        start_datetime();  // on to the date & time editor
      } else {
        show(Screen::Main, main_sel_);
      }
      if (has_custom() && pick == 0) return Action::None;  // keeps the custom zone
      const size_t preset = pick - (has_custom() ? 1 : 0);
      if (static_cast<int>(preset) == ctx_.tz_preset) return Action::None;  // no change
      ctx_.tz_preset = static_cast<int>(preset);
      tz_choice_ = preset;
      return Action::SaveTimezone;
    }

    case Screen::Wifi:
      if (sel_ == 0) {
        show(Screen::Closed, 0);
        return Action::WifiSetup;
      }
      show(Screen::WifiForget, 0);  // Cancel first: a stray press forgets nothing
      return Action::None;

    case Screen::WifiForget:
      if (sel_ == 0) return back();
      ctx_.wifi_saved = false;
      show(Screen::Main, main_sel_);
      return Action::WifiForget;

    case Screen::Alarms:
      if (sel_ == ctx_.alarms.size()) {  // Add alarm
        adding_ = true;
        edit_ = AlarmEdit();
        show(Screen::AlarmEdit, 0);
      } else {
        alarm_ = sel_;
        show(Screen::AlarmItem, 0);
      }
      return Action::None;

    case Screen::AlarmItem:
      if (sel_ == kAlarmEdit) {
        adding_ = false;
        edit_ = AlarmEdit(ctx_.alarms[alarm_]);
        show(Screen::AlarmEdit, 0);
      } else if (sel_ == kAlarmToggle) {
        ctx_.alarms[alarm_].enabled = !ctx_.alarms[alarm_].enabled;
        return Action::SaveAlarms;
      } else {
        show(Screen::AlarmDelete, 0);  // Cancel first: a stray press deletes nothing
      }
      return Action::None;

    case Screen::AlarmDelete: {
      if (sel_ == 0) return back();
      ctx_.alarms.erase(ctx_.alarms.begin() + static_cast<std::ptrdiff_t>(alarm_));
      show(Screen::Alarms, 0);
      const size_t n = count() - 1;  // rows before Back; "Add alarm" may be back
      sel_ = alarm_ < n ? alarm_ : n - 1;
      return Action::SaveAlarms;
    }

    default:
      return Action::None;
  }
}

// Save in the alarm editor: a new alarm is added at the end, an edited one
// keeps its id, label and sound. Saving turns the alarm on.
Action Menu::save_edit() {
  if (adding_) {
    if (!can_add()) {  // cannot happen: "Add alarm" is hidden when full
      show(Screen::Alarms, 0);
      return Action::None;
    }
    Alarm a;
    a.id = sched::free_id(ctx_.alarms);
    ctx_.alarms.push_back(a);
    alarm_ = ctx_.alarms.size() - 1;
  }
  Alarm& a = ctx_.alarms[alarm_];
  a.hour = edit_.hour();
  a.minute = edit_.minute();
  a.days = edit_.days();
  a.volume = edit_.volume();
  a.ramp = edit_.ramp();
  a.enabled = true;
  adding_ = false;
  show(Screen::Alarms, alarm_);
  return Action::SaveAlarms;
}

}  // namespace inkwake::menu
