// SPDX-License-Identifier: GPL-3.0-only
// M2 clock face (#18): HH:MM, date and a temperature slot on the reflective LCD,
// updated each minute with partial refresh.
//
// On battery the board deep-sleeps between minutes with the panel in LPM and
// DC/CS/RST held (proven in M0). With a USB host connected it stays awake and
// takes serial commands; USB powers the board then, so sleeping saves nothing.
//
// Time source until the RTC driver (#19) and NTP (#13) land: the ESP32 system
// clock, which keeps counting through deep sleep. A power-on starts it at the
// firmware build time (UTC); set it over serial with `set`. No timezone yet.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "clock_face.h"
#include "framebuffer.h"
#include "st7305.h"

using namespace inkwake;

namespace {

constexpr int kKeyPin = 18;  // active low, external pull-up; wakes from deep sleep
constexpr int kAmpPin = 46;  // speaker amp enable, held low in sleep
constexpr uint32_t kMagic = 0x1A7E0018;
constexpr time_t kMinValidTime = 1735689600;  // 2025-01-01: older means the clock was reset
constexpr uint32_t kWakeMarginUs = 20'000;    // wake just after the minute turns

struct RtcState {
  uint32_t magic;
  FaceState shown;  // what the panel shows now
  uint32_t wakes;
};
RTC_DATA_ATTR RtcState g_rtc;

Framebuffer g_fb;
St7305 g_lcd;

FaceState current_face() {
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  FaceState s;
  s.hour = static_cast<uint8_t>(t.tm_hour);
  s.minute = static_cast<uint8_t>(t.tm_min);
  s.weekday = static_cast<uint8_t>((t.tm_wday + 6) % 7);  // tm: 0 = Sunday
  s.day = static_cast<uint8_t>(t.tm_mday);
  s.month = static_cast<uint8_t>(t.tm_mon + 1);
  s.temp_dc = kNoTemp;  // room temperature arrives with #12
  return s;
}

void set_clock(int year, int mon, int day, int hour, int min, int sec) {
  tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  const timeval tv{mktime(&t), 0};  // TZ unset: local time = UTC
  settimeofday(&tv, nullptr);
}

// __DATE__ is "Nov 18 2026", __TIME__ is "12:34:56".
void set_clock_from_build() {
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int day = 1, year = 2026, h = 0, m = 0, s = 0;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
  const char* p = strstr(kMonths, mon);
  set_clock(year, p ? static_cast<int>(p - kMonths) / 3 + 1 : 1, day, h, m, s);
}

// Redraws the face. Full refresh on demand and once an hour, otherwise only
// the fields that changed since the last write.
void update_display(bool full) {
  const FaceState s = current_face();
  render_face(g_fb, s);
  if (full || s.minute == 0) {
    g_lcd.refresh_full(g_fb);
  } else {
    Rect dirty[kFaceRegions];
    g_lcd.refresh(g_fb, dirty, face_dirty(g_rtc.shown, s, dirty));
  }
  g_rtc.shown = s;
}

[[noreturn]] void deep_sleep() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  const uint64_t us = static_cast<uint64_t>(59 - tv.tv_sec % 60) * 1'000'000 +
                      (1'000'000 - tv.tv_usec) + kWakeMarginUs;

  pinMode(kAmpPin, OUTPUT);
  digitalWrite(kAmpPin, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(kAmpPin));
  St7305::hold_pins(true);
  esp_sleep_enable_timer_wakeup(us);
  // KEY has an external pull-up, so no RTC pull-up (that would keep RTC peripherals on).
  esp_sleep_enable_ext1_wakeup_io(1ULL << kKeyPin, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void print_help() {
  Serial.print(
      "\r\ninkwake clock (M2, #18)\r\n"
      "  set YYYY-MM-DD HH:MM   set the clock (no timezone yet: enter local time)\r\n"
      "  full                   full redraw\r\n"
      "  box                    partial-refresh check: black square in the top-left corner\r\n"
      "  help\r\n"
      "Unplug USB to run on battery with deep sleep between minutes.\r\n");
}

void print_time() {
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  Serial.printf("time %04d-%02d-%02d %02d:%02d:%02d\r\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
}

void handle_line(const String& line) {
  int y, mo, d, h, mi;
  if (line.startsWith("set ") &&
      sscanf(line.c_str() + 4, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
    set_clock(y, mo, d, h, mi, 0);
    print_time();
    update_display(true);
  } else if (line == "full") {
    update_display(true);
    Serial.print("full redraw\r\n");
  } else if (line == "box") {
    // Drawn into the framebuffer and sent with a partial window only. It must
    // appear in the top-left corner; anywhere else means the column window is
    // mirrored (see kNarrowColumns in st7305.cpp). `full` removes it.
    const Rect box{0, 0, 36, 36};
    g_fb.fill(box, true);
    g_lcd.refresh(g_fb, &box, 1);
    Serial.print("box sent to the top-left corner; `full` clears it\r\n");
  } else if (!line.isEmpty()) {
    print_help();
  }
}

}  // namespace

void setup() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  gpio_hold_dis(static_cast<gpio_num_t>(kAmpPin));
  pinMode(kAmpPin, OUTPUT);
  digitalWrite(kAmpPin, LOW);
  pinMode(kKeyPin, INPUT);

  const bool woke =
      (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_EXT1) && g_rtc.magic == kMagic;
  if (woke) {
    g_rtc.wakes++;
    g_lcd.begin(false);
    update_display(false);
    // Minute tick: back to sleep at once. Looking for a USB host costs time
    // on every wake; to use serial after plugging in, press KEY.
    if (cause == ESP_SLEEP_WAKEUP_TIMER) deep_sleep();
  } else {
    if (time(nullptr) < kMinValidTime) set_clock_from_build();
    g_rtc = RtcState{kMagic, FaceState{}, 0};
    g_lcd.begin(true);
    update_display(true);
  }

  // Power-on or KEY: stay awake while a USB host is connected. isPlugged()
  // watches USB frames from the host; `Serial` would also need a terminal open.
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!HWCDC::isPlugged() && millis() - t0 < 1500) delay(10);
  if (!HWCDC::isPlugged()) deep_sleep();
  delay(500);  // let a terminal that was already open reattach

  Serial.printf("\r\nwake cause %d, %lu deep-sleep wakes\r\n", static_cast<int>(cause),
                static_cast<unsigned long>(g_rtc.wakes));
  print_time();
  print_help();
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    handle_line(line);
  }
  if (current_face() != g_rtc.shown) update_display(false);
  if (!HWCDC::isPlugged()) {
    delay(200);  // USB unplugged: settle, then run on battery
    if (!HWCDC::isPlugged()) deep_sleep();
  }
  delay(50);
}
