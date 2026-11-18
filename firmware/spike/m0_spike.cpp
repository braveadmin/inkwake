// SPDX-License-Identifier: GPL-3.0-only
// M0 hardware spike: throwaway firmware to answer the M0 questions on a real
// Waveshare ESP32-S3-RLCD-4.2. Procedure and results: docs/m0-spike.md.
//
//   pio run -e spike -t upload && pio device monitor
//
// Serial menu (115200, line based):
//   1  deep sleep, panel in low-power mode (LPM), redraw every 60 s   <- exit gate case
//   2  deep sleep, panel in high-power mode (HPM), redraw every 60 s
//   3  light sleep, panel LPM, redraw every 60 s
//   4  deep sleep, panel off (sleep-in): baseline without the screen
//   w  watch inputs: touch GPIO1/2/3 raw values, KEY (18), BOOT (0), battery
//   t <n>  touch wake on GPIO1 with threshold n (0 = off)
//   b  battery voltage      d  redraw test screen      h  help
// Press KEY (GPIO18) to leave a sleep mode and get back to the menu.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "rlcd.h"

using spike::Rlcd;

namespace {

constexpr int kKeyPin = 18;  // KEY button, active low (Waveshare ESPHome example)
constexpr int kBootPin = 0;  // BOOT button, active low
constexpr int kBatPin = 4;   // ADC1_CH3, 1:3 divider (Waveshare ADC example)
constexpr int kAmpPin = 46;  // PA_CTRL, high = speaker amp on
constexpr int kTouchPins[] = {1, 2, 3};
constexpr uint64_t kCycleUs = 60ULL * 1000 * 1000;

enum Mode : uint8_t { kMenu = 0, kDeepLpm = 1, kDeepHpm = 2, kLightLpm = 3, kDeepOff = 4 };

// Survives deep sleep (RTC slow memory).
struct RtcState {
  uint32_t magic;
  Mode mode;
  uint32_t minute;         // simulated clock, minutes since 00:00
  uint32_t cycles;         // deep-sleep wakes in the current run
  uint32_t last_awake_ms;  // time awake in the previous cycle
  uint32_t touch_threshold;
};
RTC_DATA_ATTR RtcState g_rtc;
constexpr uint32_t kMagic = 0x1C0FFEE5;

Rlcd g_lcd;

// ---- drawing ----------------------------------------------------------------

// 3x5 glyphs, one row per entry, bit 2 = left column.
struct Glyph {
  char c;
  uint8_t rows[5];
};
constexpr Glyph kFont[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 7, 1, 7}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}}, {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 1, 1}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}}, {'.', {0, 0, 0, 0, 2}}, {':', {0, 2, 0, 2, 0}},
    {'-', {0, 0, 7, 0, 0}}, {'%', {5, 1, 2, 4, 5}}, {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {7, 4, 4, 4, 7}}, {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {7, 4, 5, 5, 7}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}}, {'K', {5, 5, 6, 5, 5}},
    {'L', {4, 4, 4, 4, 7}}, {'M', {5, 7, 7, 5, 5}}, {'N', {6, 5, 5, 5, 5}}, {'O', {7, 5, 5, 5, 7}},
    {'P', {7, 5, 7, 4, 4}}, {'R', {6, 5, 6, 5, 5}}, {'S', {7, 4, 7, 1, 7}}, {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}}, {'W', {5, 5, 7, 7, 5}}, {'Y', {5, 5, 2, 2, 2}},
};

void draw_text(int x, int y, const char* s, int scale) {
  for (; *s; ++s, x += 4 * scale) {
    const char c = static_cast<char>(toupper(*s));
    for (const auto& g : kFont) {
      if (g.c != c) continue;
      for (int r = 0; r < 5; ++r) {
        for (int col = 0; col < 3; ++col) {
          if (g.rows[r] & (4 >> col)) {
            g_lcd.fill_rect(x + col * scale, y + r * scale, scale, scale, true);
          }
        }
      }
      break;
    }
  }
}

