// SPDX-License-Identifier: GPL-3.0-only
#include "framebuffer.h"

#include <algorithm>
#include <cstring>

namespace inkwake {

Rect Rect::united(const Rect& o) const {
  if (o.empty()) return *this;
  if (empty()) return o;
  const int x0 = std::min(x, o.x);
  const int y0 = std::min(y, o.y);
  const int x1 = std::max(x + w, o.x + o.w);
  const int y1 = std::max(y + h, o.y + o.h);
  return {x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::clipped(int width, int height) const {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, width);
  const int y1 = std::min(y + h, height);
  if (x1 <= x0 || y1 <= y0) return {};
  return {x0, y0, x1 - x0, y1 - y0};
}

void Framebuffer::clear(bool black) { std::memset(buf_, black ? 0x00 : 0xFF, kBytes); }

void Framebuffer::set(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  if (black) {
    buf_[index(x, y)] &= static_cast<uint8_t>(~mask(x, y));
  } else {
    buf_[index(x, y)] |= mask(x, y);
  }
}

bool Framebuffer::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return false;
  return (buf_[index(x, y)] & mask(x, y)) == 0;
}

void Framebuffer::fill(const Rect& r, bool black) {
  const Rect c = r.clipped(kWidth, kHeight);
  for (int y = c.y; y < c.y + c.h; ++y) {
    for (int x = c.x; x < c.x + c.w; ++x) set(x, y, black);
  }
}

}  // namespace inkwake
