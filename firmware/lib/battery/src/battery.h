// SPDX-License-Identifier: GPL-3.0-only
// Battery level from the cell voltage (#36): ok, low or critical, with
// hysteresis so the face icon doesn't flicker at a threshold. Hardware-free.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::battery {

enum class Level : uint8_t { Ok = 0, Low = 1, Critical = 2 };

// Single Li-ion cell read with no load while the board sleeps. Starting
// points from the issue, to tune on the board with `bat`.
constexpr uint16_t kLowMv = 3500;
constexpr uint16_t kCriticalMv = 3300;
// A level clears only once the voltage is this far above its threshold
// (charging also lifts the reading to ~4.1 V, clearing it at once).
constexpr uint16_t kHysteresisMv = 100;

// New level from the previous one and a fresh reading. 0 mV (no reading)
// keeps the previous level.
Level classify(Level prev, uint16_t mv);

// Mean of n readings, 0 when n is 0.
uint16_t average_mv(const uint16_t* mv, size_t n);

const char* level_name(Level l);

}  // namespace inkwake::battery
