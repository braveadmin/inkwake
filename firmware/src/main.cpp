// SPDX-License-Identifier: GPL-3.0-only
// M2 clock face (#18): HH:MM, date and a temperature slot on the reflective LCD,
// updated each minute with partial refresh.
//
// On battery the board deep-sleeps between minutes with the panel in LPM and
// DC/CS/RST held (proven in M0). With a USB host connected it stays awake and
// takes serial commands; USB powers the board then, so sleeping saves nothing.
//
// Time source: the on-board PCF85063 RTC (#19), which keeps time while the
// ESP32 is off. Its once-a-minute interrupt on GPIO15 wakes the board; each
// wake copies the RTC time into the ESP32 system clock (whose deep-sleep timer
// drifts by seconds per minute). A timer wake 10 s after the minute is a
// fallback in case the interrupt is missed. If the RTC lost its time, the
// firmware build time (UTC) is written to it; set it over serial with `set`.
// No timezone yet (#13): the RTC holds what is displayed.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "clock_face.h"
#include "framebuffer.h"
#include "rtc.h"
#include "st7305.h"

using namespace inkwake;

namespace {

constexpr int kKeyPin = 18;  // active low, external pull-up; wakes from deep sleep
constexpr int kAmpPin = 46;  // speaker amp enable, held low in sleep
constexpr uint32_t kMagic = 0x1A7E0019;
constexpr time_t kMinValidTime = 1735689600;  // 2025-01-01: older means the clock was reset
constexpr uint32_t kWakeMarginUs = 20'000;    // timer-only wake: just after the minute turns
// Fallback timer when the RTC interrupt is armed. Late enough that the ESP32's
// deep-sleep clock (internal RC, a few % off) never fires it before the minute.
constexpr uint32_t kFallbackUs = 10'000'000;

struct RtcState {
  uint32_t magic;
  FaceState shown;  // what the panel shows now
  uint32_t wakes;
  uint32_t rtc_wakes;    // woken by the RTC minute interrupt
  uint32_t timer_wakes;  // woken by the fallback timer: interrupt missed or not armed
  uint32_t rtc_errors;   // wakes where the RTC could not be read
  bool int_pullup;       // GPIO15 has no external pull-up: use the internal one in sleep
};
// `= {}` makes this a constant initialization. Without it, FaceState's member
// defaults can turn it into startup code that runs again on every deep-sleep
// wake, resetting `shown` (magic survives) so each wake redrew every field.
RTC_DATA_ATTR RtcState g_rtc = {};

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

void set_system_clock(time_t t) {
  const timeval tv{t, 0};
  settimeofday(&tv, nullptr);
}

// Copies the RTC time into the system clock. On a minute-interrupt wake the
// RTC has just turned the minute, so the lost fraction of a second is tiny.
rtc::Status sync_from_rtc() {
  time_t t;
  const rtc::Status s = rtc::read(t);
  if (s == rtc::Status::Ok) set_system_clock(t);
  return s;
}

// Sets both the system clock and the RTC.
void set_clock(int year, int mon, int day, int hour, int min, int sec) {
  tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  const time_t now = mktime(&t);  // TZ unset: local time = UTC
  set_system_clock(now);
  if (!rtc::write(now)) Serial.print("RTC write failed\r\n");
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

// INT is open drain. Checks for an external pull-up by pulling the pin down
// weakly: it reads high only if a board resistor wins. Run with INT released.
bool int_needs_pullup() {
  pinMode(rtc::kIntPin, INPUT_PULLDOWN);
  delay(2);
  const bool high = digitalRead(rtc::kIntPin) == HIGH;
  pinMode(rtc::kIntPin, INPUT);
  return !high;
}

[[noreturn]] void deep_sleep() {
  // Releases INT; without this it stays low and would wake us at once.
  const bool armed = rtc::arm_minute_interrupt();

  timeval tv;
  gettimeofday(&tv, nullptr);
  const uint64_t us = static_cast<uint64_t>(59 - tv.tv_sec % 60) * 1'000'000 +
                      (1'000'000 - tv.tv_usec) + (armed ? kFallbackUs : kWakeMarginUs);

  pinMode(kAmpPin, OUTPUT);
  digitalWrite(kAmpPin, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(kAmpPin));
  St7305::hold_pins(true);
  esp_sleep_enable_timer_wakeup(us);
  // KEY has an external pull-up, so no RTC pull-up for it (that keeps RTC
  // peripherals powered). INT only gets one if the board has none.
  uint64_t mask = 1ULL << kKeyPin;
  if (armed) {
    mask |= 1ULL << rtc::kIntPin;
    if (g_rtc.int_pullup) {
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      rtc_gpio_pullup_en(static_cast<gpio_num_t>(rtc::kIntPin));
      rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(rtc::kIntPin));
    }
  }
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void print_help() {
  Serial.print(
      "\r\ninkwake clock (M2, #18)\r\n"
      "  set YYYY-MM-DD HH:MM   set the clock and the RTC (no timezone yet: enter local time)\r\n"
      "  rtc                    RTC time and wake counters\r\n"
      "  full                   full redraw\r\n"
      "  box                    partial-refresh check: black square in the top-left corner\r\n"
      "  help\r\n"
      "Unplug USB to run on battery with deep sleep between minutes.\r\n");
}

void print_stamp(const char* label, time_t when) {
  tm t;
  gmtime_r(&when, &t);
  Serial.printf("%s %04d-%02d-%02d %02d:%02d:%02d", label, t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

void print_time() {
  print_stamp("time", time(nullptr));
  Serial.print("\r\n");
}

void print_rtc() {
  time_t t = 0;
  const rtc::Status s = rtc::read(t);
  if (s == rtc::Status::Ok) {
    print_stamp("rtc ", t);
    Serial.print("\r\n");
  } else {
    Serial.printf("rtc  %s\r\n", rtc::status_name(s));
  }
  print_time();
  Serial.printf(
      "wakes %lu: rtc interrupt %lu, fallback timer %lu, rtc read errors %lu\r\n"
      "GPIO15 (RTC INT): %s\r\n",
      static_cast<unsigned long>(g_rtc.wakes), static_cast<unsigned long>(g_rtc.rtc_wakes),
      static_cast<unsigned long>(g_rtc.timer_wakes), static_cast<unsigned long>(g_rtc.rtc_errors),
      g_rtc.int_pullup ? "no external pull-up, internal one used in sleep"
                       : "external pull-up found");
}

void handle_line(const String& line) {
  int y, mo, d, h, mi;
  if (line.startsWith("set ") &&
      sscanf(line.c_str() + 4, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
    set_clock(y, mo, d, h, mi, 0);
    print_rtc();
    update_display(true);
  } else if (line == "rtc") {
    print_rtc();
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

  rtc::begin();

  const uint64_t ext1 = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  const bool key = ext1 & (1ULL << kKeyPin);
  const bool woke =
      (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_EXT1) && g_rtc.magic == kMagic;
  rtc::Status rtc_status = rtc::Status::Ok;
  if (woke) {
    g_rtc.wakes++;
    if (ext1 & (1ULL << rtc::kIntPin)) g_rtc.rtc_wakes++;
    if (cause == ESP_SLEEP_WAKEUP_TIMER) g_rtc.timer_wakes++;
    rtc_status = sync_from_rtc();
    if (rtc_status != rtc::Status::Ok) g_rtc.rtc_errors++;  // keep the system clock
    g_lcd.begin(false);
    update_display(false);
    // Minute tick: back to sleep at once. Looking for a USB host costs time
    // on every wake; to use serial after plugging in, press KEY.
    if (!key) deep_sleep();
  } else {
    rtc_status = sync_from_rtc();
    if (rtc_status != rtc::Status::Ok) {
      // A reset keeps the system clock; a power-on starts it at 1970.
      if (time(nullptr) < kMinValidTime) set_clock_from_build();
      if (rtc_status == rtc::Status::TimeLost) rtc::write(time(nullptr));
    }
    rtc::arm_minute_interrupt();  // release INT before probing it
    g_rtc = RtcState{kMagic, FaceState{}, 0, 0, 0, 0, int_needs_pullup()};
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
  if (rtc_status != rtc::Status::Ok) {
    Serial.printf("RTC at boot: %s\r\n", rtc::status_name(rtc_status));
  }
  print_rtc();
  print_help();
}

void loop() {
  // The serial terminal sends each key as it is typed, so collect a line until
  // Enter (CR or LF) instead of reading with a timeout, and echo it back.
  static String line;
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (line.isEmpty()) continue;  // second half of CRLF
      Serial.print("\r\n");
      line.trim();
      handle_line(line);
      line = "";
    } else if (c == '\b' || c == 0x7F) {
      if (!line.isEmpty()) {
        line.remove(line.length() - 1);
        Serial.print("\b \b");
      }
    } else if (c >= 0x20 && c < 0x7F && line.length() < 64) {
      line += static_cast<char>(c);
      Serial.write(static_cast<uint8_t>(c));
    }
  }
  if (current_face() != g_rtc.shown) update_display(false);
  if (!HWCDC::isPlugged()) {
    delay(200);  // USB unplugged: settle, then run on battery
    if (!HWCDC::isPlugged()) deep_sleep();
  }
  delay(50);
}
