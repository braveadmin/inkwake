// SPDX-License-Identifier: GPL-3.0-only
#include "battery.h"

namespace inkwake::battery {

Level classify(Level prev, uint16_t mv) {
  if (mv == 0) return prev;
  if (mv < kCriticalMv) return Level::Critical;
  if (prev == Level::Critical && mv < kCriticalMv + kHysteresisMv) return Level::Critical;
  if (mv < kLowMv) return Level::Low;
  if (prev != Level::Ok && mv < kLowMv + kHysteresisMv) return Level::Low;
  return Level::Ok;
}

uint16_t average_mv(const uint16_t* mv, size_t n) {
  if (n == 0) return 0;
  uint32_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += mv[i];
  return static_cast<uint16_t>((sum + n / 2) / n);
}

const char* level_name(Level l) {
  switch (l) {
    case Level::Low:
      return "low";
    case Level::Critical:
      return "critical";
    default:
      return "ok";
  }
}

}  // namespace inkwake::battery
