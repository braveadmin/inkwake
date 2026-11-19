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
// fallback in case the interrupt is missed. The RTC has no backup supply, so
// PWR off or a flat battery loses the time: the face then shows --:-- and
// "Time not set" until `set` over serial (later NTP, #13) writes it. The RTC's
// oscillator-stopped flag is what marks this, so it survives resets and sleep.
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
constexpr int kBatPin = 4;   // battery sense, 1:3 divider
constexpr uint32_t kMagic = 0x1A7E001B;
constexpr time_t kMinValidTime = 1735689600;  // 2025-01-01: older means the clock was reset
constexpr uint32_t kWakeMarginUs = 20'000;    // timer-only wake: just after the minute turns
// Fallback timer when the RTC interrupt is armed. Late enough that the ESP32's
// deep-sleep clock (internal RC, a few % off) never fires it before the minute.
constexpr uint32_t kFallbackUs = 10'000'000;

// One deep-sleep wake (or power-on), for diagnosing battery runs afterwards
// over serial (`log`).
struct WakeEntry {
  uint32_t when;      // system time after the RTC sync (Unix, UTC)
  char cause;         // R = RTC interrupt, T = fallback timer, K = KEY, P = power-on
  uint8_t rtc;        // rtc::Status of the sync
  uint8_t regions;    // regions redrawn, kFullRefresh / kPanelRestart
  uint8_t te_ms;      // wait for the panel's TE pulse, kNoTe if none
  uint16_t awake_ms;  // from boot to the display update done
  uint16_t bat_mv;
};
constexpr size_t kLogLen = 32;
constexpr uint8_t kFullRefresh = 0xFF;
constexpr uint8_t kPanelRestart = 0xFE;  // TE was quiet: panel reset + init + full refresh
constexpr uint8_t kNoTe = 0xFF;
constexpr uint32_t kTeTimeoutMs = 80;  // a few frames in high power mode

struct RtcState {
  uint32_t magic;
  FaceState shown;  // what the panel shows now
  uint32_t wakes;
  uint32_t rtc_wakes;    // woken by the RTC minute interrupt
  uint32_t timer_wakes;  // woken by the fallback timer: interrupt missed or not armed
  uint32_t rtc_errors;   // wakes where the RTC could not be read
  bool int_pullup;       // GPIO15 has no external pull-up: use the internal one in sleep
  bool time_set;         // false while the RTC reports its time lost
  bool te_ok;            // TE pulsed after the power-on init, so a quiet TE means trouble
  uint32_t panel_restarts;
  uint32_t log_count;  // entries ever written; the newest is at (log_count - 1) % kLogLen
  WakeEntry log[kLogLen];
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
  s.time_set = g_rtc.time_set;
  return s;
}

void set_system_clock(time_t t) {
  const timeval tv{t, 0};
  settimeofday(&tv, nullptr);
}

// Copies the RTC time into the system clock. On a minute-interrupt wake the
// RTC has just turned the minute, so the lost fraction of a second is tiny.
// No answer on I2C keeps the system clock and the previous time_set.
rtc::Status sync_from_rtc() {
  time_t t;
  const rtc::Status s = rtc::read(t);
  if (s == rtc::Status::Ok) set_system_clock(t);
  if (s != rtc::Status::NoDevice) g_rtc.time_set = s == rtc::Status::Ok;
  return s;
}

time_t make_time(int year, int mon, int day, int hour, int min, int sec) {
  tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  return mktime(&t);  // TZ unset: local time = UTC
}

// Sets both the system clock and the RTC.
void set_clock(time_t now) {
  set_system_clock(now);
  g_rtc.time_set = true;
  // Fails if the RTC is missing, or within ~2 s of power-up while its crystal starts.
  if (!rtc::write(now)) Serial.print("RTC write failed: try `set` again\r\n");
}

// __DATE__ is "Nov 18 2026", __TIME__ is "12:34:56". Gives the system clock a
// start for the wake timer maths while the time is not set; not shown.
void set_clock_from_build() {
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int day = 1, year = 2026, h = 0, m = 0, s = 0;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
  const char* p = strstr(kMonths, mon);
  set_system_clock(make_time(year, p ? static_cast<int>(p - kMonths) / 3 + 1 : 1, day, h, m, s));
}

