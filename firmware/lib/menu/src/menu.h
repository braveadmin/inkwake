// SPDX-License-Identifier: GPL-3.0-only
// Button menu navigation (#41, ADR-0018): which screen shows what, and what a
// button press does there. Pure state: main.cpp turns presses into Inputs,
// draws the screen and carries out the returned Action.
//
//   Main list:   Date & time, Timezone, Wi-Fi
//   Date & time: year, month, day, hour, minute; the last field saves
//   Timezone:    preset list (plus "Custom" while a zone not in it is set)
//   Wi-Fi:       Set up Wi-Fi, Forget all networks (only with one saved)
//   Forget:      Cancel, Forget all  (confirmation, Cancel first)
#pragma once

#include <cstddef>
#include <cstdint>

#include "datetime.h"

namespace inkwake::menu {

enum class Input : uint8_t {
  Next,    // BOOT press or repeat: next item, or +1 on the marked field
  Select,  // KEY tap: open, next field, save
  Back,    // KEY held: back without saving
};

enum class Screen : uint8_t { Closed, Main, DateTime, Timezone, Wifi, WifiForget };

enum class Action : uint8_t {
  None,          // redraw if the screen changed
  SaveDateTime,  // value() is the local time to set
  SaveTimezone,  // tz_choice() is the preset to save
  WifiSetup,     // open the Wi-Fi setup page; the menu is closed
  WifiForget,    // forget all networks; the menu is back on the main list
};

struct Context {
  DateTime now;        // editor start value, local time
  int tz_preset = -1;  // current zone: index into kTzPresets, -1 = custom
  bool wifi_saved = false;
};

class Menu {
 public:
  // Opens the main list.
  void open(const Context& c);
  // Opens the date & time editor alone ("Time not set"): saving or going
  // back closes the menu.
  void open_datetime(const Context& c);

  Action handle(Input in);

  Screen screen() const { return screen_; }
  bool closed() const { return screen_ == Screen::Closed; }
  const char* title() const;
  const char* hint() const;  // button help for the bottom line

  // List screens (Main, Timezone, Wifi, WifiForget).
  size_t count() const;
  size_t selected() const { return sel_; }
  const char* item(size_t i) const;

  // Date & time editor.
  const DateTime& value() const { return value_; }
  Field field() const { return field_; }

  // After SaveTimezone: index into kTzPresets.
  size_t tz_choice() const { return tz_choice_; }

 private:
  void show(Screen s, size_t sel);
  bool has_custom() const { return ctx_.tz_preset < 0; }

  Context ctx_;
  Screen screen_ = Screen::Closed;
  size_t sel_ = 0;
  size_t main_sel_ = 0;  // kept while a sub-screen is open
  DateTime value_;
  Field field_ = Field::Year;
  bool datetime_alone_ = false;
  size_t tz_choice_ = 0;
};

}  // namespace inkwake::menu
