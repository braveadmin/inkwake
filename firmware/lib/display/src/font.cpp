// SPDX-License-Identifier: GPL-3.0-only
#include "font.h"

namespace inkwake {

namespace {

// Decodes one UTF-8 codepoint (up to 3 bytes) and advances p. Invalid bytes
// decode to 0xFFFD so they are skipped as missing glyphs.
uint32_t next_codepoint(const char*& p) {
  const auto b0 = static_cast<uint8_t>(*p++);
  if (b0 < 0x80) return b0;
  int extra = 0;
  uint32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else {
    return 0xFFFD;
  }
  for (int i = 0; i < extra; ++i) {
    const auto b = static_cast<uint8_t>(*p);
    if ((b & 0xC0) != 0x80) return 0xFFFD;  // truncated: leave p on the next lead byte
    cp = (cp << 6) | (b & 0x3F);
    ++p;
  }
  return cp;
}

void blit(Framebuffer& fb, const Font& font, const Glyph& g, int x, int y, bool black) {
  const uint8_t* bits = font.bitmap + g.offset;
  for (int row = 0; row < g.height; ++row) {
    for (int col = 0; col < g.width; ++col) {
      const int i = row * g.width + col;
      if (bits[i >> 3] & (0x80u >> (i & 7))) fb.set(x + col, y + row, black);
    }
  }
}

}  // namespace

const Glyph* Font::glyph(uint32_t codepoint) const {
  if (codepoint < first || codepoint > last) return nullptr;
  return &glyphs[codepoint - first];
}

int draw_text(Framebuffer& fb, const Font& font, int x, int y, const char* text, bool black) {
  for (const char* p = text; *p != '\0';) {
    const Glyph* g = font.glyph(next_codepoint(p));
    if (g == nullptr) continue;
    blit(fb, font, *g, x + g->left, y + g->top, black);
    x += g->advance;
  }
  return x;
}

int text_width(const Font& font, const char* text) {
  int w = 0;
  for (const char* p = text; *p != '\0';) {
    const Glyph* g = font.glyph(next_codepoint(p));
    if (g != nullptr) w += g->advance;
  }
  return w;
}

void draw_text_centered(Framebuffer& fb, const Font& font, int cx, int y, const char* text,
                        bool black) {
  draw_text(fb, font, cx - text_width(font, text) / 2, y, text, black);
}

void draw_glyph_centered(Framebuffer& fb, const Font& font, int x, int cell_w, int y,
                         uint32_t codepoint, bool black) {
  const Glyph* g = font.glyph(codepoint);
  if (g == nullptr) return;
  blit(fb, font, *g, x + (cell_w - g->width) / 2, y + g->top, black);
}

}  // namespace inkwake
