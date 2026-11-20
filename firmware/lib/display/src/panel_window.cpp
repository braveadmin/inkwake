// SPDX-License-Identifier: GPL-3.0-only
#include "panel_window.h"

namespace inkwake {

PanelWindow PanelWindow::cover(const Rect& r, bool full_columns) {
  const Rect c = r.clipped(Framebuffer::kWidth, Framebuffer::kHeight);
  if (c.empty()) return full();

  PanelWindow w;
  w.row_first = static_cast<uint8_t>(c.x >> 1);
  w.row_last = static_cast<uint8_t>((c.x + c.w - 1) >> 1);
  if (full_columns) {
    w.col_first = 0;
    w.col_last = kCols - 1;
    return w;
  }
  // y grows down the screen but byte order runs bottom-up (inverted y).
  const int byte_first = (Framebuffer::kHeight - 1 - (c.y + c.h - 1)) >> 2;
  const int byte_last = (Framebuffer::kHeight - 1 - c.y) >> 2;
  w.col_first = static_cast<uint8_t>(byte_first / kBytesPerCol);
  w.col_last = static_cast<uint8_t>(byte_last / kBytesPerCol);
  return w;
}

}  // namespace inkwake
