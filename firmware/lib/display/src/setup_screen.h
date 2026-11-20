// SPDX-License-Identifier: GPL-3.0-only
// Screens for Wi-Fi setup mode (#20, ADR-0016): a QR code that joins the open
// setup network, and short two-line messages ("Connected", ...).
#pragma once

#include <cstdint>

#include "framebuffer.h"

namespace inkwake {

constexpr char kSetupTitle[] = "Scan to set up Wi-Fi";
constexpr char kRetryTitle[] = "Not connected: try again";

// Clears the framebuffer and draws `title`, the QR code from `qr` (a qrcodegen
// buffer) as large as fits, and "or join <ap_name>".
void render_setup(Framebuffer& fb, const uint8_t* qr, const char* ap_name,
                  const char* title = kSetupTitle);

// Clears the framebuffer and draws one or two centred lines (`line2` may be null).
void render_message(Framebuffer& fb, const char* line1, const char* line2);

// QR area of render_setup, for tests: module size in pixels and top-left corner.
struct QrPlacement {
  int scale;
  int x;
  int y;
};
QrPlacement setup_qr_placement(int modules);

}  // namespace inkwake
