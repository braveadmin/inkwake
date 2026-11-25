// SPDX-License-Identifier: GPL-3.0-only
// Button gestures from a polled level (#41): debounce, tap, hold, auto-repeat.
// Pure C++17, no Arduino; the caller reads the pin and passes millis().
#pragma once

#include <cstdint>

namespace inkwake::menu {

// KEY and BOOT timings (ADR-0018). To confirm on the board.
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kMenuHoldMs = 2'000;   // KEY on the face: opens the menu on release
constexpr uint32_t kSetupHoldMs = 5'000;  // KEY on the face: Wi-Fi setup
constexpr uint32_t kBackHoldMs = 1'000;   // KEY in the menu: back
constexpr uint32_t kRepeatDelayMs = 500;  // BOOT held: first repeat
constexpr uint32_t kRepeatMs = 150;       // BOOT held: then every
constexpr uint32_t kIdleMs = 60'000;      // menu closes without saving

enum class Gesture : uint8_t {
  None,
  Press,    // went down (debounced)
  Tap,      // released before hold_ms
  Hold,     // held for hold_ms; reported once, while still down
  Release,  // released after a Hold
  Repeat,   // still down: every repeat_ms after repeat_delay_ms
};

struct ButtonConfig {
  uint32_t hold_ms = 0;          // 0 = no Hold: every release is a Tap
  uint32_t repeat_delay_ms = 0;  // 0 = no Repeat
  uint32_t repeat_ms = 0;
};

class Button {
 public:
  explicit Button(const ButtonConfig& c) : c_(c) {}

  // Call every few ms with the pin level (true = pressed). At most one gesture per call.
  Gesture update(bool down, uint32_t now_ms);

  // Starts in the pressed state, pressed at `since_ms` (the press that woke the
  // board). Reports no Press for it.
  void pressed_since(uint32_t since_ms);

  // Ignores the press in progress until it is released: no Tap, Hold or Repeat.
  void ignore_until_release();

  bool down() const { return stable_; }
  // How long the debounced press has lasted; 0 when up.
  uint32_t held_ms(uint32_t now_ms) const { return stable_ ? now_ms - press_ms_ : 0; }

 private:
  ButtonConfig c_;
  bool raw_ = false;
  uint32_t raw_since_ = 0;
  bool stable_ = false;
  uint32_t press_ms_ = 0;
  uint32_t next_repeat_ = 0;
  bool held_ = false;
  bool ignored_ = false;
};

}  // namespace inkwake::menu
