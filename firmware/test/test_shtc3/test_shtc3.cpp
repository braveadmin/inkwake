// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include "shtc3.h"

using namespace inkwake::shtc3;

void setUp() {}
void tearDown() {}

void test_crc_datasheet_examples() {
  const uint8_t beef[] = {0xBE, 0xEF};  // SHTC3 datasheet, CRC example
  TEST_ASSERT_EQUAL_HEX8(0x92, crc8(beef, 2));
  const uint8_t zero[] = {0x00, 0x00};  // Sensirion reference value
  TEST_ASSERT_EQUAL_HEX8(0x81, crc8(zero, 2));
}

void test_command_bytes_msb_first() {
  uint8_t b[2] = {};
  command_bytes(kCmdMeasureNormal, b);
  TEST_ASSERT_EQUAL_HEX8(0x78, b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x66, b[1]);
  command_bytes(kCmdWakeup, b);
  TEST_ASSERT_EQUAL_HEX8(0x35, b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x17, b[1]);
}

void test_temperature_conversion() {
  TEST_ASSERT_EQUAL_INT16(-450, temp_dc_from_raw(0x0000));  // -45.0 C
  TEST_ASSERT_EQUAL_INT16(1300, temp_dc_from_raw(0xFFFF));  // 130.0 C (175 * 65535/65536 - 45)
  TEST_ASSERT_EQUAL_INT16(425, temp_dc_from_raw(0x8000));   // -45 + 87.5
  TEST_ASSERT_EQUAL_INT16(250, temp_dc_from_raw(0x6666));   // 25.0 C
  TEST_ASSERT_EQUAL_INT16(-30, temp_dc_from_raw(0x3D71));   // -2.95 C rounds to -3.0
}

void test_humidity_conversion() {
  TEST_ASSERT_EQUAL_UINT16(0, rh_dpct_from_raw(0x0000));
  TEST_ASSERT_EQUAL_UINT16(1000, rh_dpct_from_raw(0xFFFF));
  TEST_ASSERT_EQUAL_UINT16(500, rh_dpct_from_raw(0x8000));
  TEST_ASSERT_EQUAL_UINT16(400, rh_dpct_from_raw(0x6666));
}

void test_measurement_decodes() {
  const uint8_t buf[kMeasureBytes] = {0x66, 0x66, 0x93, 0x80, 0x00, 0xA2};
  Reading r{};
  TEST_ASSERT_TRUE(decode_measurement(buf, r));
  TEST_ASSERT_EQUAL_INT16(250, r.temp_dc);
  TEST_ASSERT_EQUAL_UINT16(500, r.rh_dpct);
}

void test_measurement_bad_crc_rejected() {
  const uint8_t good[kMeasureBytes] = {0x66, 0x66, 0x93, 0x80, 0x00, 0xA2};
  for (size_t i = 0; i < kMeasureBytes; ++i) {
    uint8_t buf[kMeasureBytes];
    for (size_t j = 0; j < kMeasureBytes; ++j) buf[j] = good[j];
    buf[i] ^= 0x01;  // one flipped bit anywhere fails a CRC
    Reading r{123, 456};
    TEST_ASSERT_FALSE(decode_measurement(buf, r));
    TEST_ASSERT_EQUAL_INT16(123, r.temp_dc);
    TEST_ASSERT_EQUAL_UINT16(456, r.rh_dpct);
  }
}

void test_id() {
  uint16_t id = 0;
  const uint8_t base[kIdBytes] = {0x08, 0x07, 0x21};
  TEST_ASSERT_TRUE(decode_id(base, id));
  TEST_ASSERT_EQUAL_HEX16(0x0807, id);
  const uint8_t other_bits[kIdBytes] = {0x08, 0x87, 0x5B};  // bits outside the mask are free
  TEST_ASSERT_TRUE(decode_id(other_bits, id));
  TEST_ASSERT_EQUAL_HEX16(0x0887, id);
  const uint8_t wrong_product[kIdBytes] = {0x08, 0x06, 0x10};
  TEST_ASSERT_FALSE(decode_id(wrong_product, id));
  const uint8_t bad_crc[kIdBytes] = {0x08, 0x07, 0x22};
  TEST_ASSERT_FALSE(decode_id(bad_crc, id));
}

void test_offset() {
  TEST_ASSERT_EQUAL_INT16(215, apply_offset(250, -35));
  TEST_ASSERT_EQUAL_INT16(250, apply_offset(250, 0));
  TEST_ASSERT_EQUAL_INT16(-20, apply_offset(10, -30));   // crosses zero
  TEST_ASSERT_EQUAL_INT16(350, apply_offset(250, 500));  // clamped to +10.0
  TEST_ASSERT_EQUAL_INT16(150, apply_offset(250, -500));
  TEST_ASSERT_TRUE(offset_valid(kMaxOffsetDc));
  TEST_ASSERT_TRUE(offset_valid(-kMaxOffsetDc));
  TEST_ASSERT_FALSE(offset_valid(kMaxOffsetDc + 1));
  TEST_ASSERT_FALSE(offset_valid(-kMaxOffsetDc - 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_datasheet_examples);
  RUN_TEST(test_command_bytes_msb_first);
  RUN_TEST(test_temperature_conversion);
  RUN_TEST(test_humidity_conversion);
  RUN_TEST(test_measurement_decodes);
  RUN_TEST(test_measurement_bad_crc_rejected);
  RUN_TEST(test_id);
  RUN_TEST(test_offset);
  return UNITY_END();
}