// Redraws the face. Full refresh on demand and once an hour, otherwise only
// the fields that changed since the last write. Returns the regions written,
// kFullRefresh for a full refresh.
uint8_t update_display(bool full) {
  const FaceState s = current_face();
  render_face(g_fb, s);
  uint8_t n = kFullRefresh;
  if (full || s.minute == 0) {
    g_lcd.refresh_full(g_fb);
  } else {
    Rect dirty[kFaceRegions];
    n = static_cast<uint8_t>(face_dirty(g_rtc.shown, s, dirty));
    g_lcd.refresh(g_fb, dirty, n);
  }
  g_rtc.shown = s;
  return n;
}

uint16_t battery_mv() { return static_cast<uint16_t>(analogReadMilliVolts(kBatPin) * 3); }

void log_wake(char cause, rtc::Status rtc_status, uint8_t regions, int te_ms) {
  WakeEntry& e = g_rtc.log[g_rtc.log_count++ % kLogLen];
  e.when = static_cast<uint32_t>(time(nullptr));
  e.cause = cause;
  e.rtc = static_cast<uint8_t>(rtc_status);
  e.regions = regions;
  e.te_ms = te_ms < 0 || te_ms >= kNoTe ? kNoTe : static_cast<uint8_t>(te_ms);
  e.awake_ms = static_cast<uint16_t>(millis());
  e.bat_mv = battery_mv();
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
      "  log                    last 32 wakes: time, cause, redraw, panel TE, awake time, "
      "battery\r\n"
      "  full                   full redraw\r\n"
      "  init                   restart the panel (reset + init) and redraw\r\n"
      "  te                     panel TE pin check (1 s in each power mode)\r\n"
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
      "GPIO15 (RTC INT): %s\r\n"
      "panel TE check: %s, panel restarts %lu\r\n",
      static_cast<unsigned long>(g_rtc.wakes), static_cast<unsigned long>(g_rtc.rtc_wakes),
      static_cast<unsigned long>(g_rtc.timer_wakes), static_cast<unsigned long>(g_rtc.rtc_errors),
      g_rtc.int_pullup ? "no external pull-up, internal one used in sleep"
                       : "external pull-up found",
      g_rtc.te_ok ? "on" : "off (no TE pulse after power-on)",
      static_cast<unsigned long>(g_rtc.panel_restarts));
}

void print_log() {
  const uint32_t n = g_rtc.log_count < kLogLen ? g_rtc.log_count : kLogLen;
  Serial.printf("last %lu of %lu wakes (R rtc, T timer, K key, P power-on), battery now %u mV\r\n",
                static_cast<unsigned long>(n), static_cast<unsigned long>(g_rtc.log_count),
                battery_mv());
  for (uint32_t i = g_rtc.log_count - n; i < g_rtc.log_count; ++i) {
    const WakeEntry& e = g_rtc.log[i % kLogLen];
    print_stamp(" ", static_cast<time_t>(e.when));
    char redraw[16];
    if (e.regions == kFullRefresh) {
      snprintf(redraw, sizeof redraw, "full");
    } else if (e.regions == kPanelRestart) {
      snprintf(redraw, sizeof redraw, "PANEL RESTART");
    } else {
      snprintf(redraw, sizeof redraw, "%u regions", e.regions);
    }
    char te[12];
    if (e.te_ms == kNoTe) {
      snprintf(te, sizeof te, "te none");
    } else {
      snprintf(te, sizeof te, "te %2u ms", e.te_ms);
    }
    Serial.printf("  %c  %-13s %-8s %5u ms  %4u mV%s%s\r\n", e.cause, redraw, te, e.awake_ms,
                  e.bat_mv, e.rtc == static_cast<uint8_t>(rtc::Status::Ok) ? "" : "  rtc: ",
                  e.rtc == static_cast<uint8_t>(rtc::Status::Ok)
                      ? ""
                      : rtc::status_name(static_cast<rtc::Status>(e.rtc)));
  }
}

