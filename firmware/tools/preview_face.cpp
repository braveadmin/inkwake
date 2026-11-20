// SPDX-License-Identifier: GPL-3.0-only
// Renders the clock face on the host into a PBM image, for layout review
// without the board. Dirty regions are outlined with --regions, --wifi shows
// the Wi-Fi failure mark, --setup draws the Wi-Fi setup screen instead.
//
//   gcc -c lib/qrcodegen/src/qrcodegen.c -o qrcodegen.o
//   g++ -std=c++17 -Ilib/display/src -Ilib/qrcodegen/src tools/preview_face.cpp
//       lib/display/src/*.cpp qrcodegen.o -o preview   (one line)
//   ./preview face.pbm 7 5 2 18 11 215 --regions
//   (hour minute weekday[0=Mon] day month temp_dc; temp_dc -32768 = no reading)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clock_face.h"
#include "qrcodegen.h"
#include "setup_screen.h"

using namespace inkwake;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(
        stderr,
        "usage: %s out.pbm [h m weekday day month temp_dc] [--regions] [--wifi] [--setup]\n",
        argv[0]);
    return 2;
  }
  FaceState s{7, 5, 2, 18, 11, 215};
  if (argc >= 8) {
    s.hour = static_cast<uint8_t>(std::atoi(argv[2]));
    s.minute = static_cast<uint8_t>(std::atoi(argv[3]));
    s.weekday = static_cast<uint8_t>(std::atoi(argv[4]));
    s.day = static_cast<uint8_t>(std::atoi(argv[5]));
    s.month = static_cast<uint8_t>(std::atoi(argv[6]));
    s.temp_dc = static_cast<int16_t>(std::atoi(argv[7]));
  }
  bool regions = false, setup = false;
  for (int i = 2; i < argc; ++i) {
    regions = regions || std::strcmp(argv[i], "--regions") == 0;
    setup = setup || std::strcmp(argv[i], "--setup") == 0;
    if (std::strcmp(argv[i], "--wifi") == 0) s.wifi_failed = true;
  }

  static Framebuffer fb;
  if (setup) {
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    qrcodegen_encodeText("WIFI:T:nopass;S:inkwake-A10B;;", tmp, qr, qrcodegen_Ecc_LOW, 1, 40,
                         qrcodegen_Mask_AUTO, true);
    render_setup(fb, qr, "inkwake-A10B");
    regions = false;
  } else {
    render_face(fb, s);
  }
  if (regions) {
    for (size_t i = 0; i < kFaceRegions; ++i) {
      const Rect r = face_region(static_cast<FaceRegion>(i));
      for (int x = r.x; x < r.x + r.w; x += 2) {
        fb.set(x, r.y, true);
        fb.set(x, r.y + r.h - 1, true);
      }
      for (int y = r.y; y < r.y + r.h; y += 2) {
        fb.set(r.x, y, true);
        fb.set(r.x + r.w - 1, y, true);
      }
    }
  }

  FILE* f = std::fopen(argv[1], "w");
  if (f == nullptr) return 1;
  std::fprintf(f, "P1\n%d %d\n", Framebuffer::kWidth, Framebuffer::kHeight);
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) std::fputc(fb.get(x, y) ? '1' : '0', f);
    std::fputc('\n', f);
  }
  std::fclose(f);
  return 0;
}
