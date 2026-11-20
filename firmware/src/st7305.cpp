// SPDX-License-Identifier: GPL-3.0-only
#include "st7305.h"

#include <driver/gpio.h>
#include <soc/soc_caps.h>

namespace inkwake {

namespace {

constexpr uint32_t kSpiHz = 10'000'000;  // Waveshare's esp_lcd demo runs at 10 MHz

// Narrow the column window (0x2A) as well as the row window (0x2B) on partial
// writes. Column addresses are mirrored (panel_window.h); the `box` serial
// command checks the mapping. Fallback if it ever misbehaves: false makes
// windows span the full screen height and narrows only the row window.
constexpr bool kNarrowColumns = true;

}  // namespace

void St7305::begin(bool cold) {
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
    init_regs();  // ends in high power mode
    lpm_ = false;
    display_on_ = false;
  } else {
    lpm_ = true;  // refresh() always parks the panel in LPM before sleep
  }
}

void St7305::hold_pins(bool hold) {
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

void St7305::refresh(const Framebuffer& fb, const Rect* rects, size_t n) {
  if (n == 0) return;
  power_mode(false);
  for (size_t i = 0; i < n; ++i) {
    if (rects[i].clipped(Framebuffer::kWidth, Framebuffer::kHeight).empty()) continue;
    write_window(fb, PanelWindow::cover(rects[i], !kNarrowColumns));
  }
  display_on();
  power_mode(true);
}

void St7305::refresh_full(const Framebuffer& fb) {
  power_mode(false);
  write_window(fb, PanelWindow::full());
  display_on();
  power_mode(true);
}

void St7305::display_on() {
  if (display_on_) return;
  cmd(0x29);
  display_on_ = true;
}

void St7305::power_mode(bool low) {
  if (low == lpm_) return;
  cmd(low ? 0x39 : 0x38);
  lpm_ = low;
}

void St7305::write_window(const Framebuffer& fb, const PanelWindow& w) {
  const uint8_t col[] = {w.col_start_addr(), w.col_end_addr()};
  const uint8_t row[] = {w.row_first, w.row_last};
  cmd_data(0x2A, col, sizeof col);
  cmd_data(0x2B, row, sizeof row);

  // One 0x2C transaction: the controller fills the window row by row.
  spi_.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kCs, LOW);
  digitalWrite(kDc, LOW);
  spi_.transfer(0x2C);
  digitalWrite(kDc, HIGH);
  if (w.row_len() == Framebuffer::kRowBytes) {
    // Full-height window: the rows are contiguous in the framebuffer.
    spi_.writeBytes(fb.data() + w.row_offset(w.row_first), w.bytes());
  } else {
    for (int r = w.row_first; r <= w.row_last; ++r) {
      spi_.writeBytes(fb.data() + w.row_offset(r), w.row_len());
    }
  }
  digitalWrite(kCs, HIGH);
  spi_.endTransaction();
}

void St7305::cmd(uint8_t c) { cmd_data(c, nullptr, 0); }

void St7305::cmd_data(uint8_t c, const uint8_t* d, size_t n) {
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

void St7305::reset() {
  digitalWrite(kRst, HIGH);
  delay(50);
  digitalWrite(kRst, LOW);
  delay(20);
  digitalWrite(kRst, HIGH);
  delay(50);
}

void St7305::init_regs() {
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
  const PanelWindow full = PanelWindow::full();
  const uint8_t col[] = {full.col_start_addr(), full.col_end_addr()};
  const uint8_t row[] = {full.row_first, full.row_last};
  static const uint8_t kTe[] = {0x00};
  static const uint8_t kD0[] = {0xFF};
  cmd_data(0x2A, col, sizeof col);
  cmd_data(0x2B, row, sizeof row);
  cmd_data(0x35, kTe, sizeof kTe);  // tearing effect on
  cmd_data(0xD0, kD0, sizeof kD0);
  cmd(0x38);  // high power mode; display on (0x29) follows the first frame
}

}  // namespace inkwake