// Seven-segment digit, 60 x 120 px.
void draw_digit(int x, int y, int d) {
  static const uint8_t kSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  constexpr int w = 60, h = 120, t = 14;
  const uint8_t s = kSeg[d % 10];
  if (s & 0x01) g_lcd.fill_rect(x, y, w, t, true);                      // a
  if (s & 0x02) g_lcd.fill_rect(x + w - t, y, t, h / 2, true);          // b
  if (s & 0x04) g_lcd.fill_rect(x + w - t, y + h / 2, t, h / 2, true);  // c
  if (s & 0x08) g_lcd.fill_rect(x, y + h - t, w, t, true);              // d
  if (s & 0x10) g_lcd.fill_rect(x, y + h / 2, t, h / 2, true);          // e
  if (s & 0x20) g_lcd.fill_rect(x, y, t, h / 2, true);                  // f
  if (s & 0x40) g_lcd.fill_rect(x, y + (h - t) / 2, w, t, true);        // g
}

float battery_volts() { return analogReadMilliVolts(kBatPin) * 3.0f / 1000.0f; }

const char* mode_name(Mode m) {
  switch (m) {
    case kDeepLpm:
      return "DEEP LPM";
    case kDeepHpm:
      return "DEEP HPM";
    case kLightLpm:
      return "LIGHT LPM";
    case kDeepOff:
      return "DEEP OFF";
    default:
      return "MENU";
  }
}

void draw_clock_face() {
  g_lcd.clear(true);
  const uint32_t hh = (g_rtc.minute / 60) % 24, mm = g_rtc.minute % 60;
  draw_digit(45, 60, hh / 10);
  draw_digit(125, 60, hh % 10);
  g_lcd.fill_rect(196, 95, 14, 14, true);
  g_lcd.fill_rect(196, 145, 14, 14, true);
  draw_digit(225, 60, mm / 10);
  draw_digit(305, 60, mm % 10);

  char line[64];
  snprintf(line, sizeof line, "%s  %.2fV", mode_name(g_rtc.mode), battery_volts());
  draw_text(10, 10, line, 4);
  snprintf(line, sizeof line, "CYCLE %lu  AWAKE %lu MS", static_cast<unsigned long>(g_rtc.cycles),
           static_cast<unsigned long>(g_rtc.last_awake_ms));
  draw_text(10, 260, line, 4);
}

// Write RAM in HPM, then park the panel in the requested power mode.
void show(bool lpm_after) {
  if (g_lcd.is_low_power()) g_lcd.low_power(false);
  g_lcd.flush();
  if (lpm_after) g_lcd.low_power(true);
}

// ---- sleep ------------------------------------------------------------------

void arm_wake_sources() {
  esp_sleep_enable_timer_wakeup(kCycleUs);
  // KEY has an external pull-up on the board (Waveshare uses it as plain INPUT),
  // so no RTC pull-up: that would keep the RTC peripherals powered.
  esp_sleep_enable_ext1_wakeup_io(1ULL << kKeyPin, ESP_EXT1_WAKEUP_ANY_LOW);
  if (g_rtc.touch_threshold > 0) {
    touchSleepWakeUpEnable(kTouchPins[0], g_rtc.touch_threshold);
  }
}

void amp_off_and_hold() {
  pinMode(kAmpPin, OUTPUT);
  digitalWrite(kAmpPin, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(kAmpPin));
}

[[noreturn]] void enter_deep_sleep() {
  Serial.flush();
  amp_off_and_hold();
  Rlcd::hold_pins(true);
  arm_wake_sources();
  esp_deep_sleep_start();
}

// One timer wake in a deep-sleep mode: redraw the minute and go back to sleep.
[[noreturn]] void deep_cycle(uint32_t t0) {
  g_rtc.cycles++;
  g_rtc.minute++;
  if (g_rtc.mode != kDeepOff) {
    g_lcd.begin(false);
    draw_clock_face();
    show(g_rtc.mode == kDeepLpm);
  }
  g_rtc.last_awake_ms = millis() - t0;
  enter_deep_sleep();
}

void run_light_sleep() {
  Serial.println("light sleep: press KEY to stop");
  Serial.flush();
  amp_off_and_hold();
  for (;;) {
    const uint32_t t0 = millis();
    g_rtc.cycles++;
    g_rtc.minute++;
    draw_clock_face();
    show(true);
    g_rtc.last_awake_ms = millis() - t0;
    arm_wake_sources();
    esp_light_sleep_start();
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) break;
  }
  gpio_hold_dis(static_cast<gpio_num_t>(kAmpPin));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  g_rtc.mode = kMenu;
  Serial.println("light sleep stopped");
}

