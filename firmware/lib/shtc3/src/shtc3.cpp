// SPDX-License-Identifier: GPL-3.0-only
#include "shtc3.h"

namespace inkwake::shtc3 {

namespace {

constexpr uint16_t kIdMask = 0x083F;  // bits 11 and 5:0
constexpr uint16_t kIdSHTC3 = 0x0807;

uint16_t word(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

bool crc_ok(const uint8_t* p) { return crc8(p, 2) == p[2]; }

}  // namespace

void command_bytes(uint16_t cmd, uint8_t out[2]) {
  out[0] = static_cast<uint8_t>(cmd >> 8);
  out[1] = static_cast<uint8_t>(cmd & 0xFF);
}

uint8_t crc8(const uint8_t* data, size_t n) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

// T = -45 + 175 * raw / 2^16 C; in tenths, the scaled term is 0..1750 and is
// rounded before the (exact) offset so negative values round the same way.
int16_t temp_dc_from_raw(uint16_t raw) {
  const uint32_t scaled = (1750u * raw + 32768u) >> 16;
  return static_cast<int16_t>(static_cast<int32_t>(scaled) - 450);
}

// RH = 100 * raw / 2^16 %.
uint16_t rh_dpct_from_raw(uint16_t raw) {
  return static_cast<uint16_t>((1000u * raw + 32768u) >> 16);
}

bool decode_id(const uint8_t buf[kIdBytes], uint16_t& id) {
  if (!crc_ok(buf)) return false;
  const uint16_t v = word(buf);
  if ((v & kIdMask) != kIdSHTC3) return false;
  id = v;
  return true;
}

bool decode_measurement(const uint8_t buf[kMeasureBytes], Reading& out) {
  if (!crc_ok(buf) || !crc_ok(buf + 3)) return false;
  out.temp_dc = temp_dc_from_raw(word(buf));
  out.rh_dpct = rh_dpct_from_raw(word(buf + 3));
  return true;
}

bool offset_valid(int offset_dc) { return offset_dc >= -kMaxOffsetDc && offset_dc <= kMaxOffsetDc; }

int16_t apply_offset(int16_t temp_dc, int offset_dc) {
  if (offset_dc > kMaxOffsetDc) offset_dc = kMaxOffsetDc;
  if (offset_dc < -kMaxOffsetDc) offset_dc = -kMaxOffsetDc;
  return static_cast<int16_t>(temp_dc + offset_dc);
}

}  // namespace inkwake::shtc3
