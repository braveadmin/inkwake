// SPDX-License-Identifier: GPL-3.0-only
// Renders the clock face on the host into a PBM image, for layout review
// without the board. Dirty regions are outlined with --regions.
//
//   g++ -std=c++17 -Ilib/display/src tools/preview_face.cpp lib/display/src/*.cpp -o preview
//   ./preview face.pbm 7 5 2 18 11 215 --regions
//   (hour minute weekday[0=Mon] day month temp_dc; temp_dc -32768 = no reading)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clock_face.h"

using namespace inkwake;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.pbm [h m weekday day month temp_dc] [--regions]\n",
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
  const bool regions = std::strcmp(argv[argc - 1], "--regions") == 0;

  static Framebuffer fb;
  render_face(fb, s);
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
