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
//
// Timezone (#13): the RTC and the system clock keep UTC. lib/tz turns it into
// local time for the face and turns the local time typed into `set` back into
// UTC. The POSIX TZ string (`tz`) is kept in NVS and copied into RTC memory,
// where each wake parses it again (microseconds, no NVS read).
//
// Room temperature (#12): the on-board SHTC3, measured on every wake (started
// right after boot, read before the display update) and once a minute while
// awake on USB. The calibration offset (`offset`) is kept in NVS.
#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "clock_face.h"
#include "framebuffer.h"
#include "room_temp.h"
#include "rtc.h"
#include "shtc3.h"
#include "st7305.h"
#include "tz.h"

using namespace inkwake;

namespace {

constexpr int kKeyPin = 18;  // active low, external pull-up; wakes from deep sleep
constexpr int kAmpPin = 46;  // speaker amp enable, held low in sleep
constexpr int kBatPin = 4;   // battery sense, 1:3 divider
constexpr uint32_t kMagic = 0x1A7E001D;
constexpr time_t kMinValidTime = 1735689600;  // 2025-01-01: older means the clock was reset
constexpr uint32_t kWakeMarginUs = 20'000;    // timer-only wake: just after the minute turns
// Fallback timer when the RTC interrupt is armed. Late enough that the ESP32's
// deep-sleep clock (internal RC, a few % off) never fires it before the minute.
constexpr uint32_t kFallbackUs = 10'000'000;

// One deep-sleep wake (or power-on), for diagnosing battery runs afterwards
// over serial (`log`).
struct WakeEntry {
  uint32_t when;    // system time after the RTC sync (Unix, UTC)
  char cause;       // R = RTC interrupt, T = fallback timer, K = KEY, P = power-on
  uint8_t rtc;      // rtc::Status of the sync
  uint8_t regions;  // regions redrawn, kFullRefresh / kReinit
  uint8_t pad;
  uint16_t awake_ms;  // from boot to the display update done
  uint16_t bat_mv;
};
constexpr size_t kLogLen = 32;
constexpr uint8_t kFullRefresh = 0xFF;
constexpr uint8_t kReinit = 0xFE;  // panel reset + init + full frame
// The panel now and then loses its setup during a wake (2026-11-19/20: frozen
// image until `init`; suspected: its RST/CS lines float while the ESP32 boots
// from deep sleep). Restarting it on minute wakes bounds how long a freeze
// lasts. Every minute wake re-sends the setup without a reset (`soft`, not
// visible); a full restart (visible redraw) runs every `reinit N` minutes.
// Defaults after power-on:
constexpr uint8_t kDefaultReinitMin = 60;
constexpr bool kDefaultSoftSetup = true;
constexpr uint32_t kTempPeriodMs = 60'000;  // while awake on USB
constexpr char kPrefsNamespace[] = "inkwake";
constexpr char kPrefTempOffset[] = "temp_off";               // int16, tenths of a degree C
constexpr char kPrefTz[] = "tz";                             // POSIX TZ string
constexpr size_t kTzLen = 64;                                // including the terminating NUL
constexpr char kDefaultTz[] = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe

struct RtcState {
  uint32_t magic;
  FaceState shown;  // what the panel shows now
  uint32_t wakes;
  uint32_t rtc_wakes;    // woken by the RTC minute interrupt
  uint32_t timer_wakes;  // woken by the fallback timer: interrupt missed or not armed
  uint32_t rtc_errors;   // wakes where the RTC could not be read
  bool int_pullup;       // GPIO15 has no external pull-up: use the internal one in sleep
  bool time_set;         // false while the RTC reports its time lost
  uint8_t reinit_min;    // restart the panel when minute % reinit_min == 0; 0 = never
  bool soft_setup;       // re-send the panel setup on the other minute wakes
  uint32_t log_count;    // entries ever written; the newest is at (log_count - 1) % kLogLen
  WakeEntry log[kLogLen];
  // Added after `log` so the log, kept across resets, stays where it was.
  int16_t temp_raw_dc;     // last SHTC3 reading without offset, kNoTemp if it failed
  uint16_t temp_rh_dpct;   // humidity of the same reading (sensor, not corrected)
  int16_t temp_offset_dc;  // calibration, copy of the NVS value
  uint8_t temp_status;     // room_temp::Status of the last reading
  uint32_t temp_errors;    // failed readings since power-on
  char tz[kTzLen];         // POSIX TZ string, copy of the NVS value
};
// `= {}` makes this a constant initialization. Without it, FaceState's member
// defaults can turn it into startup code that runs again on every deep-sleep
// wake, resetting `shown` (magic survives) so each wake redrew every field.
RTC_DATA_ATTR RtcState g_rtc = {};

Framebuffer g_fb;
St7305 g_lcd;
tz::Zone g_zone;  // parsed from g_rtc.tz on every boot and wake

FaceState current_face() {
  const tz::Civil t = tz::to_civil(g_zone, time(nullptr));
  FaceState s;
  s.hour = t.hour;
  s.minute = t.minute;
  s.weekday = t.weekday;  // 0 = Monday, as FaceState
  s.day = t.day;
  s.month = t.month;
  s.temp_dc = g_rtc.temp_raw_dc == kNoTemp
                  ? kNoTemp
                  : shtc3::apply_offset(g_rtc.temp_raw_dc, g_rtc.temp_offset_dc);
  s.time_set = g_rtc.time_set;
  return s;
}

// Completes a measurement begun with room_temp::start(). A failed reading shows
// --.- until the next good one.
void finish_temp(room_temp::Status started) {
  shtc3::Reading r{};
  const room_temp::Status s = started == room_temp::Status::Ok ? room_temp::finish(r) : started;
  g_rtc.temp_status = static_cast<uint8_t>(s);
  if (s == room_temp::Status::Ok) {
    g_rtc.temp_raw_dc = r.temp_dc;
    g_rtc.temp_rh_dpct = r.rh_dpct;
  } else {
    g_rtc.temp_raw_dc = kNoTemp;
    g_rtc.temp_errors++;
  }
}

void measure_temp() { finish_temp(room_temp::start()); }

int16_t load_temp_offset() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return 0;  // nothing saved yet
  const int16_t v = prefs.getShort(kPrefTempOffset, 0);
  prefs.end();
  return shtc3::offset_valid(v) ? v : 0;
}

