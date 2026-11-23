// SPDX-License-Identifier: GPL-3.0-only
// ST7305 reflective LCD on the Waveshare ESP32-S3-RLCD-4.2: SPI, init, power
// modes, partial refresh and status read-back. Init values from Waveshare's
// Apache-2.0 demo (github.com/waveshareteam/ESP32-S3-RLCD-4.2,
// 09_LVGL_V9_Test/display_bsp.cpp), as proven in the M0 spike. Timings follow
// the Sitronix ST7305 datasheet V0.2 (section numbers below). Address math
// lives in lib/display/panel_window.h, status decoding in panel_status.h.
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

  // How frames reach the panel (#25). The datasheet (§7.11) wants 300 ms after
  // switching LPM -> HPM and 100 ms after HPM -> LPM; switching on every
  // minute wake without those waits is the suspected cause of the panel
  // latching up.
  enum class WriteMode : uint8_t {
    LowPower,   // write RAM in LPM, no mode switch on minute wakes (default)
    HighPower,  // switch to HPM for the write and back, with the datasheet waits
  };

  // Waits longer than a few ms go through this, so the battery path can light
  // sleep instead of busy-waiting. Default: delay().
  using WaitFn = void (*)(uint32_t ms);
  void set_wait(WaitFn fn) { wait_ = fn; }
  void set_write_mode(WriteMode m) { mode_ = m; }

  // cold = true resets and initialises the panel, leaving the display off
  // until the first refresh has written a frame, so a restart shows no
  // garbage. After a deep-sleep wake with the pins held, pass false: the
  // panel kept its RAM and is parked in LPM.
  void begin(bool cold);

  // Writes the given regions (or the whole frame), turns the display on and
  // leaves the panel parked in low power mode (0x39), which keeps the image.
  void refresh(const Framebuffer& fb, const Rect* rects, size_t n);
  void refresh_full(const Framebuffer& fb);

  // RDDPM (0x0A, §8.1.5): the power-mode byte, decoded by panel_status. Read
  // over the bidirectional data line (§7.1.4); 0xFF when nothing answers.
  uint8_t read_power_mode();
  // RDDST (0x09, §8.1.4): 32 bits clocked out after the command, raw, so the
  // serial console can show whether a dummy bit precedes the 3 status bytes.
  uint32_t read_display_status();

  // Recovery for a latched controller: sleep in (stops the booster and the
  // oscillator, §8.1.6), hardware reset, full setup. Like begin(true), the
  // display stays off until the next refresh writes a frame.
  void recover();

  // Latch DC/CS/RST so the panel keeps its state through deep or light sleep.
  static void hold_pins(bool hold);

 private:
  void cmd(uint8_t c);
  void cmd_data(uint8_t c, const uint8_t* d, size_t n);
  void write_window(const Framebuffer& fb, const PanelWindow& w);
  void power_mode(bool low);
  void reset();
  void init_regs();
  void display_on();
  uint32_t read_bits(uint8_t c, int bits);

  SPIClass spi_{HSPI};
  WaitFn wait_ = [](uint32_t ms) { delay(ms); };
  WriteMode mode_ = WriteMode::LowPower;
  bool lpm_ = false;
  bool display_on_ = true;
};

}  // namespace inkwake
