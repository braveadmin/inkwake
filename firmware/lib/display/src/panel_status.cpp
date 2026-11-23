// SPDX-License-Identifier: GPL-3.0-only
#include "panel_status.h"

#include <cstdio>

namespace inkwake {
namespace panel_status {

Health classify(uint8_t status) {
  if ((status & kAlwaysZero) != 0 || status == 0x00) return Health::NoAnswer;
  constexpr uint8_t kNeeded = kBooster | kSleepOut | kDisplayOn;
  return (status & kNeeded) == kNeeded ? Health::Ok : Health::Bad;
}

void describe(uint8_t status, char* out, size_t n) {
  if (n == 0) return;
  if (classify(status) == Health::NoAnswer) {
    snprintf(out, n, "no answer");
    return;
  }
  snprintf(out, n, "booster %s, %s, %s, %s, display %s", status & kBooster ? "on" : "OFF",
           status & kLowPower ? "low power" : "high power",
           status & kSleepOut ? "sleep out" : "SLEEP IN",
           status & kPartial ? "partial" : (status & kNormal ? "normal" : "scroll"),
           status & kDisplayOn ? "on" : "OFF");
}

}  // namespace panel_status
}  // namespace inkwake
