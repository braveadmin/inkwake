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
// "Time not set" until `set` over serial or an NTP sync writes it. The RTC's
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
//
// NTP (#13): at power-on and then every `ntp every H` hours (default 24) the
// wake that finds a sync due turns Wi-Fi on, asks the NTP server once, turns
// Wi-Fi off, and writes the corrected UTC time to the RTC. Failed tries retry
// with a growing delay (lib/ntp), sooner while the time is not set. Without
// Wi-Fi the clock just keeps running from the RTC.
//
// Wi-Fi setup (#20, ADR-0016): at power-on with no network saved, on KEY held
// 5 s, or `setup` over serial, the clock opens the open network inkwake-XXXX
// with a captive settings page and shows a QR code that joins it. Setup ends
// when a network connects (then NTP syncs at once), after 10 min without page
// activity, or on a KEY press. A failed sync later only shows a small Wi-Fi "!"
// mark on the face; setup never reopens by itself. The `wifi` serial command
// stays as a fallback.
//
// Saved networks (#33): up to 5, in NVS as one list in try order. A sync tries
// the first, and if it fails, the others a scan finds in range; the one that
// connects moves to the top. The setup page can remove networks or move them up.
//
// Alarms (#2, ADR-0017): up to 16, kept in NVS and copied into RTC memory.
// Every wake checks which alarm came due since the last check (lib/alarm_sched,
// UTC time line, DST policy there). A due alarm keeps the board awake and
// beeps through the ES8311 speaker, louder over time: KEY tap = snooze 9 min,
// KEY held 2 s = stop; 10 min unattended = auto-snooze, gives up after the
// third snooze. A snooze sleeps with a timer that ends it on time. The next
// alarm (or the snooze end) shows on the bottom line of the face.
#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "alarm.h"
#include "alarm_sched.h"
#include "audio.h"
#include "battery.h"
#include "clock_face.h"
#include "framebuffer.h"
#include "ntp.h"
#include "ntp_sync.h"
#include "pcf85063.h"
#include "portal.h"
#include "provision.h"
#include "qrcodegen.h"
#include "room_temp.h"
#include "rtc.h"
#include "setup_screen.h"
#include "shtc3.h"
#include "st7305.h"
#include "tz.h"
#include "wifi_list.h"

using namespace inkwake;

namespace {

constexpr int kKeyPin = 18;                   // active low, external pull-up; wakes from deep sleep
constexpr int kAmpPin = 46;                   // speaker amp enable, held low in sleep
constexpr int kBatPin = 4;                    // battery sense, 1:3 divider
constexpr uint32_t kMagic = 0x1A7E0021;       // changed with the RtcState layout
constexpr time_t kMinValidTime = 1735689600;  // 2025-01-01: older means the clock was reset
constexpr uint32_t kWakeMarginUs = 20'000;    // timer-only wake: just after the minute turns
// Fallback timer when the RTC interrupt is armed. Late enough that the ESP32's
// deep-sleep clock (internal RC, a few % off) never fires it before the minute.
constexpr uint32_t kFallbackUs = 10'000'000;
// Fallback timer while the time is not set (#27). The system clock then runs
// from the build time, unrelated to the RTC's minute, so a timer aimed at the
// next system minute fired between two RTC ticks: two wakes a minute. Instead
// it counts from now, longer than a minute plus the RC clock's error, and only
// fires if the interrupt is missed. The face shows --:--, so timing is moot.
constexpr uint64_t kUnsetFallbackUs = 70'000'000;

// One deep-sleep wake (or power-on), for diagnosing battery runs afterwards
// over serial (`log`).
struct WakeEntry {
  uint32_t when;      // system time after the RTC sync (Unix, UTC)
  char cause;         // R = RTC interrupt, T = fallback timer, K = KEY, P = power-on
  uint8_t rtc;        // rtc::Status of the sync
  uint8_t regions;    // regions redrawn, kFullRefresh / kReinit
  char ntp_result;    // 0 = no sync on this wake, else ntp_sync::status_letter()
  uint16_t awake_ms;  // from boot to the display update done
  uint16_t bat_mv;
  char alarm;  // A = an alarm started ringing, S = a snooze ended, 0 = none
};
constexpr size_t kLogLen = 32;
constexpr uint8_t kFullRefresh = 0xFF;
constexpr uint8_t kReinit = 0xFE;  // panel reset + init + full frame
// Panel freezes and latch-ups on battery (#25). Frames are written in low
// power mode, with no mode switch per wake (the old switch broke the
// datasheet's timing). The panel cannot be read back, so a scheduled restart
// (sleep in, reset, setup, full frame) every `reinit N` minutes bounds how
// long a freeze lasts. Defaults after power-on:
constexpr uint8_t kDefaultReinitMin = 60;
constexpr St7305::WriteMode kDefaultPanelMode = St7305::WriteMode::LowPower;
constexpr uint32_t kTempPeriodMs = 60'000;      // while awake on USB
constexpr uint32_t kKeyHoldMs = 5'000;          // KEY held this long opens Wi-Fi setup
constexpr uint32_t kSetupIdleMs = 10 * 60'000;  // setup closes after this without page activity
constexpr char kPrefsNamespace[] = "inkwake";
constexpr char kPrefTempOffset[] = "temp_off";               // int16, tenths of a degree C
constexpr char kPrefTz[] = "tz";                             // POSIX TZ string
constexpr size_t kTzLen = 64;                                // including the terminating NUL
constexpr char kDefaultTz[] = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe
constexpr char kPrefWifiList[] = "wifi_list";                // blob, provision::encode (#33)
// Single network saved by firmware before #33: moved into the list on first load.
constexpr char kPrefWifiSsid[] = "wifi_ssid";
constexpr char kPrefWifiPass[] = "wifi_pass";
constexpr char kPrefNtpServer[] = "ntp_server";
constexpr char kPrefNtpHours[] = "ntp_hours";  // uint16
constexpr char kDefaultNtpServer[] = "pool.ntp.org";
constexpr char kPrefRtcOffset[] = "rtc_offset";  // int8, PCF85063 offset steps (#13)
// A sync measures the RTC's drift only if the system clock was copied from the
// RTC this recently: while awake the system clock runs on the ESP32's own
// crystal, which drifts on its own.
constexpr uint32_t kTrimFreshMs = 15 * 60'000;
constexpr uint16_t kDefaultNtpHours = 24;  // crystal drift ~2 s/day at most; ADR-0013
constexpr uint16_t kMaxNtpHours = 168;
constexpr size_t kSsidLen = 33;  // 32 + NUL
constexpr size_t kPassLen = 64;  // WPA2 passphrase up to 63 + NUL
constexpr size_t kHostLen = 64;
constexpr char kPrefAlarms[] = "alarms";  // sched::encode blob
constexpr size_t kAlarmBlobMax = 4 + sched::kMaxAlarms * (8 + sched::kMaxLabel + sched::kMaxSound);
constexpr uint32_t kDismissHoldMs = 2'000;  // KEY held this long stops a ringing or snoozed alarm
constexpr uint8_t kRingStartPct = 50;       // beep loudness ramp, audio::beep scale (dB-like)
constexpr uint8_t kRingMaxPct = 90;
constexpr uint8_t kTestAlarmId = 0;  // `alarm test`

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
  uint8_t panel_mode;    // St7305::WriteMode
  uint32_t log_count;    // entries ever written; the newest is at (log_count - 1) % kLogLen
  WakeEntry log[kLogLen];
  // Added after `log` so the log, kept across resets, stays where it was.
  int16_t temp_raw_dc;     // last SHTC3 reading without offset, kNoTemp if it failed
  uint16_t temp_rh_dpct;   // humidity of the same reading (sensor, not corrected)
  int16_t temp_offset_dc;  // calibration, copy of the NVS value
  uint8_t temp_status;     // room_temp::Status of the last reading
  uint32_t temp_errors;    // failed readings since power-on
  char tz[kTzLen];         // POSIX TZ string, copy of the NVS value
  // NTP (#13). Server and Wi-Fi credentials are read from NVS only to sync.
  ntp::Schedule ntp_schedule;
  uint16_t ntp_hours;     // sync interval, copy of the NVS value
  bool wifi_configured;   // an SSID is saved
  uint8_t ntp_status;     // ntp_sync::Status of the last try, 0 = none since power-on
  uint8_t ntp_reply;      // ntp::Reply of the last bad reply
  int32_t ntp_offset_ms;  // correction applied by the last good sync
  uint16_t ntp_rtt_ms;
  uint16_t ntp_total_ms;  // last try, Wi-Fi on to off
  uint16_t ntp_wifi_ms;   // last try, until Wi-Fi connected
  uint32_t ntp_ok;        // good syncs since power-on
  uint32_t ntp_failed;    // failed tries since power-on
  // Battery (#36), read on every wake.
  uint16_t bat_mv;    // last averaged reading
  uint8_t bat_level;  // battery::Level, with hysteresis
  // Alarms (#2): copy of the NVS blob, decoded into g_alarms on every boot.
  uint16_t alarm_blob_len;
  uint8_t alarm_blob[kAlarmBlobMax];
  int64_t alarm_checked;  // UTC up to which due alarms were handled; 0 = not yet
  Ringer ring;            // plain data, all-zero = idle
  // RTC drift trim (#13).
  int8_t rtc_offset;      // offset register steps in force, copy of the NVS value
  int64_t trim_base;      // UTC of the last sync that set the RTC exactly; 0 = none
  int32_t drift_ppm_x10;  // last measured drift, + = RTC fast; kNoDrift = none yet
  int64_t drift_at;       // UTC of that measurement
};
constexpr int32_t kNoDrift = INT32_MIN;
// `= {}` makes this a constant initialization. Without it, FaceState's member
// defaults can turn it into startup code that runs again on every deep-sleep
// wake, resetting `shown` (magic survives) so each wake redrew every field.
RTC_DATA_ATTR RtcState g_rtc = {};

