// SPDX-License-Identifier: GPL-3.0-only
#include "room_temp.h"

#include <Arduino.h>
#include <Wire.h>

namespace inkwake::room_temp {

namespace {

uint32_t g_started_us = 0;

bool command(uint16_t cmd) {
  uint8_t b[2];
  shtc3::command_bytes(cmd, b);
  Wire.beginTransmission(shtc3::kAddress);
  Wire.write(b, sizeof b);
  return Wire.endTransmission() == 0;
}

bool read_bytes(uint8_t* buf, size_t n) {
  if (Wire.requestFrom(static_cast<uint16_t>(shtc3::kAddress), n, true) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

// The wakeup command is acknowledged in sleep; one retry covers a bus glitch.
bool wake() {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (command(shtc3::kCmdWakeup)) {
      delayMicroseconds(shtc3::kWakeupUs);
      return true;
    }
    delayMicroseconds(shtc3::kWakeupUs);
  }
  return false;
}

}  // namespace

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok:
      return "ok";
    case Status::NoDevice:
      return "no answer on I2C";
    case Status::BadData:
      return "bad CRC or unexpected ID";
  }
  return "?";
}

Status start() {
  if (!wake() || !command(shtc3::kCmdMeasureNormal)) return Status::NoDevice;
  g_started_us = micros();
  return Status::Ok;
}

Status finish(shtc3::Reading& r) {
  const uint32_t elapsed = micros() - g_started_us;
  if (elapsed < shtc3::kMeasureNormalUs) delayMicroseconds(shtc3::kMeasureNormalUs - elapsed);
  // Without clock stretching the sensor NACKs the read until the result is
  // ready; the datasheet time is a maximum, so this rarely retries.
  uint8_t buf[shtc3::kMeasureBytes];
  bool got = false;
  for (int attempt = 0; attempt < 3 && !got; attempt++) {
    if (attempt > 0) delay(1);
    got = read_bytes(buf, sizeof buf);
  }
  command(shtc3::kCmdSleep);
  if (!got) return Status::NoDevice;
  return shtc3::decode_measurement(buf, r) ? Status::Ok : Status::BadData;
}

Status read_id(uint16_t& id) {
  if (!wake() || !command(shtc3::kCmdReadId)) return Status::NoDevice;
  uint8_t buf[shtc3::kIdBytes];
  const bool got = read_bytes(buf, sizeof buf);
  command(shtc3::kCmdSleep);
  if (!got) return Status::NoDevice;
  return shtc3::decode_id(buf, id) ? Status::Ok : Status::BadData;
}

}  // namespace inkwake::room_temp