bool save_temp_offset(int16_t v) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok = prefs.putShort(kPrefTempOffset, v) == sizeof v;
  prefs.end();
  return ok;
}

// Copies a valid TZ string into RTC memory and the parsed zone. False leaves both.
bool use_tz(const char* posix) {
  tz::Zone z;
  // strnlen: the RTC copy may hold anything after a firmware change.
  const size_t n = strnlen(posix, kTzLen);
  if (n >= kTzLen || !tz::parse(posix, z)) return false;
  if (posix != g_rtc.tz) memcpy(g_rtc.tz, posix, n + 1);
  g_zone = z;
  return true;
}

// NVS value, or the default when nothing valid is saved.
void load_tz() {
  char buf[kTzLen] = {};
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    if (prefs.isKey(kPrefTz)) prefs.getString(kPrefTz, buf, sizeof buf);
    prefs.end();
  }
  if (!use_tz(buf)) use_tz(kDefaultTz);
}

bool save_tz(const char* posix) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok = prefs.putString(kPrefTz, posix) == strlen(posix);
  prefs.end();
  return ok;
}

// Tenths as "-3.5"; ASCII only for the serial terminal.
void format_tenths(int v, char* buf, size_t n) {
  snprintf(buf, n, "%s%d.%d", v < 0 ? "-" : "", std::abs(v) / 10, std::abs(v) % 10);
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
  return mktime(&t);  // TZ unset in newlib: fields taken as they are, no zone applied
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

void log_wake(char cause, rtc::Status rtc_status, uint8_t regions) {
  WakeEntry& e = g_rtc.log[g_rtc.log_count++ % kLogLen];
  e.when = static_cast<uint32_t>(time(nullptr));
  e.cause = cause;
  e.rtc = static_cast<uint8_t>(rtc_status);
  e.regions = regions;
  e.pad = 0;
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
      "\r\ninkwake clock (M2)\r\n"
      "  set YYYY-MM-DD HH:MM   set the clock, in local time (the RTC keeps UTC)\r\n"
      "  tz [POSIX-TZ]          show or set the timezone, e.g. tz CET-1CEST,M3.5.0,M10.5.0/3\r\n"
      "  rtc                    RTC time and wake counters\r\n"
      "  temp                   measure the room temperature now (sensor, offset, humidity)\r\n"
      "  offset [C]             show or set the temperature calibration, -10.0 to 10.0 C\r\n"
      "  log                    last 32 wakes: time, cause, redraw, awake time, battery\r\n"
      "  full                   full redraw\r\n"
      "  init                   restart the panel (reset + init) and redraw\r\n"
      "  reinit N               restart the panel every N minutes on minute wakes (0 = never)\r\n"
      "  soft 1|0               re-send the panel setup on every other minute wake\r\n"
      "  sleep                  deep sleep now, also on USB power (KEY wakes and stays awake)\r\n"
      "  box                    partial-refresh check: black square in the top-left corner\r\n"
      "  help\r\n"
      "Unplug USB to run on battery with deep sleep between minutes.\r\n");
}