Framebuffer g_fb;
St7305 g_lcd;
tz::Zone g_zone;              // parsed from g_rtc.tz on every boot and wake
std::vector<Alarm> g_alarms;  // decoded from g_rtc.alarm_blob on every boot and wake

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
  // Last sync failed (Wi-Fi, DNS or server): small mark until one succeeds.
  const auto ntp = static_cast<ntp_sync::Status>(g_rtc.ntp_status);
  s.wifi_failed = g_rtc.wifi_configured && g_rtc.ntp_status != 0 && ntp != ntp_sync::Status::Ok &&
                  ntp != ntp_sync::Status::NoWifiConfig;
  s.battery = g_rtc.bat_level;
  if (g_rtc.time_set) {
    const int64_t now = time(nullptr);
    int64_t at = 0;
    if (g_rtc.ring.state == RingState::Snoozed) {
      at = g_rtc.ring.resume_at;
      s.alarm = FaceState::kAlarmSnoozed;
    } else if (const auto u = sched::upcoming(g_alarms, g_zone, now)) {
      at = u->fire_utc;
      s.alarm = FaceState::kAlarmNext;
    }
    if (s.alarm != FaceState::kAlarmNone) {
      const tz::Civil c = tz::to_civil(g_zone, at);
      s.alarm_hour = c.hour;
      s.alarm_minute = c.minute;
      s.alarm_weekday = at - now > 86400 ? c.weekday : FaceState::kSoon;
    }
  }
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

// millis() when the system clock was copied from the RTC right at a second
// edge (minute-interrupt wake), 0 = not on this boot. Other reads land
// anywhere in the second, up to 1 s off: too coarse to measure drift.
uint32_t g_clock_from_rtc_ms = 0;

// Copies the RTC time into the system clock. On a minute-interrupt wake the
// RTC has just turned the minute, so the lost fraction of a second is tiny.
// No answer on I2C keeps the system clock and the previous time_set.
rtc::Status sync_from_rtc() {
  time_t t;
  const rtc::Status s = rtc::read(t);
  if (s == rtc::Status::Ok) {
    set_system_clock(t);
  }
  if (s != rtc::Status::NoDevice) g_rtc.time_set = s == rtc::Status::Ok;
  if (s == rtc::Status::TimeLost) g_rtc.trim_base = 0;
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
  g_rtc.alarm_checked = 0;  // a jump in time rings nothing it skipped over
  g_rtc.trim_base = 0;      // set by hand: not exact enough to measure drift from
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

// NVS string into buf; empty when missing.
void load_pref_string(const char* key, char* buf, size_t n) {
  buf[0] = '\0';
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return;
  if (prefs.isKey(key)) prefs.getString(key, buf, n);
  prefs.end();
}

bool save_pref_string(const char* key, const char* value) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok = prefs.putString(key, value) == strlen(value);
  prefs.end();
  return ok;
}

bool save_networks(const provision::WifiList& nets) {
  uint8_t blob[provision::kBlobMax];
  const size_t n = provision::encode(nets, blob, sizeof blob);
  Preferences prefs;
  if (n == 0 || !prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok = prefs.putBytes(kPrefWifiList, blob, n) == n;
  prefs.end();
  memset(blob, 0, sizeof blob);
  if (ok) g_rtc.wifi_configured = nets.count > 0;
  return ok;
}

// Saved networks, in try order. A network saved by older firmware (one
// ssid/pass pair) is moved into the list the first time.
void load_networks(provision::WifiList& nets) {
  nets = {};
  uint8_t blob[provision::kBlobMax];
  size_t n = 0;
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return;  // nothing saved yet
  const bool has_list = prefs.isKey(kPrefWifiList);
  if (has_list) n = prefs.getBytes(kPrefWifiList, blob, sizeof blob);
  const bool has_old = prefs.isKey(kPrefWifiSsid);
  prefs.end();
  if (has_list) {
    provision::decode(blob, n, nets);  // a broken blob reads as no networks
    memset(blob, 0, sizeof blob);
    return;
  }
  if (!has_old) return;
  char ssid[kSsidLen], pass[kPassLen];
  load_pref_string(kPrefWifiSsid, ssid, sizeof ssid);
  load_pref_string(kPrefWifiPass, pass, sizeof pass);
  provision::remember(nets, ssid, pass);  // skipped if invalid; no password = open, as before
  memset(pass, 0, sizeof pass);
  if (save_networks(nets) && prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kPrefWifiSsid);
    prefs.remove(kPrefWifiPass);
    prefs.end();
  }
}

void load_ntp_config() {
  provision::WifiList nets;
  load_networks(nets);
  g_rtc.wifi_configured = nets.count > 0;
  memset(&nets, 0, sizeof nets);
  uint16_t h = kDefaultNtpHours;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    h = prefs.getUShort(kPrefNtpHours, kDefaultNtpHours);
    prefs.end();
  }
  g_rtc.ntp_hours = h >= 1 && h <= kMaxNtpHours ? h : kDefaultNtpHours;
}

int8_t load_rtc_offset() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return 0;
  const int8_t v = prefs.getChar(kPrefRtcOffset, 0);
  prefs.end();
  return v >= pcf85063::kOffsetMin && v <= pcf85063::kOffsetMax ? v : 0;
}

// Writes the offset to the chip and NVS. The drift measured so far was taken
// with the old value, so measuring starts again at the next sync.
bool set_rtc_offset(int steps) {
  g_rtc.trim_base = 0;
  if (!rtc::set_offset(steps)) return false;
  g_rtc.rtc_offset = static_cast<int8_t>(steps);
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok = prefs.putChar(kPrefRtcOffset, static_cast<int8_t>(steps)) == 1;
  prefs.end();
  return ok;
}

