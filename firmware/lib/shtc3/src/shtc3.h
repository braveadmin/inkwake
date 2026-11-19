// SPDX-License-Identifier: GPL-3.0-only
// SHTC3 temperature/humidity sensor: command words, CRC check and the
// hardware-free conversion of raw readings. The I2C transport will live in
// firmware/src (follow-up to #12). Datasheet: Sensirion SHTC3, version 4.
//
// Sequence per reading: wakeup, wait kWakeupUs, measure command, wait the
// measurement time, read kMeasureBytes, sleep. Without clock stretching the
// sensor NACKs a read until the result is ready.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::shtc3 {

constexpr uint8_t kAddress = 0x70;  // 7-bit I2C address

// 16-bit commands, sent most significant byte first (command_bytes()).
constexpr uint16_t kCmdWakeup = 0x3517;
constexpr uint16_t kCmdSleep = 0xB098;
constexpr uint16_t kCmdSoftReset = 0x805D;
constexpr uint16_t kCmdReadId = 0xEFC8;
// Measurements without clock stretching, temperature first.
constexpr uint16_t kCmdMeasureNormal = 0x7866;
constexpr uint16_t kCmdMeasureLowPower = 0x609C;

// Datasheet maxima, in microseconds.
constexpr uint32_t kWakeupUs = 240;
constexpr uint32_t kMeasureNormalUs = 12100;
constexpr uint32_t kMeasureLowPowerUs = 800;

constexpr size_t kIdBytes = 3;       // id msb, id lsb, crc
constexpr size_t kMeasureBytes = 6;  // T msb, T lsb, crc, RH msb, RH lsb, crc

// Calibration offset (F11, ADR-0012): tenths of a degree C added to every
// reading, from config. Board heat makes the sensor read high, so the usual
// value is negative.
constexpr int16_t kMaxOffsetDc = 100;  // +-10.0 C

struct Reading {
  int16_t temp_dc;   // tenths of a degree C, -450..1750
  uint16_t rh_dpct;  // tenths of a percent RH, 0..1000
};

void command_bytes(uint16_t cmd, uint8_t out[2]);

// CRC-8, polynomial 0x31, init 0xFF, no reflection, no final XOR.
uint8_t crc8(const uint8_t* data, size_t n);

// Raw 16-bit sensor words to tenths, rounded to nearest.
int16_t temp_dc_from_raw(uint16_t raw);
uint16_t rh_dpct_from_raw(uint16_t raw);

// Read-ID response. False on a CRC mismatch or if the product code bits
// (11 and 5:0) do not identify an SHTC3.
bool decode_id(const uint8_t buf[kIdBytes], uint16_t& id);

// Measurement response, temperature first. False if either CRC is wrong;
// `out` is then left untouched.
bool decode_measurement(const uint8_t buf[kMeasureBytes], Reading& out);

bool offset_valid(int offset_dc);
// Reading plus the calibration offset. The offset is clamped to
// +-kMaxOffsetDc, so a bad config value cannot move the reading far.
int16_t apply_offset(int16_t temp_dc, int offset_dc);

}  // namespace inkwake::shtc3
