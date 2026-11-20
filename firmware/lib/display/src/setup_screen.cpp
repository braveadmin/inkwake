// SPDX-License-Identifier: GPL-3.0-only
#include "setup_screen.h"

#include <cstdio>

#include "font.h"
#include "fonts/text30.h"
#include "qrcodegen.h"

namespace inkwake {

namespace {

constexpr int kTitleBaseline = 34;
constexpr int kFooterBaseline = 290;
// Between the two text lines; the white around the code is its quiet zone.
constexpr int kQrTop = 48;
constexpr int kQrBottom = 260;

}  // namespace

QrPlacement setup_qr_placement(int modules) {
  int scale = modules > 0 ? (kQrBottom - kQrTop) / modules : 1;
  if (scale > 7) scale = 7;  // leaves a wider quiet zone next to the text
  if (scale < 1) scale = 1;
  const int side = modules * scale;
  return QrPlacement{scale, (Framebuffer::kWidth - side) / 2,
                     kQrTop + (kQrBottom - kQrTop - side) / 2};
}

void render_setup(Framebuffer& fb, const uint8_t* qr, const char* ap_name, const char* title) {
  fb.clear();
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kTitleBaseline, title);
  const int n = qrcodegen_getSize(qr);
  const QrPlacement p = setup_qr_placement(n);
  for (int my = 0; my < n; ++my) {
    for (int mx = 0; mx < n; ++mx) {
      if (qrcodegen_getModule(qr, mx, my)) {
        fb.fill(Rect{p.x + mx * p.scale, p.y + my * p.scale, p.scale, p.scale}, true);
      }
    }
  }
  char buf[48];
  snprintf(buf, sizeof buf, "or join %s", ap_name);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, kFooterBaseline, buf);
}

void render_message(Framebuffer& fb, const char* line1, const char* line2) {
  fb.clear();
  if (line2 == nullptr) {
    draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, 160, line1);
    return;
  }
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, 135, line1);
  draw_text_centered(fb, fonts::kText30, Framebuffer::kWidth / 2, 185, line2);
}

}  // namespace inkwake
