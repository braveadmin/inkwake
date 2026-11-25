// SPDX-License-Identifier: GPL-3.0-only
// Renders the clock face on the host into a PBM image, for layout review
// without the board. Dirty regions are outlined with --regions, --wifi shows
// the Wi-Fi failure mark, --low /
// --critical the battery mark, --alarm HH:MM [--snoozed] [--wd N] the next
// alarm, --setup draws the Wi-Fi setup screen and --ringing the alarm screen
// instead. --menu SCREEN draws a button menu screen (#41): main, tz, custom
// (timezone list with a custom zone), wifi, forget, or date0 .. date4 (the
// date & time editor with field 0-4 marked).
//
//   gcc -c lib/qrcodegen/src/qrcodegen.c -o qrcodegen.o
//   g++ -std=c++17 -Ilib/display/src -Ilib/qrcodegen/src -Ilib/menu/src -Ilib/tz/src
//       -Ilib/alarm_core/src tools/preview_face.cpp lib/display/src/*.cpp lib/menu/src/*.cpp
//       lib/tz/src/*.cpp lib/alarm_core/src/*.cpp qrcodegen.o -o preview   (one line)
//   ./preview face.pbm 7 5 2 18 11 215 --regions
//   (hour minute weekday[0=Mon] day month temp_dc; temp_dc -32768 = no reading)
//   ./preview menu.pbm --menu date3
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clock_face.h"
#include "menu.h"
#include "menu_screen.h"
#include "qrcodegen.h"
#include "setup_screen.h"
#include "tz_presets.h"

using namespace inkwake;

// Drives lib/menu to the named screen and draws it, as main.cpp does.
static void draw_menu(Framebuffer& fb, const char* name) {
  menu::Context c;
  c.now = menu::DateTime{2026, 11, 25, 14, 7};
  c.tz_preset = std::strcmp(name, "custom") == 0 ? -1 : 0;
  c.wifi_saved = true;
  menu::Menu m;
  m.open(c);
  using menu::Input;
  if (std::strncmp(name, "date", 4) == 0) {
    m.handle(Input::Select);
    for (int f = std::atoi(name + 4); f > 0; --f) m.handle(Input::Select);
  } else if (std::strcmp(name, "tz") == 0 || std::strcmp(name, "custom") == 0) {
    m.handle(Input::Next);
    m.handle(Input::Select);
    for (int k = 0; k < 6; ++k) m.handle(Input::Next);  // scrolled to show both arrows
  } else if (std::strcmp(name, "wifi") == 0 || std::strcmp(name, "forget") == 0) {
    m.handle(Input::Next);
    m.handle(Input::Next);
    m.handle(Input::Select);
    if (std::strcmp(name, "forget") == 0) {
      m.handle(Input::Next);
      m.handle(Input::Select);
    }
  }
  if (m.screen() == menu::Screen::DateTime) {
    const menu::DateTime& v = m.value();
    render_datetime(fb, m.title(),
                    {v.year, v.month, v.day, v.hour, v.minute, static_cast<int>(m.field())},
                    m.hint());
    return;
  }
  const char* items[menu::kTzPresetCount + 1];
  for (size_t i = 0; i < m.count(); ++i) items[i] = m.item(i);
  render_list(fb, m.title(), items, m.count(), m.selected(), m.hint());
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(
        stderr,
        "usage: %s out.pbm [h m weekday day month temp_dc] [--regions] [--wifi] "
        "[--low|--critical] [--alarm HH:MM [--snoozed] [--wd N]] [--setup|--ringing|--menu "
        "SCREEN]\n",
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
  bool regions = false, setup = false, ringing = false;
  const char* menu_screen = nullptr;
  for (int i = 2; i < argc; ++i) {
    regions = regions || std::strcmp(argv[i], "--regions") == 0;
    setup = setup || std::strcmp(argv[i], "--setup") == 0;
    if (std::strcmp(argv[i], "--wifi") == 0) s.wifi_failed = true;
    if (std::strcmp(argv[i], "--low") == 0) s.battery = 1;
    if (std::strcmp(argv[i], "--critical") == 0) s.battery = 2;
    ringing = ringing || std::strcmp(argv[i], "--ringing") == 0;
    if (std::strcmp(argv[i], "--menu") == 0 && i + 1 < argc) menu_screen = argv[i + 1];
    if (std::strcmp(argv[i], "--alarm") == 0 && i + 1 < argc) {
      int h = 0, m = 0;
      std::sscanf(argv[i + 1], "%d:%d", &h, &m);
      s.alarm = FaceState::kAlarmNext;
      s.alarm_hour = static_cast<uint8_t>(h);
      s.alarm_minute = static_cast<uint8_t>(m);
    }
    if (std::strcmp(argv[i], "--snoozed") == 0) s.alarm = FaceState::kAlarmSnoozed;
    if (std::strcmp(argv[i], "--wd") == 0 && i + 1 < argc) {
      s.alarm_weekday = static_cast<uint8_t>(std::atoi(argv[i + 1]));
    }
  }

  static Framebuffer fb;
  if (setup) {
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    qrcodegen_encodeText("WIFI:T:nopass;S:inkwake-A10B;;", tmp, qr, qrcodegen_Ecc_LOW, 1, 40,
                         qrcodegen_Mask_AUTO, true);
    render_setup(fb, qr, "inkwake-A10B");
    regions = false;
  } else if (menu_screen != nullptr) {
    draw_menu(fb, menu_screen);
    regions = false;
  } else if (ringing) {
    render_ringing(fb, s.hour, s.minute, "Work");
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