void print_utc(const char* label, time_t when) {
  tm t;
  gmtime_r(&when, &t);
  Serial.printf("%s %04d-%02d-%02d %02d:%02d:%02d UTC", label, t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

// Local time with the zone abbreviation, e.g. "2026-11-20 13:34:56 CET".
void print_stamp(const char* label, time_t when) {
  const tz::Civil t = tz::to_civil(g_zone, when);
  Serial.printf("%s %04d-%02d-%02d %02d:%02d:%02d %s", label, t.year, t.month, t.day, t.hour,
                t.minute, t.second, t.dst ? g_zone.dst_name : g_zone.std_name);
}

void print_time() {
  print_stamp("time", time(nullptr));
  Serial.print("\r\n");
}

void print_rtc() {
  time_t t = 0;
  const rtc::Status s = rtc::read(t);
  if (s == rtc::Status::Ok) {
    print_utc("rtc ", t);
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

void print_log() {
  const uint32_t n = g_rtc.log_count < kLogLen ? g_rtc.log_count : kLogLen;
  Serial.printf("last %lu of %lu wakes (R rtc, T timer, K key, P power-on), battery now %u mV\r\n",
                static_cast<unsigned long>(n), static_cast<unsigned long>(g_rtc.log_count),
                battery_mv());
  for (uint32_t i = g_rtc.log_count - n; i < g_rtc.log_count; ++i) {
    const WakeEntry& e = g_rtc.log[i % kLogLen];
    print_stamp(" ", static_cast<time_t>(e.when));
    char redraw[12];
    if (e.regions == kFullRefresh) {
      snprintf(redraw, sizeof redraw, "full");
    } else if (e.regions == kReinit) {
      snprintf(redraw, sizeof redraw, "reinit");
    } else {
      snprintf(redraw, sizeof redraw, "%u regions", e.regions);
    }
    Serial.printf("  %c  %-10s %5u ms  %4u mV%s%s\r\n", e.cause, redraw, e.awake_ms, e.bat_mv,
                  e.rtc == static_cast<uint8_t>(rtc::Status::Ok) ? "" : "  rtc: ",
                  e.rtc == static_cast<uint8_t>(rtc::Status::Ok)
                      ? ""
                      : rtc::status_name(static_cast<rtc::Status>(e.rtc)));
  }
}

void print_temp() {
  char shown[12], sensor[12], offset[12];
  format_tenths(g_rtc.temp_offset_dc, offset, sizeof offset);
  const auto status = static_cast<room_temp::Status>(g_rtc.temp_status);
  if (status == room_temp::Status::Ok) {
    char rh[12];
    format_tenths(current_face().temp_dc, shown, sizeof shown);
    format_tenths(g_rtc.temp_raw_dc, sensor, sizeof sensor);
    format_tenths(g_rtc.temp_rh_dpct, rh, sizeof rh);
    Serial.printf("temp %s C (sensor %s C, offset %s C), humidity %s %%RH (sensor)\r\n", shown,
                  sensor, offset, rh);
  } else {
    Serial.printf("temp: %s (offset %s C)\r\n", room_temp::status_name(status), offset);
  }
  uint16_t id = 0;
  const room_temp::Status id_status = room_temp::read_id(id);
  if (id_status == room_temp::Status::Ok) {
    Serial.printf("SHTC3 id 0x%04X", id);
  } else {
    Serial.printf("SHTC3 id: %s", room_temp::status_name(id_status));
  }
  Serial.printf(", failed readings since power-on: %lu\r\n",
                static_cast<unsigned long>(g_rtc.temp_errors));
}

// "offset" alone prints it; "offset -3.5" sets and saves it.
void handle_offset(const String& arg) {
  if (!arg.isEmpty()) {
    char* end = nullptr;
    const float c = strtof(arg.c_str(), &end);
    // Range check before rounding: lroundf of a huge value is undefined (NaN fails it too).
    const bool in_range = c >= -10.0f && c <= 10.0f;
    const long dc = in_range ? lroundf(c * 10) : 0;
    if (end == arg.c_str() || *end != '\0' || !in_range || !shtc3::offset_valid(dc)) {
      Serial.print("offset takes -10.0 to 10.0 (degrees C), e.g. `offset -3.5`\r\n");
      return;
    }
    g_rtc.temp_offset_dc = static_cast<int16_t>(dc);
    if (!save_temp_offset(g_rtc.temp_offset_dc)) {
      Serial.print("could not save the offset: it is lost at power-off\r\n");
    }
  }
  char buf[12];
  format_tenths(g_rtc.temp_offset_dc, buf, sizeof buf);
  Serial.printf("temperature offset %s C\r\n", buf);
}

void print_offset(int32_t s) {
  const int32_t a = s < 0 ? -s : s;
  Serial.printf("UTC%c%02ld:%02ld", s < 0 ? '-' : '+', static_cast<long>(a / 3600),
                static_cast<long>(a / 60 % 60));
}

// "tz" alone prints the zone; "tz <POSIX string>" sets and saves it.
void handle_tz(const String& arg) {
  if (!arg.isEmpty()) {
    if (!use_tz(arg.c_str())) {
      Serial.print(
          "not a POSIX TZ string; examples:\r\n"
          "  tz CET-1CEST,M3.5.0,M10.5.0/3     Central Europe\r\n"
          "  tz WET0WEST,M3.5.0/1,M10.5.0      Portugal, Canary Islands\r\n"
          "  tz GMT0BST,M3.5.0/1,M10.5.0       UK\r\n"
          "  tz EST5EDT,M3.2.0,M11.1.0         US Eastern\r\n"
          "  tz UTC0                           no timezone\r\n");
      return;
    }
    if (!save_tz(g_rtc.tz))
      Serial.print("could not save the timezone: it is lost at power-off\r\n");
    update_display(true);
  }
  const time_t now = time(nullptr);
  Serial.printf("tz %s\r\nnow %s, ", g_rtc.tz,
                tz::is_dst(g_zone, now) ? g_zone.dst_name : g_zone.std_name);
  print_offset(tz::utc_offset(g_zone, now));
  Serial.print("\r\n");
  int64_t start, end;
  if (tz::transitions(g_zone, tz::to_civil(g_zone, now).year, start, end)) {
    print_stamp("DST starts", static_cast<time_t>(start));
    Serial.print(",");
    print_stamp(" ends", static_cast<time_t>(end));
    Serial.print("\r\n");
  }
  print_time();
}

void handle_line(const String& line) {
  int y, mo, d, h, mi;
  if (line.startsWith("set ") &&
      sscanf(line.c_str() + 4, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
    // Local wall clock -> UTC. In the hour skipped in spring the time moves an
    // hour on (02:30 -> 03:30); in the repeated autumn hour the first one wins.
    set_clock(static_cast<time_t>(tz::to_utc(g_zone, make_time(y, mo, d, h, mi, 0))));
    print_rtc();
    update_display(true);
  } else if (line == "rtc") {
    print_rtc();
  } else if (line == "tz" || line.startsWith("tz ")) {
    String arg = line.substring(2);
    arg.trim();
    handle_tz(arg);
  } else if (line == "log") {
    print_log();
  } else if (line == "temp") {
    measure_temp();
    print_temp();
  } else if (line == "offset" || line.startsWith("offset ")) {
    String arg = line.substring(6);
    arg.trim();
    handle_offset(arg);
  } else if (line.startsWith("reinit ")) {
    const long n = line.substring(7).toInt();
    if (n >= 0 && n <= 60) {
      g_rtc.reinit_min = static_cast<uint8_t>(n);
      Serial.printf("panel restart every %ld min%s\r\n", n, n == 0 ? " (off)" : "");
    } else {
      Serial.print("reinit takes 0-60\r\n");
    }
  } else if (line == "soft 1" || line == "soft 0") {
    g_rtc.soft_setup = line == "soft 1";
    Serial.printf("panel setup re-sent on minute wakes: %s\r\n", g_rtc.soft_setup ? "on" : "off");
  } else if (line == "init") {
    g_lcd.begin(true);
    update_display(true);
    Serial.print("panel restarted and redrawn\r\n");
  } else if (line == "sleep") {
    // Minute wakes go straight back to sleep whatever the power source, so
    // this runs the battery sleep cycle on USB power. KEY stays awake on USB.
    Serial.print("deep sleep; the terminal will disconnect. Press KEY to come back.\r\n");
    Serial.flush();
    delay(100);
    deep_sleep();
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
  // Runs while the RTC sync and panel setup happen; read before the redraw.
  const room_temp::Status temp_started = room_temp::start();

  const uint64_t ext1 = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  const bool key = ext1 & (1ULL << kKeyPin);
  const bool woke =
      (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_EXT1) && g_rtc.magic == kMagic;
  rtc::Status rtc_status = rtc::Status::Ok;
  if (woke) {
    if (!use_tz(g_rtc.tz)) load_tz();  // RTC copy from older firmware: read NVS
    g_rtc.wakes++;
    if (ext1 & (1ULL << rtc::kIntPin)) g_rtc.rtc_wakes++;
    if (cause == ESP_SLEEP_WAKEUP_TIMER) g_rtc.timer_wakes++;
    rtc_status = sync_from_rtc();
    if (rtc_status == rtc::Status::NoDevice) g_rtc.rtc_errors++;
    finish_temp(temp_started);
    const bool reinit =
        !key && g_rtc.reinit_min != 0 && current_face().minute % g_rtc.reinit_min == 0;
    g_lcd.begin(reinit);
    if (!reinit && g_rtc.soft_setup) g_lcd.reapply_setup();
    const uint8_t regions = reinit ? (update_display(true), kReinit) : update_display(false);
    log_wake(key                               ? 'K'
             : cause == ESP_SLEEP_WAKEUP_TIMER ? 'T'
             : ext1 & (1ULL << rtc::kIntPin)   ? 'R'
                                               : '?',
             rtc_status, regions);
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
    g_rtc.reinit_min = kDefaultReinitMin;
    g_rtc.soft_setup = kDefaultSoftSetup;
    memcpy(g_rtc.log, saved_log, sizeof saved_log);
    g_rtc.temp_offset_dc = load_temp_offset();
    load_tz();
    finish_temp(temp_started);
    rtc_status = sync_from_rtc();
    // A reset keeps the system clock; a power-on starts it at 1970.
    if (rtc_status != rtc::Status::Ok && time(nullptr) < kMinValidTime) set_clock_from_build();
    g_lcd.begin(true);
    update_display(true);
    log_wake('P', rtc_status, kFullRefresh);
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
  static uint32_t last_temp_ms = millis();
  if (millis() - last_temp_ms >= kTempPeriodMs) {
    last_temp_ms = millis();
    measure_temp();
  }
  if (current_face() != g_rtc.shown) update_display(false);
  if (!HWCDC::isPlugged()) {
    delay(200);  // USB unplugged: settle, then run on battery
    if (!HWCDC::isPlugged()) deep_sleep();
  }
  delay(50);
}
