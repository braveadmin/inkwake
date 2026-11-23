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
  if (mode_ == WriteMode::HighPower) power_mode(false);
  for (size_t i = 0; i < n; ++i) {
    if (rects[i].clipped(Framebuffer::kWidth, Framebuffer::kHeight).empty()) continue;
    write_window(fb, PanelWindow::cover(rects[i], !kNarrowColumns));
  }
  display_on();
  power_mode(true);
}

void St7305::refresh_full(const Framebuffer& fb) {
  if (mode_ == WriteMode::HighPower) power_mode(false);
  write_window(fb, PanelWindow::full());
  display_on();
  power_mode(true);
}

void St7305::display_on() {
  if (display_on_) return;
  cmd(0x29);
  display_on_ = true;
}

// §7.11: wait 300 ms after LPM -> HPM before anything else, 100 ms after
// HPM -> LPM. The datasheet's voltage-setting step in between is left out:
// the setup programs one voltage set (0xC9 = 0) for both modes.
void St7305::power_mode(bool low) {
  if (low == lpm_) return;
  cmd(low ? 0x39 : 0x38);
  lpm_ = low;
  wait_(low ? 100 : 300);
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

// §12.1.4: low for at least 1 ms; after the rising edge the controller loads
// its NVM and, when it was running (sleep out), blanks the panel: up to 120 ms
// before it takes commands. The old 50 ms could drop the first setup commands
// (booster enable among them).
void St7305::reset() {
  digitalWrite(kRst, LOW);
  delay(10);
  digitalWrite(kRst, HIGH);
  wait_(120);
}

void St7305::recover() {
  cmd(0x10);  // sleep in: DC/DC and oscillator off (§8.1.6)
  wait_(120);
  reset();
  init_regs();
  lpm_ = false;
  display_on_ = false;
}

uint8_t St7305::read_power_mode() { return static_cast<uint8_t>(read_bits(0x0A, 8)); }

uint32_t St7305::read_display_status() { return read_bits(0x09, 32); }

// 4-line SPI read status (§7.1.4): 8 command bits with DC low, then the
// controller drives SDA, which is the MOSI line (GPIO12), shifting out on the
// falling clock edge. Bit-banged with the SPI peripheral detached, sampling
// after each rising edge. MOSI is released before the 8th falling edge so the
// two drivers never fight; its pull-up makes a silent controller read 0xFF.
uint32_t St7305::read_bits(uint8_t c, int bits) {
  spi_.end();
  pinMode(kSck, OUTPUT);
  pinMode(kMosi, OUTPUT);
  digitalWrite(kSck, LOW);
  digitalWrite(kDc, LOW);
  digitalWrite(kCs, LOW);
  delayMicroseconds(1);
  for (int i = 7; i >= 0; --i) {
    digitalWrite(kMosi, (c >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(kSck, HIGH);
    delayMicroseconds(1);
    if (i == 0) pinMode(kMosi, INPUT_PULLUP);
    digitalWrite(kSck, LOW);
  }
  digitalWrite(kDc, HIGH);
  uint32_t v = 0;
  for (int i = 0; i < bits; ++i) {
    delayMicroseconds(1);
    digitalWrite(kSck, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (digitalRead(kMosi) == HIGH ? 1 : 0);
    digitalWrite(kSck, LOW);
  }
  digitalWrite(kCs, HIGH);
  pinMode(kMosi, OUTPUT);
  spi_.begin(kSck, -1, kMosi, -1);
  return v;
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
  cmd(0x11);  // sleep out; at least 120 ms before the next commands (§7.9.2)
  wait_(200);
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
