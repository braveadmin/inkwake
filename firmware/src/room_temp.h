// SPDX-License-Identifier: GPL-3.0-only
// On-board SHTC3 room temperature sensor on the shared I2C bus (SDA 13, SCL 14,
// started by rtc::begin()). Command words and conversion: lib/shtc3/shtc3.h.
//
// A measurement takes up to 12.1 ms, so it is split: start() early in a wake,
// finish() when the result is needed, with other work in between. The sensor
// sleeps between measurements (under 1 uA).
#pragma once

#include <cstdint>

#include "shtc3.h"

namespace inkwake::room_temp {

enum class Status : uint8_t { Ok, NoDevice, BadData };
const char* status_name(Status s);

// Wakes the sensor and starts a measurement (normal mode, no clock stretching).
Status start();

// Waits until the measurement started by start() is ready, reads it and puts
// the sensor back to sleep. Call only after start() returned Ok.
Status finish(shtc3::Reading& r);

// Reads the sensor's ID register (wakes it and puts it back to sleep).
Status read_id(uint16_t& id);

}  // namespace inkwake::room_temp
