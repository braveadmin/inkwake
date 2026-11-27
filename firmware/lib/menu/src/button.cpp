// SPDX-License-Identifier: GPL-3.0-only
#include "button.h"

namespace inkwake::menu {

namespace {

// a - b >= 0 on a wrapping millisecond counter.
bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

}  // namespace

Gesture Button::update(bool down, uint32_t now_ms) {
  if (down != raw_) {
    raw_ = down;
    raw_since_ = now_ms;
  }
  if (raw_ != stable_ && now_ms - raw_since_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      press_ms_ = raw_since_;
      next_repeat_ = press_ms_ + c_.repeat_delay_ms;
      held_ = false;
      ignored_ = false;
      return Gesture::Press;
    }
    const bool ignored = ignored_;
    ignored_ = false;
    if (ignored) return Gesture::None;
    return held_ ? Gesture::Release : Gesture::Tap;
  }
  // Up but not yet debounced: a release is coming, so no Hold or Repeat.
  if (!stable_ || !raw_ || ignored_) return Gesture::None;
  if (c_.hold_ms != 0 && !held_ && now_ms - press_ms_ >= c_.hold_ms) {
    held_ = true;
    return Gesture::Hold;
  }
  if (c_.repeat_delay_ms != 0 && reached(now_ms, next_repeat_)) {
    next_repeat_ += c_.repeat_ms != 0 ? c_.repeat_ms : c_.repeat_delay_ms;
    // Fell far behind (the caller was busy): no burst of catch-up repeats.
    if (reached(now_ms, next_repeat_)) next_repeat_ = now_ms + c_.repeat_ms;
    return Gesture::Repeat;
  }
  return Gesture::None;
}

void Button::pressed_since(uint32_t since_ms) {
  raw_ = true;
  raw_since_ = since_ms;
  stable_ = true;
  press_ms_ = since_ms;
  next_repeat_ = since_ms + c_.repeat_delay_ms;
  held_ = false;
  ignored_ = false;
}

int setup_hold_steps(uint32_t held_ms) {
  if (held_ms < kMenuHoldMs) return -1;
  const uint32_t steps = (held_ms - kMenuHoldMs) / kHoldStepMs;
  return steps >= static_cast<uint32_t>(kHoldSteps) ? kHoldSteps : static_cast<int>(steps);
}

void Button::ignore_until_release() {
  if (stable_) ignored_ = true;
}

}  // namespace inkwake::menu
