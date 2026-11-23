// SPDX-License-Identifier: GPL-3.0-only
// ST7305 power-mode status (RDDPM, command 0x0A; datasheet V0.2 §8.1.5), the
// hardware-free part of the panel health check (#25).
//
// The byte read back is BSTON LPM PTLON SLPOUT NORON DISON 0 0 (D7..D0). The
// two low bits are always 0, so a byte with either set was not driven by the
// controller (no answer on the data line, which reads 0xFF with the pull-up).
// After a hardware reset the controller reports 0x08 (only NORON).
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake {
namespace panel_status {

constexpr uint8_t kBooster = 0x80;    // BSTON: booster (panel drive voltages) on
constexpr uint8_t kLowPower = 0x40;   // LPM: low power mode
constexpr uint8_t kPartial = 0x20;    // PTLON
constexpr uint8_t kSleepOut = 0x10;   // SLPOUT: out of sleep, panel scanning
constexpr uint8_t kNormal = 0x08;     // NORON
constexpr uint8_t kDisplayOn = 0x04;  // DISON
constexpr uint8_t kAlwaysZero = 0x03;

// What a parked, working panel reports: booster on, LPM, sleep out, normal, display on.
constexpr uint8_t kParked = kBooster | kLowPower | kSleepOut | kNormal | kDisplayOn;  // 0xDC

enum class Health : uint8_t {
  Ok,        // booster on, out of sleep, display on
  Bad,       // answered, but something the image needs is off
  NoAnswer,  // the byte cannot have come from the controller
};

Health classify(uint8_t status);

// "booster on, low power, sleep out, normal, display on" for the serial console.
// Writes at most n bytes including the terminating NUL.
void describe(uint8_t status, char* out, size_t n);

}  // namespace panel_status
}  // namespace inkwake