// After a good sync, before the time is corrected: compares the RTC with NTP
// time since the previous exact sync and trims the offset register if it drifted.
void measure_drift(int64_t ntp_offset_us) {
  const int64_t now = time(nullptr);
  if (g_rtc.trim_base == 0 || g_clock_from_rtc_ms == 0 ||
      millis() - g_clock_from_rtc_ms > kTrimFreshMs || now <= g_rtc.trim_base) {
    return;
  }
  const int64_t elapsed = now - g_rtc.trim_base;
  const int64_t rtc_error_us = -ntp_offset_us;  // the sync adds offset: RTC was ahead by -offset
  if (elapsed >= pcf85063::kTrimMinSpanS) {
    g_rtc.drift_ppm_x10 = static_cast<int32_t>(rtc_error_us * 10 / elapsed);
    g_rtc.drift_at = now;
  }
  int next;
  if (pcf85063::trim_offset(g_rtc.rtc_offset, rtc_error_us, elapsed, next)) set_rtc_offset(next);
}

int64_t ntp_interval_s() {
  const uint16_t h = g_rtc.ntp_hours != 0 ? g_rtc.ntp_hours : kDefaultNtpHours;
  return static_cast<int64_t>(h) * 3600;
}

// Adds the NTP correction to the system clock, then writes the RTC at the
// next whole second (a write restarts the RTC's second), so both agree to
// about a millisecond. Returns the RTC write result.
bool apply_ntp_offset(int64_t offset_us) {
  timeval tv;
  gettimeofday(&tv, nullptr);
  const int64_t now = static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec + offset_us;
  tv.tv_sec = static_cast<time_t>(now / 1000000);
  tv.tv_usec = static_cast<suseconds_t>(now % 1000000);
  settimeofday(&tv, nullptr);
  const time_t s = time(nullptr);
  while (time(nullptr) == s) delay(1);
  const bool ok = rtc::write(time(nullptr));
  // A failed write leaves the RTC flagged: the next wake reads "time lost"
  // and the schedule tries again soon.
  g_rtc.time_set = ok;
  return ok;
}

// One NTP try now, recorded in the schedule and the counters. Returns the log letter.
char run_ntp_sync() {
  static provision::WifiList nets;  // static: kept off the stack
  char server[kHostLen];
  load_networks(nets);
  load_pref_string(kPrefNtpServer, server, sizeof server);
  if (server[0] == '\0') snprintf(server, sizeof server, "%s", kDefaultNtpServer);
  const ntp_sync::Result r = ntp_sync::run(nets, server);
  // The network that connected is tried first next time (#33).
  if (r.network > 0 && provision::promote(nets, static_cast<size_t>(r.network))) {
    save_networks(nets);
  }
  memset(&nets, 0, sizeof nets);
  bool ok = r.status == ntp_sync::Status::Ok;
  if (ok) {
    measure_drift(r.offset_us);
    ok = apply_ntp_offset(r.offset_us);
    g_rtc.trim_base = ok ? time(nullptr) : 0;
    g_rtc.ntp_offset_ms = static_cast<int32_t>(r.offset_us / 1000);
    g_rtc.ntp_rtt_ms = static_cast<uint16_t>(r.rtt_us / 1000);
  }
  g_rtc.ntp_status = static_cast<uint8_t>(r.status);
  g_rtc.ntp_reply = static_cast<uint8_t>(r.reply);
  g_rtc.ntp_total_ms = static_cast<uint16_t>(r.total_ms);
  g_rtc.ntp_wifi_ms = static_cast<uint16_t>(r.wifi_ms);
  if (r.status != ntp_sync::Status::NoWifiConfig) {
    ntp::record(g_rtc.ntp_schedule, time(nullptr), ok);
    if (ok) {
      g_rtc.ntp_ok++;
    } else {
      g_rtc.ntp_failed++;
    }
  }
  // X: the server answered but the RTC did not take the time.
  return r.status == ntp_sync::Status::Ok && !ok ? 'X' : ntp_sync::status_letter(r.status);
}

bool battery_critical() {
  return static_cast<battery::Level>(g_rtc.bat_level) == battery::Level::Critical;
}

// Syncs if Wi-Fi is set up and the schedule says so. 0 if nothing ran.
// Skipped on a critical battery: the Wi-Fi current peak could brown the board
// out, and a flat cell loses the RTC time anyway.
char maybe_ntp_sync() {
  if (!g_rtc.wifi_configured || battery_critical()) return 0;
  if (!ntp::due(g_rtc.ntp_schedule, time(nullptr), g_rtc.time_set, ntp_interval_s())) return 0;
  return run_ntp_sync();
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

// Averaged reading into g_rtc and the level updated. Call before the redraw.
void measure_battery() {
  constexpr size_t kSamples = 4;
  uint16_t mv[kSamples];
  for (uint16_t& v : mv) v = battery_mv();
  g_rtc.bat_mv = battery::average_mv(mv, kSamples);
  g_rtc.bat_level = static_cast<uint8_t>(
      battery::classify(static_cast<battery::Level>(g_rtc.bat_level), g_rtc.bat_mv));
}

void log_wake(char cause, rtc::Status rtc_status, uint8_t regions, char ntp_letter,
              char alarm = 0) {
  WakeEntry& e = g_rtc.log[g_rtc.log_count++ % kLogLen];
  e.when = static_cast<uint32_t>(time(nullptr));
  e.cause = cause;
  e.rtc = static_cast<uint8_t>(rtc_status);
  e.regions = regions;
  e.ntp_result = ntp_letter;
  e.awake_ms = static_cast<uint16_t>(millis());
  e.bat_mv = g_rtc.bat_mv;
  e.alarm = alarm;
}

// Light sleep for the panel's datasheet waits on battery minute wakes, instead
// of burning ~40 mA in delay(). DC/CS/RST are held so the panel sees no glitch.
void nap(uint32_t ms) {
  St7305::hold_pins(true);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000);
  esp_light_sleep_start();
  St7305::hold_pins(false);
}

// Restarts the panel (sleep in, reset, setup) and redraws everything.
void restart_panel() {
  g_lcd.recover();
  update_display(true);
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
  uint64_t us = armed && !g_rtc.time_set
                    ? kUnsetFallbackUs
                    : static_cast<uint64_t>(59 - tv.tv_sec % 60) * 1'000'000 +
                          (1'000'000 - tv.tv_usec) + (armed ? kFallbackUs : kWakeMarginUs);
  // A snooze ends between minute wakes: wake for it on time.
  if (g_rtc.ring.state == RingState::Snoozed) {
    const int64_t left_s = g_rtc.ring.resume_at - tv.tv_sec;
    const uint64_t snooze_us =
        left_s <= 0 ? 1'000 : static_cast<uint64_t>(left_s) * 1'000'000 - tv.tv_usec;
    if (snooze_us < us) us = snooze_us;
  }

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

// ---- Alarms (#2) ----

void use_alarms(const std::vector<Alarm>& v) {
  const std::vector<uint8_t> b = sched::encode(v);
  memcpy(g_rtc.alarm_blob, b.data(), b.size());  // <= kAlarmBlobMax: sched::check limits
  g_rtc.alarm_blob_len = static_cast<uint16_t>(b.size());
  g_alarms = v;
}

// RTC copy -> g_alarms; empty if the copy is damaged.
void alarms_from_rtc() {
  std::vector<Alarm> v;
  if (g_rtc.alarm_blob_len > kAlarmBlobMax ||
      !sched::decode(g_rtc.alarm_blob, g_rtc.alarm_blob_len, v)) {
    v.clear();
  }
  g_alarms = v;
}

void load_alarms() {
  static uint8_t buf[kAlarmBlobMax];
  size_t n = 0;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    if (prefs.isKey(kPrefAlarms)) n = prefs.getBytes(kPrefAlarms, buf, sizeof buf);
    prefs.end();
  }
  std::vector<Alarm> v;
  if (n == 0 || !sched::decode(buf, n, v)) v.clear();
  use_alarms(v);
}

