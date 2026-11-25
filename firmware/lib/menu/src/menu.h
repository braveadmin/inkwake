// SPDX-License-Identifier: GPL-3.0-only
// Button menu navigation (#41, ADR-0018): which screen shows what, and what a
// button press does there. Pure state: main.cpp turns presses into Inputs,
// draws the screen and carries out the returned Action.
//
//   Main list:   Date & time, Alarms, Timezone, Wi-Fi
//   Date & time: year, month, day, hour, minute; the last field saves
//   Alarms:      one row per alarm, then "Add alarm" (hidden at kMaxAlarms)
//   Alarm:       Edit, Turn off / Turn on, Delete
//   Alarm editor: hour, minute, days (+ Mon..Sun with Custom); the last field saves
//   Delete:      Cancel, Delete  (confirmation, Cancel first)
//   Timezone:    preset list (plus "Custom" while a zone not in it is set)
//   Wi-Fi:       Set up Wi-Fi, Forget all networks (only with one saved)
//   Forget:      Cancel, Forget all  (confirmation, Cancel first)
//
// First-time setup (#34): timezone, then the date & time editor, with no way
// back; KEY held 5 s opens Wi-Fi setup instead (a sync then sets the time).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "alarm.h"
#include "alarm_edit.h"
#include "alarm_sched.h"
#include "button.h"
#include "datetime.h"
#include "tz_presets.h"

namespace inkwake::menu {

enum class Input : uint8_t {
  Next,    // BOOT press or repeat: next item, or +1 on the marked field
  Select,  // KEY tap: open, next field, save
  Back,    // KEY held: back without saving
};

enum class Screen : uint8_t {
  Closed,
  Main,
  DateTime,
  Timezone,
  Wifi,
  WifiForget,
  Alarms,
  AlarmItem,
  AlarmEdit,
  AlarmDelete,
};

// Longest list: the timezones (plus "Custom") or the alarms (plus "Add alarm").
constexpr size_t kMaxListItems =
    kTzPresetCount + 1 > sched::kMaxAlarms + 1 ? kTzPresetCount + 1 : sched::kMaxAlarms + 1;
constexpr size_t kItemLen = 32;  // alarm rows and titles, including the NUL

enum class Action : uint8_t {
  None,          // redraw if the screen changed
  SaveDateTime,  // value() is the local time to set
  SaveTimezone,  // tz_choice() is the preset to save
  WifiSetup,     // open the Wi-Fi setup page; the menu is closed
  WifiForget,    // forget all networks; the menu is back on the main list
  SaveAlarms,    // alarms() is the whole list to save (added, edited, toggled or deleted)
};

struct Context {
  DateTime now;        // editor start value, local time
  int tz_preset = -1;  // current zone: index into kTzPresets, -1 = custom
  bool wifi_saved = false;
  std::vector<Alarm> alarms;  // as saved; the menu edits a copy
};

class Menu {
 public:
  // Opens the main list.
  void open(const Context& c);
  // Opens the date & time editor alone ("Time not set"): saving or going
  // back closes the menu.
  void open_datetime(const Context& c);
  // First-time setup: the timezone list, then the date & time editor. KEY
  // held 5 s (back_hold_ms()) asks for Wi-Fi setup; the last field saves.
  void open_first_time(const Context& c);

  // How long KEY must be held for Input::Back on this menu.
  uint32_t back_hold_ms() const { return first_time_ ? kSetupHoldMs : kBackHoldMs; }
  bool first_time() const { return first_time_; }

  Action handle(Input in);

  Screen screen() const { return screen_; }
  bool closed() const { return screen_ == Screen::Closed; }
  const char* title() const;
  // What the buttons do here, drawn next to each button on the panel's top edge.
  const char* key_label() const;       // KEY tap, e.g. "Open"
  const char* key_hold_label() const;  // KEY held, e.g. "hold: back"
  const char* boot_label() const;      // BOOT, e.g. "Next" or "+1"

  // List screens (Main, Timezone, Wifi, WifiForget).
  size_t count() const;
  size_t selected() const { return sel_; }
  const char* item(size_t i) const;

  // Date & time editor.
  const DateTime& value() const { return value_; }
  Field field() const { return field_; }

  // After SaveTimezone: index into kTzPresets.
  size_t tz_choice() const { return tz_choice_; }

  // Alarm editor.
  const AlarmEdit& alarm_edit() const { return edit_; }
  // After SaveAlarms: the list to save.
  const std::vector<Alarm>& alarms() const { return ctx_.alarms; }

 private:
  void show(Screen s, size_t sel);
  bool has_custom() const { return ctx_.tz_preset < 0; }
  bool can_add() const { return ctx_.alarms.size() < sched::kMaxAlarms; }
  Action handle_alarms(Input in);
  Action save_edit();

  Context ctx_;
  Screen screen_ = Screen::Closed;
  size_t sel_ = 0;
  size_t main_sel_ = 0;  // kept while a sub-screen is open
  DateTime value_;
  Field field_ = Field::Year;
  bool datetime_alone_ = false;
  bool first_time_ = false;
  size_t tz_choice_ = 0;
  size_t alarm_ = 0;     // alarm open in AlarmItem / AlarmEdit / AlarmDelete
  bool adding_ = false;  // AlarmEdit on a new alarm
  AlarmEdit edit_;
  mutable char row_[kMaxListItems][kItemLen] = {};  // formatted alarm rows
  mutable char title_[kItemLen] = {};
};

}  // namespace inkwake::menu
