// SPDX-License-Identifier: GPL-3.0-only
// Partial refresh addressing for the ST7305 on the RLCD-4.2 (hardware-free part).
//
// The init sequence sets the full window to column addresses 0x12..0x2A
// (25 x 3 bytes) and row addresses 0x00..0xC7 (200 rows). A 0x2C write fills the
// window row by row, so a full frame is the framebuffer in memory order. A
// partial write sets a smaller window with 0x2A/0x2B and sends, for each row in
// it, the contiguous byte run the window covers.
//
// Granularity: 2 pixels in x (one panel row), 12 pixels in y (one column address).
#pragma once

#include <cstddef>
#include <cstdint>

#include "framebuffer.h"

namespace inkwake {

struct PanelWindow {
  static constexpr uint8_t kColBase = 0x12;  // first column address of the panel
  static constexpr uint8_t kCols = 25;       // 0x12..0x2A
  static constexpr uint8_t kBytesPerCol = 3;
  static constexpr uint8_t kRows = 200;  // 0x00..0xC7

  uint8_t col_first = 0;  // 0..24, add kColBase for the 0x2A command
  uint8_t col_last = 0;
  uint8_t row_first = 0;  // 0..199, sent as is with 0x2B
  uint8_t row_last = 0;

  bool operator==(const PanelWindow& o) const {
    return col_first == o.col_first && col_last == o.col_last && row_first == o.row_first &&
           row_last == o.row_last;
  }

  // Parameters for the 0x2A and 0x2B commands.
  uint8_t col_start_addr() const { return static_cast<uint8_t>(kColBase + col_first); }
  uint8_t col_end_addr() const { return static_cast<uint8_t>(kColBase + col_last); }

  size_t rows() const { return static_cast<size_t>(row_last - row_first + 1); }
  // Bytes sent per panel row, and in total.
  size_t row_len() const { return static_cast<size_t>(col_last - col_first + 1) * kBytesPerCol; }
  size_t bytes() const { return rows() * row_len(); }
  // Offset into Framebuffer::data() of the run for panel row `row` (row_first..row_last).
  size_t row_offset(int row) const {
    return static_cast<size_t>(row) * Framebuffer::kRowBytes +
           static_cast<size_t>(col_first) * kBytesPerCol;
  }

  static PanelWindow full() { return {0, kCols - 1, 0, kRows - 1}; }

  // Smallest window holding `r` (clipped to the screen). With full_columns the
  // window spans all column addresses, so only the row window (0x2B) is narrowed.
  // `r` must not be empty after clipping.
  static PanelWindow cover(const Rect& r, bool full_columns = false);
};

}  // namespace inkwake