bool save_alarms(const std::vector<Alarm>& v) {
  use_alarms(v);
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  const bool ok =
      prefs.putBytes(kPrefAlarms, g_rtc.alarm_blob, g_rtc.alarm_blob_len) == g_rtc.alarm_blob_len;
  prefs.end();
  return ok;
}

// Starts an alarm that came due since the last check, or rings again after a
// snooze. Returns the log letter (A, S) or 0.
char check_alarms() {
  const int64_t now = time(nullptr);
  char letter = 0;
  if (!g_rtc.time_set) {
    g_rtc.alarm_checked = 0;  // the system clock is a guess: nothing to ring
    return 0;
  }
  const int64_t prev = g_rtc.alarm_checked;
  if (prev == 0 || now > prev) g_rtc.alarm_checked = now;
  if (prev != 0 && now > prev) {
    if (const auto d = sched::due(g_alarms, g_zone, prev, now)) {
      Alarm& a = g_alarms[d->index];
      ring_trigger(g_rtc.ring, a.id, now);
      letter = 'A';
      if (a.days == kOneShot) {  // rings once, then turns itself off
        std::vector<Alarm> v = g_alarms;
        v[d->index].enabled = false;
        save_alarms(v);
      }
    }
  }
  if (ring_tick(g_rtc.ring, kDefaultRing, now) == RingEvent::Restart) letter = 'S';
  return letter;
}

void ring_title(char* buf, size_t n) {
  const uint8_t id = g_rtc.ring.alarm_id;
  const auto i = sched::find(g_alarms, id);
  if (id == kTestAlarmId) {
    snprintf(buf, n, "Test alarm");
  } else if (i && !g_alarms[*i].label.empty()) {
    snprintf(buf, n, "%s", g_alarms[*i].label.c_str());
  } else {
    snprintf(buf, n, "Alarm");
  }
}

uint8_t show_ringing() {
  char title[sched::kMaxLabel + 1];
  ring_title(title, sizeof title);
  const tz::Civil c = tz::to_civil(g_zone, time(nullptr));
  render_ringing(g_fb, c.hour, c.minute, title);
  g_lcd.refresh_full(g_fb);
  return c.minute;
}

// Beeps until the alarm is snoozed, stopped or gives up, then redraws the face.
// A KEY press already down when ringing starts counts only after its release.
void ring_loop() {
  const bool sound = audio::begin();
  if (HWCDC::isPlugged()) {
    Serial.printf("alarm ringing%s: tap KEY to snooze, hold 2 s to stop\r\n",
                  sound ? "" : " (no sound: speaker codec did not answer)");
  }
  uint8_t shown_min = show_ringing();
  uint32_t pos_ms = 0;
  bool was_down = digitalRead(kKeyPin) == LOW;
  uint32_t down_ms = 0;  // 0 = no press counted
  while (g_rtc.ring.state == RingState::Ringing) {
    const int64_t now = time(nullptr);
    const auto elapsed = static_cast<uint32_t>(now - g_rtc.ring.since);
    audio::beep(100, ring_volume(elapsed, kRingStartPct, kRingMaxPct), pos_ms);
    const bool down = digitalRead(kKeyPin) == LOW;
    if (down && !was_down) down_ms = millis() | 1;
    if (down && down_ms != 0 && millis() - down_ms >= kDismissHoldMs) {
      ring_dismiss(g_rtc.ring);
    } else if (!down && was_down && down_ms != 0) {
      ring_snooze(g_rtc.ring, kDefaultRing, now);  // ignored past the last snooze
      down_ms = 0;
    }
    was_down = down;
    ring_tick(g_rtc.ring, kDefaultRing, now);
    const tz::Civil c = tz::to_civil(g_zone, time(nullptr));
    if (g_rtc.ring.state == RingState::Ringing && c.minute != shown_min) shown_min = show_ringing();
  }
  audio::end();
  // A stopping hold is not also the start of a 5 s setup hold.
  while (digitalRead(kKeyPin) == LOW) delay(10);
  if (HWCDC::isPlugged()) {
    Serial.print(g_rtc.ring.state == RingState::Snoozed ? "alarm snoozed\r\n"
                                                        : "alarm stopped\r\n");
  }
  update_display(true);
}

// Checks alarms and rings if one is due. Returns the log letter.
char handle_alarms() {
  const char letter = check_alarms();
  if (g_rtc.ring.state == RingState::Ringing) ring_loop();
  return letter;
}

// ---- Wi-Fi setup (#20) ----

void ntp_sync_soon();

void show_setup(portal::Screen screen, const char* ap, const char* ssid) {
  switch (screen) {
    case portal::Screen::Setup:
    case portal::Screen::Failed: {
      char text[64];
      static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
      static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
      if (provision::wifi_qr_text(ap, text, sizeof text) &&
          qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, 1, 10, qrcodegen_Mask_AUTO,
                               true)) {
        render_setup(g_fb, qr, ap, screen == portal::Screen::Failed ? kRetryTitle : kSetupTitle);
      } else {
        render_message(g_fb, "Join Wi-Fi", ap);
      }
      break;
    }
    case portal::Screen::Connecting:
      render_message(g_fb, "Connecting to", ssid);
      break;
    case portal::Screen::Connected:
      render_message(g_fb, "Connected to", ssid);
      break;
  }
  g_lcd.refresh_full(g_fb);
}

void setup_load(provision::WifiList& nets, char* tz, size_t tz_n, bool& tz_saved) {
  load_networks(nets);
  snprintf(tz, tz_n, "%s", g_rtc.tz);
  tz_saved = false;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    tz_saved = prefs.isKey(kPrefTz);
    prefs.end();
  }
}

bool setup_apply_tz(const char* posix) { return use_tz(posix) && save_tz(g_rtc.tz); }

// The phone's clock, good to a second or so: only used while the clock has no
// time at all; NTP corrects it right after setup.
void setup_offer_time(time_t utc) {
  if (!g_rtc.time_set && utc >= kMinValidTime) set_clock(utc);
}

// KEY, or an alarm coming due: setup closes and run_setup() rings it.
bool setup_cancel() {
  check_alarms();
  return digitalRead(kKeyPin) == LOW || g_rtc.ring.state == RingState::Ringing;
}

