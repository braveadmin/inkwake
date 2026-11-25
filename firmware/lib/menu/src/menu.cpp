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
  value_ = clamped(c.now);
  field_ = Field::Year;
  show(Screen::DateTime, 0);
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
}

size_t Menu::count() const {
  switch (screen_) {
    case Screen::Main:
      return kMainCount;
    case Screen::Timezone:
      return kTzPresetCount + (has_custom() ? 1 : 0);
    case Screen::Wifi:
      return ctx_.wifi_saved ? 2 : 1;
    case Screen::WifiForget:
      return 2;
    case Screen::Alarms:
      return ctx_.alarms.size() + (can_add() ? 1 : 0);
    case Screen::AlarmItem:
      return kAlarmItemCount;
    case Screen::AlarmDelete:
      return 2;
    default:
      return 0;
  }
}

const char* Menu::item(size_t i) const {
  if (i >= count()) return "";
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

const char* Menu::key_label() const {
  switch (screen_) {
    case Screen::Main:
      return "Open";
    case Screen::DateTime:
      return static_cast<int>(field_) == kFieldCount - 1 ? "Save" : "Next";
    case Screen::Timezone:
      return first_time_ ? "OK" : "Save";  // "Next" is BOOT's
    case Screen::Wifi:
    case Screen::WifiForget:
    case Screen::AlarmItem:
    case Screen::AlarmDelete:
      return "Choose";
    case Screen::Alarms:
      return "Open";
    case Screen::AlarmEdit:
      return edit_.last_field() ? "Save" : "Next";
    default:
      return "";
  }
}

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
  switch (screen_) {
    case Screen::Closed:
      return "";
    case Screen::DateTime:
      return "+1";
    case Screen::AlarmEdit:
      if (edit_.field() == AlarmField::Hour || edit_.field() == AlarmField::Minute) return "+1";
      return edit_.field() == AlarmField::Days ? "Change" : "On/off";
    default:
      return "Next";
  }
}

Action Menu::handle(Input in) {
  switch (screen_) {
    case Screen::Closed:
      return Action::None;

    case Screen::Main:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % kMainCount;
      } else if (in == Input::Back) {
        show(Screen::Closed, 0);
      } else {
        main_sel_ = sel_;
        if (sel_ == kItemDateTime) {
          value_ = clamped(ctx_.now);
          field_ = Field::Year;
          show(Screen::DateTime, 0);
        } else if (sel_ == kItemAlarms) {
          show(Screen::Alarms, 0);
        } else if (sel_ == kItemTimezone) {
          show(Screen::Timezone, has_custom() ? 0 : static_cast<size_t>(ctx_.tz_preset));
        } else {
          show(Screen::Wifi, 0);
        }
      }
      return Action::None;

    case Screen::DateTime:
      if (in == Input::Next) {
        increment(value_, field_);
      } else if (in == Input::Back && first_time_) {
        show(Screen::Closed, 0);
        return Action::WifiSetup;
      } else if (in == Input::Back) {
        show(datetime_alone_ ? Screen::Closed : Screen::Main, main_sel_);
      } else if (static_cast<int>(field_) + 1 < kFieldCount) {
        field_ = static_cast<Field>(static_cast<int>(field_) + 1);
      } else {
        show(Screen::Closed, 0);
        return Action::SaveDateTime;
      }
      return Action::None;

    case Screen::Timezone:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back && first_time_) {
        show(Screen::Closed, 0);
        return Action::WifiSetup;
      } else if (in == Input::Back) {
        show(Screen::Main, main_sel_);
      } else {
        const size_t pick = sel_;
        if (first_time_) {  // on to the date & time editor
          value_ = clamped(ctx_.now);
          field_ = Field::Year;
          show(Screen::DateTime, 0);
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
      return Action::None;

    case Screen::Wifi:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back) {
        show(Screen::Main, main_sel_);
      } else if (sel_ == 0) {
        show(Screen::Closed, 0);
        return Action::WifiSetup;
      } else {
        show(Screen::WifiForget, 0);  // Cancel first: a stray tap forgets nothing
      }
      return Action::None;

    case Screen::WifiForget:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back || sel_ == 0) {
        show(Screen::Wifi, 1);
      } else {
        ctx_.wifi_saved = false;
        show(Screen::Main, main_sel_);
        return Action::WifiForget;
      }
      return Action::None;

    case Screen::Alarms:
    case Screen::AlarmItem:
    case Screen::AlarmEdit:
    case Screen::AlarmDelete:
      return handle_alarms(in);
  }
  return Action::None;
}

Action Menu::handle_alarms(Input in) {
  switch (screen_) {
    case Screen::Alarms:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back) {
        show(Screen::Main, main_sel_);
      } else if (sel_ == ctx_.alarms.size()) {  // Add alarm
        adding_ = true;
        edit_ = AlarmEdit();
        show(Screen::AlarmEdit, 0);
      } else {
        alarm_ = sel_;
        show(Screen::AlarmItem, 0);
      }
      return Action::None;

    case Screen::AlarmItem:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back) {
        show(Screen::Alarms, alarm_);
      } else if (sel_ == kAlarmEdit) {
        adding_ = false;
        edit_ = AlarmEdit(ctx_.alarms[alarm_]);
        show(Screen::AlarmEdit, 0);
      } else if (sel_ == kAlarmToggle) {
        ctx_.alarms[alarm_].enabled = !ctx_.alarms[alarm_].enabled;
        return Action::SaveAlarms;
      } else {
        show(Screen::AlarmDelete, 0);  // Cancel first: a stray tap deletes nothing
      }
      return Action::None;

    case Screen::AlarmEdit:
      if (in == Input::Next) {
        edit_.increment();
      } else if (in == Input::Back) {
        if (adding_) {
          show(Screen::Alarms, ctx_.alarms.size());  // back on "Add alarm"
        } else {
          show(Screen::AlarmItem, kAlarmEdit);
        }
      } else if (!edit_.next_field()) {
        return save_edit();
      }
      return Action::None;

    case Screen::AlarmDelete:
      if (in == Input::Next) {
        sel_ = (sel_ + 1) % count();
      } else if (in == Input::Back || sel_ == 0) {
        show(Screen::AlarmItem, kAlarmDelete);
      } else {
        ctx_.alarms.erase(ctx_.alarms.begin() + static_cast<std::ptrdiff_t>(alarm_));
        const size_t n = count();  // the list may now show "Add alarm" again
        show(Screen::Alarms, alarm_ < n ? alarm_ : n - 1);
        return Action::SaveAlarms;
      }
      return Action::None;

    default:
      return Action::None;
  }
}

// Last field of the alarm editor: a new alarm is added at the end, an edited
// one keeps its id, label and sound. Saving turns the alarm on.
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
  a.enabled = true;
  adding_ = false;
  show(Screen::Alarms, alarm_);
  return Action::SaveAlarms;
}

}  // namespace inkwake::menu
