// SPDX-License-Identifier: GPL-3.0-only
// Minimal ST7305 driver for the Waveshare ESP32-S3-RLCD-4.2 (M0 spike only).
// Init values and pixel packing follow Waveshare's Apache-2.0 demo
// (github.com/waveshareteam/ESP32-S3-RLCD-4.2, examples/arduino/09_LVGL_V9_Test/display_bsp.cpp).
// Full-frame writes only; partial refresh is M2 work.
#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace spike {

class Rlcd {
 public:
  static constexpr int kWidth = 400;  // landscape
  static constexpr int kHeight = 300;
  static constexpr size_t kBufLen = kWidth * kHeight / 8;  // 15000 bytes

  // Pins from Waveshare's demo: SCK 11, MOSI 12, DC 5, CS 40, RST 41.
  static constexpr int kSck = 11, kMosi = 12, kDc = 5, kCs = 40, kRst = 41;

  // cold = true resets and initialises the panel. After a deep-sleep wake with the
  // pins held, pass false: the panel kept its state, only the SPI bus is set up again.
  void begin(bool cold);

  void clear(bool white = true);
  void pixel(int x, int y, bool black);
  void fill_rect(int x, int y, int w, int h, bool black);
  void flush();  // full frame, about 15 KB over SPI

  void low_power(bool on);  // 0x39 LPM / 0x38 HPM
  void sleep_in();          // display off + sleep in: the image is expected to vanish
  bool is_low_power() const { return lpm_; }

  // Latch DC/CS/RST so the panel keeps its state through deep sleep.
  static void hold_pins(bool hold);

 private:
  void cmd(uint8_t c);
  void cmd_data(uint8_t c, const uint8_t* d, size_t n);
  void reset();
  void init_regs();

  SPIClass spi_{HSPI};
  uint8_t buf_[kBufLen];
  bool lpm_ = false;
};

}  // namespace spike
