// SPDX-License-Identifier: GPL-3.0-only
// On-board PCF85063 over I2C (SDA 13, SCL 14, shared with the SHTC3) and its
// interrupt line on GPIO15. Register logic: lib/rtc/pcf85063.h.
#pragma once

#include <ctime>

namespace inkwake::rtc {

constexpr int kSda = 13, kScl = 14, kIntPin = 15;

enum class Status { Ok, NoDevice, TimeLost };
const char* status_name(Status s);

void begin();

// Reads the time (UTC). TimeLost: the chip lost power or was never set.
Status read(time_t& t);

// Sets the time and restarts the second at this moment, then reads it back.
// False if the chip did not take it: right after power-up the crystal needs up
// to ~2 s to start, and until then the oscillator-stopped flag will not clear.
bool write(time_t t);

// Enables the once-a-minute interrupt and releases INT (clears its flag). Must
// run before every deep sleep: INT stays low until this is written.
bool arm_minute_interrupt();

}  // namespace inkwake::rtc
