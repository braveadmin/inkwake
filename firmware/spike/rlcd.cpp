// SPDX-License-Identifier: GPL-3.0-only
#include "rlcd.h"

#include <driver/gpio.h>
#include <soc/soc_caps.h>

namespace spike {

namespace {
constexpr uint32_t kSpiHz = 10'000'000;  // Waveshare's esp_lcd demo runs at 10 MHz
}

void Rlcd::begin(bool cold) {
  // Pins first, with idle levels, so releasing a deep-sleep hold causes no glitch.
  pinMode(kDc, OUTPUT);
  pinMode(kCs, OUTPUT);
  pinMode(kRst, OUTPUT);
  digitalWrite(kDc, HIGH);
  digitalWrite(kCs, HIGH);
  digitalWrite(kRst, HIGH);
  hold_pins(false);

  spi_.begin(kSck, -1, kMosi, -1);
  if (cold) {
    reset();
    init_regs();
    lpm_ = false;
  } else {
    lpm_ = true;  // the spike always parks the panel in LPM before deep sleep
  }
  clear(true);
}

void Rlcd::hold_pins(bool hold) {
  for (int p : {kDc, kCs, kRst}) {
    if (hold) {
      gpio_hold_en(static_cast<gpio_num_t>(p));
    } else {
      gpio_hold_dis(static_cast<gpio_num_t>(p));
    }
  }
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  if (hold) {
    gpio_deep_sleep_hold_en();
  } else {
    gpio_deep_sleep_hold_dis();
  }
#endif
}

void Rlcd::cmd(uint8_t c) { cmd_data(c, nullptr, 0); }

void Rlcd::cmd_data(uint8_t c, const uint8_t* d, size_t n) {
  spi_.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kCs, LOW);
  digitalWrite(kDc, LOW);
  spi_.transfer(c);
  if (n > 0) {
    digitalWrite(kDc, HIGH);
    spi_.writeBytes(d, n);
  }
  digitalWrite(kCs, HIGH);
  spi_.endTransaction();
}

void Rlcd::reset() {
  digitalWrite(kRst, HIGH);
  delay(50);
  digitalWrite(kRst, LOW);
  delay(20);
  digitalWrite(kRst, HIGH);
  delay(50);
}

void Rlcd::init_regs() {
  struct Reg {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[10];
  };
  static const Reg kPre[] = {
      {0xD6, 2, {0x17, 0x02}},  // NVM load control
      {0xD1, 1, {0x01}},        // booster enable
      {0xC0, 2, {0x11, 0x04}},  // gate voltage
      {0xC1, 4, {0x69, 0x69, 0x69, 0x69}},
      {0xC2, 4, {0x19, 0x19, 0x19, 0x19}},
      {0xC4, 4, {0x4B, 0x4B, 0x4B, 0x4B}},
      {0xC5, 4, {0x19, 0x19, 0x19, 0x19}},
      {0xD8, 2, {0x80, 0xE9}},
      {0xB2, 1, {0x02}},  // frame rate
      {0xB3, 10, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}},
      {0xB4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}},
      {0x62, 3, {0x32, 0x03, 0x1F}},
      {0xB7, 1, {0x13}},
      {0xB0, 1, {0x64}},
  };
  static const Reg kPost[] = {
      {0xC9, 1, {0x00}}, {0x36, 1, {0x48}},  // MADCTL
      {0x3A, 1, {0x11}},                     // pixel format
      {0xB9, 1, {0x20}}, {0xB8, 1, {0x29}},
  };
  for (const auto& r : kPre) cmd_data(r.cmd, r.data, r.len);
  cmd(0x11);  // sleep out
  delay(200);
  for (const auto& r : kPost) cmd_data(r.cmd, r.data, r.len);
  cmd(0x21);  // inversion on
  static const uint8_t kCol[] = {0x12, 0x2A};
  static const uint8_t kRow[] = {0x00, 0xC7};
  static const uint8_t kTe[] = {0x00};
  static const uint8_t kD0[] = {0xFF};
  cmd_data(0x2A, kCol, sizeof kCol);
  cmd_data(0x2B, kRow, sizeof kRow);
  cmd_data(0x35, kTe, sizeof kTe);  // tearing effect on
  cmd_data(0xD0, kD0, sizeof kD0);
  cmd(0x38);  // high power mode
  cmd(0x29);  // display on
}

void Rlcd::clear(bool white) { memset(buf_, white ? 0xFF : 0x00, kBufLen); }

// Landscape packing from Waveshare's RLCD_SetLandscapePixel: one byte holds
// 2 columns x 4 rows, bit set = white.
void Rlcd::pixel(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  const int inv_y = kHeight - 1 - y;
  const size_t index = static_cast<size_t>(x >> 1) * (kHeight / 4) + (inv_y >> 2);
  const uint8_t mask = 1u << (7 - (((inv_y & 3) << 1) | (x & 1)));
  if (black) {
    buf_[index] &= ~mask;
  } else {
    buf_[index] |= mask;
  }
}

void Rlcd::fill_rect(int x, int y, int w, int h, bool black) {
  for (int j = y; j < y + h; ++j) {
    for (int i = x; i < x + w; ++i) pixel(i, j, black);
  }
}

void Rlcd::flush() {
  static const uint8_t kCol[] = {0x12, 0x2A};
  static const uint8_t kRow[] = {0x00, 0xC7};
  cmd_data(0x2A, kCol, sizeof kCol);
  cmd_data(0x2B, kRow, sizeof kRow);
  cmd_data(0x2C, buf_, kBufLen);
}

void Rlcd::low_power(bool on) {
  cmd(on ? 0x39 : 0x38);
  lpm_ = on;
}

void Rlcd::sleep_in() {
  cmd(0x28);  // display off
  cmd(0x10);  // sleep in
  delay(120);
}

}  // namespace spike
