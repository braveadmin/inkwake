// SPDX-License-Identifier: GPL-3.0-only
// ST7305 reflective LCD on the Waveshare ESP32-S3-RLCD-4.2: SPI, init, power
// modes and partial refresh. Init values from Waveshare's Apache-2.0 demo
// (github.com/waveshareteam/ESP32-S3-RLCD-4.2, 09_LVGL_V9_Test/display_bsp.cpp),
// as proven in the M0 spike. Address math lives in lib/display/panel_window.h.
#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "framebuffer.h"
#include "panel_window.h"

namespace inkwake {

class St7305 {
 public:
  // Pins: SCK 11, MOSI 12, DC 5, CS 40, RST 41 (hardware/wiring.md).
  static constexpr int kSck = 11, kMosi = 12, kDc = 5, kCs = 40, kRst = 41;

  // cold = true resets and initialises the panel, leaving the display off
  // until the first refresh has written a frame, so a restart shows no
  // garbage. After a deep-sleep wake with the pins held, pass false: the
  // panel kept its RAM and is parked in LPM.
  void begin(bool cold);

  // Writes the given regions (or the whole frame) in high power mode, then
  // parks the panel in low power mode (0x39), which keeps the image.
  void refresh(const Framebuffer& fb, const Rect* rects, size_t n);

  // Re-sends the whole register setup (sleep out, display on included) without
  // the reset pulse or the post-sleep-out wait. Restores a controller that lost
  // its settings or got a stray command, without the visible redraw of a reset.
  // Ends in high power mode; the next refresh parks it in LPM.
  void reapply_setup();
  void refresh_full(const Framebuffer& fb);

  // Latch DC/CS/RST so the panel keeps its state through deep sleep.
  static void hold_pins(bool hold);

 private:
  void cmd(uint8_t c);
  void cmd_data(uint8_t c, const uint8_t* d, size_t n);
  void write_window(const Framebuffer& fb, const PanelWindow& w);
  void power_mode(bool low);
  void reset();
  void init_regs(bool wait);

  SPIClass spi_{HSPI};
  void display_on();

  bool lpm_ = false;
  bool display_on_ = true;
};

}  // namespace inkwake
