// SPDX-License-Identifier: GPL-3.0-only
#include "rtc.h"

#include <Arduino.h>
#include <Wire.h>

#include "pcf85063.h"

namespace inkwake::rtc {

namespace p = pcf85063;

namespace {

bool write_regs(uint8_t reg, const uint8_t* data, size_t n) {
  Wire.beginTransmission(p::kAddress);
  Wire.write(reg);
  Wire.write(data, n);
  return Wire.endTransmission() == 0;
}

bool read_regs(uint8_t reg, uint8_t* data, size_t n) {
  Wire.beginTransmission(p::kAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint16_t>(p::kAddress), n, true) != n) return false;
  for (size_t i = 0; i < n; i++) data[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

}  // namespace

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok:
      return "ok";
    case Status::NoDevice:
      return "no answer on I2C";
    case Status::TimeLost:
      return "time lost (power loss or never set)";
  }
  return "?";
}

void begin() { Wire.begin(kSda, kScl, 400000); }

Status read(time_t& t) {
  uint8_t regs[p::kTimeRegs];
  if (!read_regs(p::kRegSeconds, regs, sizeof regs)) return Status::NoDevice;
  int64_t unix_seconds;
  if (!p::decode(regs, unix_seconds)) return Status::TimeLost;
  t = static_cast<time_t>(unix_seconds);
  return Status::Ok;
}

bool write(time_t t) {
  uint8_t regs[p::kTimeRegs];
  if (!p::encode(t, regs)) return false;
  uint8_t ctrl1;
  if (!read_regs(p::kRegControl1, &ctrl1, 1)) return false;
  // Keep only CAP_SEL: 24-hour mode, no test mode, no software reset.
  ctrl1 &= p::kControl1CapSel;
  const uint8_t stop = ctrl1 | p::kControl1Stop;
  return write_regs(p::kRegControl1, &stop, 1) && write_regs(p::kRegSeconds, regs, sizeof regs) &&
         write_regs(p::kRegControl1, &ctrl1, 1);
}

bool arm_minute_interrupt() {
  const uint8_t ctrl2 = p::kControl2MinuteInterrupt;
  return write_regs(p::kRegControl2, &ctrl2, 1);
}

}  // namespace inkwake::rtc
