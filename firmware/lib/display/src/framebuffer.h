// SPDX-License-Identifier: GPL-3.0-only
// 1-bit framebuffer in the ST7305 RAM layout of the Waveshare ESP32-S3-RLCD-4.2,
// landscape 400x300. Pure C++17 so it can be unit tested on the host.
//
// Packing (from Waveshare's RLCD_SetLandscapePixel, Apache-2.0): one byte holds
// 2 columns x 4 rows, bit set = white. The panel's row address is x / 2 (200 rows
// of 75 bytes); a panel column address covers 3 bytes = 12 landscape rows.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake {

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  // Smallest rect holding both. An empty rect is ignored.
  Rect united(const Rect& o) const;
  Rect clipped(int width, int height) const;
};

class Framebuffer {
 public:
  static constexpr int kWidth = 400;
  static constexpr int kHeight = 300;
  static constexpr size_t kRowBytes = kHeight / 4;            // 75 bytes per panel row
  static constexpr size_t kBytes = kRowBytes * (kWidth / 2);  // 15000

  void clear(bool black = false);
  void set(int x, int y, bool black);  // out of range is ignored
  bool get(int x, int y) const;        // true = black; out of range reads white
  void fill(const Rect& r, bool black);

  const uint8_t* data() const { return buf_; }

  // Byte index and bit mask of a pixel. Caller checks the range.
  static size_t index(int x, int y) {
    return static_cast<size_t>(x >> 1) * kRowBytes + static_cast<size_t>((kHeight - 1 - y) >> 2);
  }
  static uint8_t mask(int x, int y) {
    return static_cast<uint8_t>(0x80u >> ((((kHeight - 1 - y) & 3) << 1) | (x & 1)));
  }

 private:
  uint8_t buf_[kBytes];
};

}  // namespace inkwake