void handle_line(const String& line) {
  int y, mo, d, h, mi;
  if (line.startsWith("set ") &&
      sscanf(line.c_str() + 4, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
    set_clock(make_time(y, mo, d, h, mi, 0));
    print_rtc();
    update_display(true);
  } else if (line == "rtc") {
    print_rtc();
  } else if (line == "log") {
    print_log();
  } else if (line == "te") {
    // Is TE driven at all? A floating pin follows the internal pulls.
    pinMode(St7305::kTe, INPUT_PULLDOWN);
    delay(2);
    const int with_down = digitalRead(St7305::kTe);
    pinMode(St7305::kTe, INPUT_PULLUP);
    delay(2);
    const int with_up = digitalRead(St7305::kTe);
    Serial.printf("TE (GPIO%d) with pull-down %d, with pull-up %d%s\r\n", St7305::kTe, with_down,
                  with_up,
                  with_down == 0 && with_up == 1 ? "  <- follows the pulls: floating?" : "");
    for (const bool lpm : {false, true}) {
      const St7305::TeStats st = g_lcd.te_sample(lpm, 1000);
      Serial.printf("%s mode, 1 s: %lu pulses, high %lu of %lu samples\r\n", lpm ? "low " : "high",
                    static_cast<unsigned long>(st.rising), static_cast<unsigned long>(st.high),
                    static_cast<unsigned long>(st.samples));
    }
  } else if (line == "init") {
    Serial.printf("TE before: %d ms (-1 = none)\r\n", g_lcd.te_wait_ms(kTeTimeoutMs));
    g_lcd.begin(true);
    const int te = g_lcd.te_wait_ms(kTeTimeoutMs);
    update_display(true);
    Serial.printf("panel restarted; TE after: %d ms\r\n", te);
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
    if (rtc_status == rtc::Status::NoDevice) g_rtc.rtc_errors++;
    g_lcd.begin(false);
    const int te = g_lcd.te_wait_ms(kTeTimeoutMs);
    const bool restart = g_rtc.te_ok && te < 0;
    if (restart) {
      g_rtc.panel_restarts++;
      g_lcd.begin(true);
    }
    const uint8_t regions = restart ? (update_display(true), kPanelRestart) : update_display(false);
    log_wake(key                               ? 'K'
             : cause == ESP_SLEEP_WAKEUP_TIMER ? 'T'
             : ext1 & (1ULL << rtc::kIntPin)   ? 'R'
                                               : '?',
             rtc_status, regions, te);
    // Minute tick: back to sleep at once. Looking for a USB host costs time
    // on every wake; to use serial after plugging in, press KEY.
    if (!key) deep_sleep();
  } else {
    rtc::arm_minute_interrupt();  // release INT before probing it
    const bool pullup = int_needs_pullup();
    // Keep the wake log across resets (not power loss) when it is intact.
    const bool keep_log = g_rtc.magic == kMagic;
    const uint32_t log_count = keep_log ? g_rtc.log_count : 0;
    WakeEntry saved_log[kLogLen] = {};
    if (keep_log) memcpy(saved_log, g_rtc.log, sizeof saved_log);
    g_rtc = {};
    g_rtc.magic = kMagic;
    g_rtc.int_pullup = pullup;
    g_rtc.log_count = log_count;
    memcpy(g_rtc.log, saved_log, sizeof saved_log);
    rtc_status = sync_from_rtc();
    // A reset keeps the system clock; a power-on starts it at 1970.
    if (rtc_status != rtc::Status::Ok && time(nullptr) < kMinValidTime) set_clock_from_build();
    g_lcd.begin(true);
    const int te = g_lcd.te_wait_ms(kTeTimeoutMs);
    g_rtc.te_ok = te >= 0;
    update_display(true);
    log_wake('P', rtc_status, kFullRefresh, te);
  }

  // Power-on or KEY: stay awake while a USB host is connected. isPlugged()
  // watches USB frames from the host; `Serial` would also need a terminal open.
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!HWCDC::isPlugged() && millis() - t0 < 1500) delay(10);
  if (!HWCDC::isPlugged()) deep_sleep();
  delay(500);  // let a terminal that was already open reattach

  Serial.printf("\r\nwake cause %d (0 = power-on or reset), %lu deep-sleep wakes\r\n",
                static_cast<int>(cause), static_cast<unsigned long>(g_rtc.wakes));
  if (rtc_status != rtc::Status::Ok) {
    Serial.printf("RTC at boot: %s. Set the time with `set`.\r\n", rtc::status_name(rtc_status));
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