// ---- menu -------------------------------------------------------------------

void print_help() {
  Serial.println(
      "\ninkwake M0 spike\n"
      "  1  deep sleep, panel LPM   (exit gate case)\n"
      "  2  deep sleep, panel HPM\n"
      "  3  light sleep, panel LPM\n"
      "  4  deep sleep, panel off\n"
      "  w  watch inputs (any key stops)\n"
      "  t <n>  touch wake on GPIO1, threshold n (0 = off)\n"
      "  b  battery    d  redraw    h  help\n"
      "KEY wakes back to this menu.");
  Serial.printf("touch wake threshold: %lu\n", static_cast<unsigned long>(g_rtc.touch_threshold));
}

void watch_inputs() {
  uint32_t base[3];
  for (int i = 0; i < 3; ++i) base[i] = touchRead(kTouchPins[i]);
  Serial.println("pad1 pad2 pad3 (raw / delta vs start), KEY, BOOT, battery. Any key stops.");
  while (!Serial.available()) {
    for (int i = 0; i < 3; ++i) {
      const uint32_t v = touchRead(kTouchPins[i]);
      Serial.printf("T%d %7lu %+7ld  ", kTouchPins[i], static_cast<unsigned long>(v),
                    static_cast<long>(v) - static_cast<long>(base[i]));
    }
    Serial.printf("KEY %d BOOT %d  %.2fV\n", !digitalRead(kKeyPin), !digitalRead(kBootPin),
                  battery_volts());
    delay(100);
  }
  while (Serial.available()) Serial.read();
}

void start_mode(Mode m) {
  g_rtc.mode = m;
  g_rtc.cycles = 0;
  g_rtc.last_awake_ms = 0;
  Serial.printf("%s: measure now. Press KEY to return.\n", mode_name(m));
  if (m == kLightLpm) {
    run_light_sleep();
    return;
  }
  if (m == kDeepOff) {
    g_lcd.sleep_in();
  } else {
    draw_clock_face();
    show(m == kDeepLpm);
  }
  delay(200);  // let the serial line drain before USB drops
  enter_deep_sleep();
}

void handle_line(const String& line) {
  if (line.isEmpty()) return;
  switch (line[0]) {
    case '1':
      start_mode(kDeepLpm);
      break;
    case '2':
      start_mode(kDeepHpm);
      break;
    case '3':
      start_mode(kLightLpm);
      break;
    case '4':
      start_mode(kDeepOff);
      break;
    case 'w':
      watch_inputs();
      break;
    case 't':
      g_rtc.touch_threshold = line.substring(1).toInt();
      Serial.printf("touch wake threshold %lu\n",
                    static_cast<unsigned long>(g_rtc.touch_threshold));
      break;
    case 'b':
      Serial.printf("battery %.3f V\n", battery_volts());
      break;
    case 'd':
      draw_clock_face();
      show(false);
      break;
    default:
      print_help();
      break;
  }
}

}  // namespace

void setup() {
  const uint32_t t0 = millis();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  gpio_hold_dis(static_cast<gpio_num_t>(kAmpPin));
  pinMode(kKeyPin, INPUT);
  pinMode(kBootPin, INPUT);
  analogSetPinAttenuation(kBatPin, ADC_11db);

  if (g_rtc.magic != kMagic) {
    g_rtc = RtcState{kMagic, kMenu, 7 * 60, 0, 0, 0};
  }

  if (cause == ESP_SLEEP_WAKEUP_TIMER &&
      (g_rtc.mode == kDeepLpm || g_rtc.mode == kDeepHpm || g_rtc.mode == kDeepOff)) {
    deep_cycle(t0);
  }

  // Power-on, reset, KEY or touch wake: back to the menu with a fresh panel init.
  Serial.begin(115200);
  delay(1500);  // give USB CDC time to enumerate
  const Mode was = g_rtc.mode;
  g_rtc.mode = kMenu;
  Serial.printf("wake cause %d, previous mode %s, %lu cycles, last awake %lu ms\n", cause,
                mode_name(was), static_cast<unsigned long>(g_rtc.cycles),
                static_cast<unsigned long>(g_rtc.last_awake_ms));
  g_lcd.begin(true);
  draw_clock_face();
  show(false);
  print_help();
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    handle_line(line);
  }
  delay(20);
}