void setup_alarms_json(char* buf, size_t n) {
  static const char* const kDay[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const int64_t now = time(nullptr);
  std::string out = "[";
  for (const Alarm& a : g_alarms) {
    char label[sched::kMaxLabel * 6 + 1], next[16] = "", item[sizeof label + 160];
    provision::json_escape(a.label.c_str(), label, sizeof label);
    if (const auto f = g_rtc.time_set ? sched::next_fire(a, g_zone, now) : std::nullopt) {
      const tz::Civil t = tz::to_civil(g_zone, *f);
      snprintf(next, sizeof next, "%s %02u:%02u", kDay[t.weekday % 7], t.hour, t.minute);
    }
    snprintf(item, sizeof item,
             "%s{\"id\":%u,\"time\":\"%02u:%02u\",\"days\":\"%s\",\"mask\":%u,\"on\":%s,"
             "\"label\":\"%s\",\"next\":\"%s\"}",
             out.size() > 1 ? "," : "", a.id, a.hour, a.minute, sched::format_days(a.days).c_str(),
             a.days, a.enabled ? "true" : "false", label, next);
    if (out.size() + strlen(item) + 2 > n) break;  // cannot happen with 16 alarms in 4 KB
    out += item;
  }
  out += "]";
  snprintf(buf, n, "%s", out.c_str());
}

// POST /api/alarms: op=add|save|del, id, time=HH:MM, days (parse_days form), label, on=1|0.
const char* setup_alarm_edit(const char* form) {
  char op[8] = "", id_s[8] = "", time_s[8] = "", days_s[48] = "", on_s[4] = "1";
  char label[sched::kMaxLabel + 1] = "";
  provision::form_value(form, "op", op, sizeof op);
  provision::form_value(form, "id", id_s, sizeof id_s);
  std::vector<Alarm> v = g_alarms;
  Alarm* a = nullptr;
  if (strcmp(op, "add") == 0) {
    if (v.size() >= sched::kMaxAlarms) return "16 alarms already: delete one first";
    v.emplace_back();
    a = &v.back();
    a->id = sched::free_id(g_alarms);
  } else {
    const long id = strtol(id_s, nullptr, 10);
    const auto i = id > 0 && id < 256 ? sched::find(v, static_cast<uint8_t>(id)) : std::nullopt;
    if (!i) return "alarm not found";
    if (strcmp(op, "del") == 0) {
      v.erase(v.begin() + static_cast<long>(*i));
    } else if (strcmp(op, "save") == 0) {
      a = &v[*i];
    } else {
      return "unknown operation";
    }
  }
  if (a != nullptr) {
    if (!provision::form_value(form, "time", time_s, sizeof time_s) ||
        !sched::parse_time(time_s, a->hour, a->minute)) {
      return "time as HH:MM";
    }
    if (!provision::form_value(form, "days", days_s, sizeof days_s) ||
        !sched::parse_days(days_s, a->days)) {
      return "bad days";
    }
    if (!provision::form_value(form, "label", label, sizeof label)) return "label too long";
    a->label = label;
    provision::form_value(form, "on", on_s, sizeof on_s);
    a->enabled = strcmp(on_s, "0") != 0;
    if (const char* why = sched::check(*a)) return why;
  }
  if (!save_alarms(v)) return "saved until power-off only (NVS write failed)";
  return nullptr;
}

const portal::Hooks kSetupHooks = {show_setup,        setup_load,      setup_apply_tz,
                                   setup_offer_time,  save_networks,   setup_cancel,
                                   setup_alarms_json, setup_alarm_edit};

// True if KEY stays down until `until_ms` after boot.
bool key_held_until(uint32_t until_ms) {
  while (digitalRead(kKeyPin) == LOW) {
    if (millis() >= until_ms) return true;
    delay(10);
  }
  return false;
}

// Runs setup mode, then syncs if a network was set and redraws the face.
// Returns the NTP log letter (0 if no sync ran).
char run_setup(bool verbose) {
  while (digitalRead(kKeyPin) == LOW) delay(10);  // the hold that opened it is no cancel
  delay(50);                                      // debounce the release
  if (verbose) {
    Serial.print("Wi-Fi setup: join the inkwake-XXXX network shown on the screen\r\n");
    Serial.flush();
  }
  const portal::Outcome o = portal::run(kSetupHooks, kSetupIdleMs);
  if (verbose) Serial.printf("Wi-Fi setup %s\r\n", portal::outcome_name(o));
  char letter = 0;
  if (o == portal::Outcome::Connected) {
    ntp_sync_soon();
    letter = run_ntp_sync();
  }
  while (digitalRead(kKeyPin) == LOW) delay(10);  // a press that cancelled is no new hold
  update_display(true);
  if (g_rtc.ring.state == RingState::Ringing) ring_loop();
  return letter;
}

void print_help() {
  Serial.print(
      "\r\ninkwake clock (M3)\r\n"
      "  set YYYY-MM-DD HH:MM   set the clock, in local time (the RTC keeps UTC)\r\n"
      "  tz [POSIX-TZ]          show or set the timezone, e.g. tz CET-1CEST,M3.5.0,M10.5.0/3\r\n"
      "  rtc                    RTC time and wake counters\r\n"
      "  alarm                  list alarms; `alarm help` for add, edit, test\r\n"
      "  setup                  Wi-Fi setup page (same as holding KEY 5 s)\r\n"
      "  wifi                   saved Wi-Fi networks (up to 5), in the order they are tried\r\n"
      "  wifi ssid NAME         start adding a network (spaces allowed), then:\r\n"
      "  wifi pass PASSWORD     its password (empty for an open network); saves it first, syncs\r\n"
      "  wifi first N           try network N first\r\n"
      "  wifi forget N          delete network N\r\n"
      "  wifi forget all        delete all networks\r\n"
      "  ntp                    NTP status: server, interval, last sync\r\n"
      "  ntp server HOST        set the NTP server (default pool.ntp.org)\r\n"
      "  ntp every H            sync every H hours, 1 to 168 (default 24)\r\n"
      "  sync                   sync with NTP now\r\n"
      "  temp                   measure the room temperature now (sensor, offset, humidity)\r\n"
      "  offset [C]             show or set the temperature calibration, -10.0 to 10.0 C\r\n"
      "  bat                    battery voltage and level (low < 3.5 V, critical < 3.3 V)\r\n"
      "  log                    last 32 wakes: time, cause, redraw, awake time, battery\r\n"
      "  full                   full redraw\r\n"
      "  init                   restart the panel (reset + init) and redraw\r\n"
      "  reinit N               restart the panel every N minutes on minute wakes (0 = never)\r\n"
      "  panel                  panel write mode and restart interval\r\n"
      "  panel lpm|hpm          write frames in low power mode (default) or switch to high\r\n"
      "  recover                restart the panel: sleep in, reset, setup, redraw\r\n"
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

// x10 value as "-1.5"
void fmt_tenths(char* buf, size_t n, int32_t v) {
  snprintf(buf, n, "%s%ld.%ld", v < 0 ? "-" : "", static_cast<long>((v < 0 ? -v : v) / 10),
           static_cast<long>((v < 0 ? -v : v) % 10));
}

void print_drift() {
  int chip = 0;
  const bool read_ok = rtc::read_offset(chip);
  Serial.printf("offset %d steps (%s %.1f ppm)", g_rtc.rtc_offset,
                g_rtc.rtc_offset >= 0 ? "slows by" : "speeds up by",
                (g_rtc.rtc_offset >= 0 ? 1 : -1) * g_rtc.rtc_offset * pcf85063::kOffsetStepPpm);
  if (!read_ok) {
    Serial.print(", chip not readable");
  } else if (chip != g_rtc.rtc_offset) {
    Serial.printf(", chip has %d", chip);
  }
  Serial.print("\r\n");
  if (g_rtc.drift_ppm_x10 != kNoDrift) {
    char ppm[16];
    fmt_tenths(ppm, sizeof ppm, g_rtc.drift_ppm_x10);
    const int32_t ms_day =
        static_cast<int32_t>(static_cast<int64_t>(g_rtc.drift_ppm_x10) * 864 / 100);
    print_stamp("drift measured", static_cast<time_t>(g_rtc.drift_at));
    Serial.printf(": %s ppm (%ld ms/day, + = fast)\r\n", ppm, static_cast<long>(ms_day));
  } else {
    Serial.print(
        "drift: not measured yet (needs two NTP syncs at least 12 h apart on battery)\r\n");
  }
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
  print_drift();
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
  Serial.printf(
      "last %lu of %lu wakes (R rtc, T timer, K key, P power-on), battery now %u mV\r\n"
      "ntp column: + synced, W no Wi-Fi, D DNS, N no reply, B bad reply, X RTC write failed\r\n"
      "alarm column: A alarm rang, S snooze ended\r\n",
      static_cast<unsigned long>(n), static_cast<unsigned long>(g_rtc.log_count), battery_mv());
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
    Serial.printf("  %c  %-10s %5u ms  %4u mV  ntp %c  alarm %c%s%s\r\n", e.cause, redraw,
                  e.awake_ms, e.bat_mv, e.ntp_result ? e.ntp_result : '-', e.alarm ? e.alarm : '-',
                  e.rtc == static_cast<uint8_t>(rtc::Status::Ok) ? "" : "  rtc: ",
                  e.rtc == static_cast<uint8_t>(rtc::Status::Ok)
                      ? ""
                      : rtc::status_name(static_cast<rtc::Status>(e.rtc)));
  }
}

void print_panel() {
  Serial.printf(
      "panel: frames written in %s power mode; restart every %u min%s\r\n",
      g_rtc.panel_mode == static_cast<uint8_t>(St7305::WriteMode::LowPower) ? "low" : "high",
      g_rtc.reinit_min, g_rtc.reinit_min == 0 ? " (off)" : "");
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

void print_ntp() {
  char server[kHostLen];
  load_pref_string(kPrefNtpServer, server, sizeof server);
  static provision::WifiList nets;
  load_networks(nets);
  Serial.printf("ntp server %s, every %u h, Wi-Fi ", server[0] ? server : kDefaultNtpServer,
                g_rtc.ntp_hours);
  if (nets.count == 0) {
    Serial.print("(none: `setup`, or `wifi ssid` and `wifi pass`)\r\n");
  } else {
    Serial.printf("%s", nets.nets[0].ssid);
    if (nets.count > 1) Serial.printf(" first, %u more saved", nets.count - 1);
    Serial.print("\r\n");
  }
  memset(&nets, 0, sizeof nets);
  Serial.printf("syncs since power-on: %lu ok, %lu failed",
                static_cast<unsigned long>(g_rtc.ntp_ok),
                static_cast<unsigned long>(g_rtc.ntp_failed));
  if (g_rtc.ntp_schedule.fails > 0) Serial.printf(" (%u in a row)", g_rtc.ntp_schedule.fails);
  Serial.print("\r\n");
  if (g_rtc.ntp_status != 0) {
    const auto st = static_cast<ntp_sync::Status>(g_rtc.ntp_status);
    Serial.printf("last try: %s", ntp_sync::status_name(st));
    if (st == ntp_sync::Status::BadReply) {
      Serial.printf(" (%s)", ntp::reply_name(static_cast<ntp::Reply>(g_rtc.ntp_reply)));
    }
    Serial.printf(", Wi-Fi %u ms, total %u ms\r\n", g_rtc.ntp_wifi_ms, g_rtc.ntp_total_ms);
  }
  if (g_rtc.ntp_schedule.last_ok != 0) {
    print_stamp("last sync", static_cast<time_t>(g_rtc.ntp_schedule.last_ok));
    Serial.printf(": corrected by %ld ms, round trip %u ms\r\n",
                  static_cast<long>(g_rtc.ntp_offset_ms), g_rtc.ntp_rtt_ms);
  }
  if (g_rtc.wifi_configured) {
    const int64_t now = time(nullptr);
    if (ntp::due(g_rtc.ntp_schedule, now, g_rtc.time_set, ntp_interval_s())) {
      Serial.print("next sync: now\r\n");
    } else {
      const int64_t next =
          g_rtc.ntp_schedule.fails > 0
              ? g_rtc.ntp_schedule.last_try +
                    ntp::retry_delay_s(g_rtc.ntp_schedule.fails, g_rtc.time_set, ntp_interval_s())
              : g_rtc.ntp_schedule.last_ok + ntp_interval_s();
      print_stamp("next sync", static_cast<time_t>(next));
      Serial.print("\r\n");
    }
  }
}

// Forget the failures and the last sync, so the next check syncs at once.
void ntp_sync_soon() { g_rtc.ntp_schedule = {}; }

void print_networks(const provision::WifiList& nets) {
  if (nets.count == 0) {
    Serial.print("no Wi-Fi networks saved; NTP is off\r\n");
    return;
  }
  Serial.printf("Wi-Fi networks, tried in this order (%u of %u):\r\n", nets.count,
                static_cast<unsigned>(provision::kMaxNetworks));
  for (size_t i = 0; i < nets.count; ++i) {
    Serial.printf("  %u. %s%s\r\n", static_cast<unsigned>(i + 1), nets.nets[i].ssid,
                  nets.nets[i].pass[0] ? "" : " (open)");
  }
}

// Network number N (1-based) from the end of `line` after `prefix`, as an
// index; -1 if it is not a number in the list.
int network_arg(const String& line, size_t prefix, const provision::WifiList& nets) {
  const String arg = line.substring(prefix);
  const long n = arg.toInt();
  if (n < 1 || n > nets.count || String(n) != arg) return -1;
  return static_cast<int>(n - 1);
}

// Name typed by `wifi ssid`, waiting for `wifi pass`. RAM only.
char g_pending_ssid[kSsidLen];

void handle_wifi(const String& line) {
  static provision::WifiList nets;  // static: kept off the stack
  load_networks(nets);
  if (line.startsWith("wifi ssid ")) {
    String name = line.substring(10);
    name.trim();
    if (!provision::ssid_valid(name.c_str())) {
      Serial.print("network name: 1 to 32 characters\r\n");
      return;
    }
    snprintf(g_pending_ssid, sizeof g_pending_ssid, "%s", name.c_str());
    Serial.printf("network %s; now `wifi pass PASSWORD` (or just `wifi pass` if open)\r\n",
                  g_pending_ssid);
  } else if (line == "wifi pass" || line.startsWith("wifi pass ")) {
    if (g_pending_ssid[0] == '\0') {
      Serial.print("name the network first: `wifi ssid NAME`\r\n");
      return;
    }
    const String pw = line.length() > 10 ? line.substring(10) : String();
    if (!provision::pass_valid(pw.c_str())) {
      Serial.print("password: 8 to 63 characters, or nothing for an open network\r\n");
      return;
    }
    char dropped[kSsidLen];
    provision::remember(nets, g_pending_ssid, pw.c_str(), dropped);
    if (!save_networks(nets)) {
      Serial.print("could not save the network\r\n");
    } else {
      g_pending_ssid[0] = '\0';
      if (dropped[0]) Serial.printf("list full: forgot %s\r\n", dropped);
      print_networks(nets);
      Serial.print("syncing...\r\n");
      ntp_sync_soon();  // loop() runs it
    }
  } else if (line == "wifi forget" || line == "wifi forget all") {
    nets = {};
    Serial.print(save_networks(nets) ? "all Wi-Fi networks forgotten; NTP is off\r\n"
                                     : "could not save the change\r\n");
  } else if (line.startsWith("wifi forget ") || line.startsWith("wifi first ")) {
    const bool forget = line.startsWith("wifi forget ");
    const int i = network_arg(line, forget ? 12 : 11, nets);
    if (i < 0) {
      Serial.print("give a network number from `wifi`\r\n");
    } else {
      if (forget) {
        provision::remove(nets, static_cast<size_t>(i));
      } else {
        provision::promote(nets, static_cast<size_t>(i));
      }
      if (save_networks(nets)) {
        print_networks(nets);
      } else {
        Serial.print("could not save the change\r\n");
      }
    }
  } else {
    print_networks(nets);
    if (g_pending_ssid[0]) Serial.printf("adding %s: waiting for `wifi pass`\r\n", g_pending_ssid);
  }
  memset(&nets, 0, sizeof nets);
}

void handle_ntp(const String& line) {
  if (line.startsWith("ntp server ")) {
    String host = line.substring(11);
    host.trim();
    if (host.isEmpty() || host.length() >= kHostLen || host.indexOf(' ') >= 0) {
      Serial.print("ntp server takes a host name, e.g. `ntp server pool.ntp.org`\r\n");
      return;
    }
    if (!save_pref_string(kPrefNtpServer, host.c_str())) {
      Serial.print("could not save the server\r\n");
      return;
    }
    ntp_sync_soon();
  } else if (line.startsWith("ntp every ")) {
    const long h = line.substring(10).toInt();
    if (h < 1 || h > kMaxNtpHours) {
      Serial.print("ntp every takes 1 to 168 (hours)\r\n");
      return;
    }
    g_rtc.ntp_hours = static_cast<uint16_t>(h);
    Preferences prefs;
    bool ok = false;
    if (prefs.begin(kPrefsNamespace, false)) {
      ok = prefs.putUShort(kPrefNtpHours, g_rtc.ntp_hours) == sizeof(uint16_t);
      prefs.end();
    }
    if (!ok) Serial.print("could not save the interval: it is lost at power-off\r\n");
  }
  print_ntp();
}

void print_alarms() {
  const int64_t now = time(nullptr);
  if (g_alarms.empty()) Serial.print("no alarms; add one with `alarm add 07:00 weekdays Work`\r\n");
  for (const Alarm& a : g_alarms) {
    Serial.printf("  %u  %02u:%02u  %-14s %-3s  %s", a.id, a.hour, a.minute,
                  sched::format_days(a.days).c_str(), a.enabled ? "on" : "off", a.label.c_str());
    if (const auto f = g_rtc.time_set ? sched::next_fire(a, g_zone, now) : std::nullopt) {
      print_stamp("   next", static_cast<time_t>(*f));
    }
    Serial.print("\r\n");
  }
  if (g_rtc.ring.state == RingState::Snoozed) {
    print_stamp("snoozed until", static_cast<time_t>(g_rtc.ring.resume_at));
    Serial.printf(" (snooze %u of %u; `alarm stop` ends it)\r\n", g_rtc.ring.snoozes,
                  kDefaultRing.max_snoozes);
  }
  if (!g_rtc.time_set) Serial.print("time not set: alarms cannot ring until it is\r\n");
}

// Splits "a b c" into at most `max` words; the last one keeps the rest of the line.
size_t split_words(const String& s, String* out, size_t max) {
  size_t n = 0;
  int pos = 0;
  while (n < max && pos < static_cast<int>(s.length())) {
    while (pos < static_cast<int>(s.length()) && s[pos] == ' ') ++pos;
    if (pos >= static_cast<int>(s.length())) break;
    const int sp = s.indexOf(' ', pos);
    if (n == max - 1 || sp < 0) {
      out[n++] = s.substring(pos);
      break;
    }
    out[n++] = s.substring(pos, sp);
    pos = sp + 1;
  }
  return n;
}

void print_alarm_usage() {
  Serial.print(
      "  alarm                      list alarms\r\n"
      "  alarm add HH:MM [DAYS] [LABEL]   DAYS: daily, weekdays, weekends, once, mon,wed or "
      "mon-fri\r\n"
      "  alarm time ID HH:MM | alarm days ID DAYS | alarm label ID [TEXT]\r\n"
      "  alarm on ID | alarm off ID | alarm del ID\r\n"
      "  alarm test                 ring now (tap KEY = snooze, hold 2 s = stop)\r\n"
      "  alarm stop                 end a snooze\r\n");
}

void handle_alarm(const String& line) {
  String w[4];
  const size_t n = split_words(line, w, 4);  // "alarm", verb, arg, rest
  const String verb = n > 1 ? w[1] : String();
  std::vector<Alarm> v = g_alarms;
  if (verb.isEmpty()) {
    print_alarms();
    return;
  }
  if (verb == "test") {
    ring_trigger(g_rtc.ring, kTestAlarmId, time(nullptr));
    ring_loop();
    return;
  }
  if (verb == "stop") {
    ring_dismiss(g_rtc.ring);
    Serial.print("no alarm snoozed\r\n");
    update_display(false);
    return;
  }
  if (verb == "add") {
    if (v.size() >= sched::kMaxAlarms) {
      Serial.print("16 alarms already: delete one first\r\n");
      return;
    }
    String rest[3];
    const size_t m = split_words(n > 3 ? w[2] + " " + w[3] : n > 2 ? w[2] : String(), rest, 3);
    Alarm a;
    a.id = sched::free_id(v);
    a.days = kEveryDay;
    if (m < 1 || !sched::parse_time(rest[0].c_str(), a.hour, a.minute)) {
      print_alarm_usage();
      return;
    }
    size_t label_at = 1;
    if (m >= 2 && sched::parse_days(rest[1].c_str(), a.days)) label_at = 2;
    if (m > label_at) {
      a.label = rest[label_at].c_str();
      if (label_at == 1 && m == 3) a.label += (" " + rest[2]).c_str();
    }
    if (const char* why = sched::check(a)) {
      Serial.printf("%s\r\n", why);
      return;
    }
    v.push_back(a);
  } else {
    const long id = n > 2 ? w[2].toInt() : 0;
    const auto i = id > 0 && id < 256 ? sched::find(v, static_cast<uint8_t>(id)) : std::nullopt;
    if (!i) {
      if (verb == "on" || verb == "off" || verb == "del" || verb == "time" || verb == "days" ||
          verb == "label") {
        Serial.print("no alarm with that ID; `alarm` lists them\r\n");
      } else {
        print_alarm_usage();
      }
      return;
    }
    Alarm& a = v[*i];
    const String arg = n > 3 ? w[3] : String();
    if (verb == "on" || verb == "off") {
      a.enabled = verb == "on";
    } else if (verb == "del") {
      v.erase(v.begin() + static_cast<long>(*i));
    } else if (verb == "time") {
      if (!sched::parse_time(arg.c_str(), a.hour, a.minute)) {
        Serial.print("time as HH:MM, e.g. `alarm time 1 06:45`\r\n");
        return;
      }
      a.enabled = true;
    } else if (verb == "days") {
      if (!sched::parse_days(arg.c_str(), a.days)) {
        Serial.print("days: daily, weekdays, weekends, once, or e.g. mon,wed,fri or mon-fri\r\n");
        return;
      }
      a.enabled = true;
    } else if (verb == "label") {
      a.label = arg.c_str();
    } else {
      print_alarm_usage();
      return;
    }
    if (verb != "del") {
      if (const char* why = sched::check(a)) {
        Serial.printf("%s\r\n", why);
        return;
      }
    }
  }
  if (!save_alarms(v)) Serial.print("could not save the alarms: they are lost at power-off\r\n");
  print_alarms();
  if (current_face() != g_rtc.shown) update_display(false);
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
  } else if (line == "rtc offset" || line.startsWith("rtc offset ")) {
    String arg = line.substring(10);
    arg.trim();
    int n;
    char extra;
    if (arg.length() > 0) {
      if (sscanf(arg.c_str(), "%d %c", &n, &extra) != 1 || n < pcf85063::kOffsetMin ||
          n > pcf85063::kOffsetMax) {
        Serial.print("rtc offset takes -64 to 63 steps of 4.34 ppm, + slows the clock\r\n");
        return;
      }
      if (!set_rtc_offset(n)) Serial.print("could not write the offset\r\n");
    }
    print_drift();
  } else if (line == "alarm" || line == "alarms" || line.startsWith("alarm ")) {
    handle_alarm(line == "alarms" ? String("alarm") : line);
  } else if (line == "tz" || line.startsWith("tz ")) {
    String arg = line.substring(2);
    arg.trim();
    handle_tz(arg);
  } else if (line == "wifi" || line.startsWith("wifi ")) {
    handle_wifi(line);
  } else if (line == "ntp" || line.startsWith("ntp ")) {
    handle_ntp(line);
  } else if (line == "sync") {
    Serial.print("syncing (Wi-Fi on, up to ~15 s)...\r\n");
    run_ntp_sync();
    print_ntp();
    print_time();
    if (current_face() != g_rtc.shown) update_display(false);
  } else if (line == "setup") {
    run_setup(true);
    print_ntp();
  } else if (line == "bat") {
    measure_battery();
    Serial.printf(
        "battery %u mV, %s (low below %u mV, critical below %u mV, clears %u mV above)%s\r\n",
        g_rtc.bat_mv, battery::level_name(static_cast<battery::Level>(g_rtc.bat_level)),
        battery::kLowMv, battery::kCriticalMv, battery::kHysteresisMv,
        HWCDC::isPlugged() ? "; on USB this reads the charger, not the cell" : "");
    if (current_face() != g_rtc.shown) update_display(false);
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
  } else if (line == "panel") {
    print_panel();
  } else if (line == "panel lpm" || line == "panel hpm") {
    const auto mode =
        line == "panel lpm" ? St7305::WriteMode::LowPower : St7305::WriteMode::HighPower;
    g_rtc.panel_mode = static_cast<uint8_t>(mode);
    g_lcd.set_write_mode(mode);
    Serial.printf("frames written in %s power mode%s; back to the default after power-off\r\n",
                  mode == St7305::WriteMode::LowPower ? "low" : "high",
                  mode == St7305::WriteMode::LowPower ? "" : " (switch per write, +400 ms)");
  } else if (line == "recover") {
    restart_panel();
    Serial.print("panel restarted (sleep in, reset, setup) and redrawn\r\n");
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

  g_lcd.set_write_mode(static_cast<St7305::WriteMode>(g_rtc.panel_mode));
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
    alarms_from_rtc();
    g_rtc.wakes++;
    if (ext1 & (1ULL << rtc::kIntPin)) g_rtc.rtc_wakes++;
    if (cause == ESP_SLEEP_WAKEUP_TIMER) g_rtc.timer_wakes++;
    rtc_status = sync_from_rtc();
    if (rtc_status == rtc::Status::NoDevice) g_rtc.rtc_errors++;
    if (rtc_status == rtc::Status::Ok && (ext1 & (1ULL << rtc::kIntPin))) {
      g_clock_from_rtc_ms = millis() | 1;  // | 1: never 0
    }
    finish_temp(temp_started);
    measure_battery();
    const bool reinit =
        !key && g_rtc.reinit_min != 0 && current_face().minute % g_rtc.reinit_min == 0;
    if (!key) g_lcd.set_wait(nap);  // KEY wakes may stay awake on USB: plain delay()
    g_lcd.begin(false);
    uint8_t regions;
    if (reinit) {
      restart_panel();
      regions = kReinit;
    } else {
      regions = update_display(false);
    }
    g_lcd.set_wait([](uint32_t ms) { delay(ms); });
    // Logged before ringing, so the entry has this wake's time and awake_ms.
    const char alarm_letter = check_alarms();
    log_wake(key                               ? 'K'
             : cause == ESP_SLEEP_WAKEUP_TIMER ? 'T'
             : ext1 & (1ULL << rtc::kIntPin)   ? 'R'
                                               : '?',
             rtc_status, regions, 0, alarm_letter);
    if (g_rtc.ring.state == RingState::Ringing) ring_loop();
    // After the redraw (and any alarm), so neither waits for Wi-Fi.
    const char ntp_letter = maybe_ntp_sync();
    if (ntp_letter != 0 && current_face() != g_rtc.shown) update_display(false);
    g_rtc.log[(g_rtc.log_count - 1) % kLogLen].ntp_result = ntp_letter;
    // KEY held 2 s while snoozed: stop the alarm. Holding on to 5 s opens setup.
    if (key && g_rtc.ring.state == RingState::Snoozed && key_held_until(kDismissHoldMs)) {
      ring_dismiss(g_rtc.ring);
      update_display(false);
    }
    if (key && key_held_until(kKeyHoldMs)) {
      const char letter = run_setup(false);
      if (letter != 0) g_rtc.log[(g_rtc.log_count - 1) % kLogLen].ntp_result = letter;
    }
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
    g_rtc.panel_mode = static_cast<uint8_t>(kDefaultPanelMode);
    g_lcd.set_write_mode(kDefaultPanelMode);
    memcpy(g_rtc.log, saved_log, sizeof saved_log);
    g_rtc.temp_offset_dc = load_temp_offset();
    load_tz();
    load_ntp_config();
    load_alarms();
    // The offset register is lost with power; a reset keeps it, but write it anyway.
    g_rtc.rtc_offset = load_rtc_offset();
    g_rtc.drift_ppm_x10 = kNoDrift;
    rtc::set_offset(g_rtc.rtc_offset);
    finish_temp(temp_started);
    measure_battery();
    rtc_status = sync_from_rtc();
    // A reset keeps the system clock; a power-on starts it at 1970.
    if (rtc_status != rtc::Status::Ok && time(nullptr) < kMinValidTime) set_clock_from_build();
    g_lcd.begin(true);
    update_display(true);
    log_wake('P', rtc_status, kFullRefresh, 0);
    // First boot (or after `wifi forget`): open setup right away.
    if (!g_rtc.wifi_configured) {
      const char letter = run_setup(false);
      if (letter != 0) g_rtc.log[(g_rtc.log_count - 1) % kLogLen].ntp_result = letter;
    }
  }

  // Power-on or KEY: stay awake while a USB host is connected. isPlugged()
  // watches USB frames from the host; `Serial` would also need a terminal open.
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!HWCDC::isPlugged() && millis() - t0 < 1500) delay(10);
  if (!HWCDC::isPlugged()) {
    // Power-on on battery: the boot sync, recorded in the power-on log entry.
    // With USB, loop() runs it instead, so the terminal answers at once.
    if (!woke) {
      const char ntp_letter = maybe_ntp_sync();
      if (ntp_letter != 0 && current_face() != g_rtc.shown) update_display(false);
      g_rtc.log[(g_rtc.log_count - 1) % kLogLen].ntp_result = ntp_letter;
    }
    deep_sleep();
  }
  delay(500);  // let a terminal that was already open reattach

  Serial.printf("\r\nwake cause %d (0 = power-on or reset), %lu deep-sleep wakes\r\n",
                static_cast<int>(cause), static_cast<unsigned long>(g_rtc.wakes));
  if (rtc_status != rtc::Status::Ok) {
    Serial.printf("RTC at boot: %s. Set the time with `set`, or Wi-Fi (`setup`) for NTP.\r\n",
                  rtc::status_name(rtc_status));
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
    } else if (c >= 0x20 && c < 0x7F && line.length() < 100) {
      line += static_cast<char>(c);
      Serial.write(static_cast<uint8_t>(c));
    }
  }
  if (handle_alarms() != 0) print_alarms();
  // KEY held 2 s while snoozed stops the alarm; held 5 s opens Wi-Fi setup.
  static uint32_t key_down_ms = 0;
  if (digitalRead(kKeyPin) == LOW) {
    if (key_down_ms == 0) key_down_ms = millis() | 1;
    if (g_rtc.ring.state == RingState::Snoozed && millis() - key_down_ms >= kDismissHoldMs) {
      ring_dismiss(g_rtc.ring);
      Serial.print("alarm stopped\r\n");
      update_display(false);
    }
    if (millis() - key_down_ms >= kKeyHoldMs) {
      run_setup(true);
      key_down_ms = 0;
      print_ntp();
    }
  } else {
    key_down_ms = 0;
  }
  static uint32_t last_temp_ms = millis();
  if (millis() - last_temp_ms >= kTempPeriodMs) {
    last_temp_ms = millis();
    measure_temp();
    measure_battery();
  }
  if (maybe_ntp_sync() != 0) {
    print_ntp();
    print_time();
  }
  if (current_face() != g_rtc.shown) update_display(false);
  if (!HWCDC::isPlugged()) {
    delay(200);  // USB unplugged: settle, then run on battery
    if (!HWCDC::isPlugged()) deep_sleep();
  }
  delay(50);
}
