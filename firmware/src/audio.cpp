// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <driver/gpio.h>

#include <cmath>

#include "alarm.h"
#include "sound.h"

namespace audio {

namespace {

constexpr uint8_t kAddr = 0x18;  // ES8311, CE low
constexpr int kAmpPin = 46;      // PA_CTRL, high = amp on
constexpr int kMclk = 16, kBclk = 9, kWs = 45, kDout = 8;
constexpr uint32_t kToneHz = 2000;

I2SClass g_i2s;
bool g_running = false;
uint32_t g_rate = kBeepRate;

bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// esp_codec_dev es8311_open + config_sample (4.096 MHz / 16 kHz row) + start
// for DAC only, 16-bit I2S.
bool codec_on() {
  bool ok = wr(0x44, 0x08);
  ok = wr(0x44, 0x08) && ok;  // first write after power-up sometimes fails
  ok = wr(0x01, 0x30) && ok;
  ok = wr(0x02, 0x00) && ok;
  ok = wr(0x03, 0x10) && ok;
  ok = wr(0x16, 0x24) && ok;
  ok = wr(0x04, 0x10) && ok;
  ok = wr(0x05, 0x00) && ok;
  ok = wr(0x0B, 0x00) && ok;
  ok = wr(0x0C, 0x00) && ok;
  ok = wr(0x10, 0x1F) && ok;
  ok = wr(0x11, 0x7F) && ok;
  ok = wr(0x00, 0x80) && ok;  // power on, slave
  ok = wr(0x01, 0x3F) && ok;  // clocks on, MCLK from pin
  ok = wr(0x06, 0x03) && ok;  // BCLK = MCLK / 4, not inverted
  ok = wr(0x07, 0x00) && ok;  // LRCK = MCLK / 256
  ok = wr(0x08, 0xFF) && ok;
  ok = wr(0x13, 0x10) && ok;
  ok = wr(0x1B, 0x0A) && ok;
  ok = wr(0x1C, 0x6A) && ok;
  ok = wr(0x44, 0x08) && ok;  // no DAC reference into the ADC
  ok = wr(0x09, 0x0C) && ok;  // DAC in: I2S, 16 bit, not muted
  ok = wr(0x0A, 0x4C) && ok;  // ADC out: 16 bit, muted
  ok = wr(0x17, 0xBF) && ok;
  ok = wr(0x0E, 0x02) && ok;
  ok = wr(0x12, 0x00) && ok;  // DAC on
  ok = wr(0x14, 0x1A) && ok;
  ok = wr(0x0D, 0x01) && ok;  // analog on
  ok = wr(0x15, 0x40) && ok;
  ok = wr(0x37, 0x08) && ok;  // EQ bypass
  ok = wr(0x45, 0x00) && ok;
  ok = wr(0x31, 0x00) && ok;  // unmute
  ok = wr(0x32, 0xBF) && ok;  // DAC volume 0 dB; loudness is set in the samples
  return ok;
}

// es8311_suspend: lowest codec current.
void codec_off() {
  const uint8_t seq[][2] = {{0x32, 0x00}, {0x17, 0x00}, {0x0E, 0xFF}, {0x12, 0x02}, {0x14, 0x00},
                            {0x0D, 0xFA}, {0x15, 0x00}, {0x02, 0x10}, {0x00, 0x00}, {0x00, 0x1F},
                            {0x01, 0x30}, {0x01, 0x00}, {0x45, 0x00}, {0x0D, 0xFC}, {0x02, 0x00}};
  for (const auto& r : seq) wr(r[0], r[1]);
}

void amp(bool on) {
  gpio_hold_dis(static_cast<gpio_num_t>(kAmpPin));
  pinMode(kAmpPin, OUTPUT);
  digitalWrite(kAmpPin, on ? HIGH : LOW);
}

}  // namespace

bool begin(uint32_t rate) {
  if (g_running) return true;
  // MCLK must run before the codec registers are written.
  g_i2s.setPins(kBclk, kWs, kDout, -1, kMclk);
  if (!g_i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    return false;
  }
  delay(5);
  if (!codec_on()) {
    g_i2s.end();
    return false;
  }
  amp(true);
  g_rate = rate;
  g_running = true;
  return true;
}

void end() {
  if (!g_running) return;
  amp(false);
  codec_off();
  g_i2s.end();
  g_running = false;
}

bool running() { return g_running; }

void set_attenuation(uint8_t half_db) {
  if (!g_running) return;
  constexpr uint8_t kZeroDb = 0xBF;  // reg 0x32: 0x00 = -95.5 dB, +0.5 dB per step
  wr(0x32, static_cast<uint8_t>(half_db >= kZeroDb ? 0 : kZeroDb - half_db));
}

void beep(uint32_t ms, uint8_t pct, uint32_t& pos_ms) {
  if (!g_running) {
    delay(ms);
    pos_ms += ms;
    return;
  }
  const uint32_t chunk = g_rate / 100;  // samples = 10 ms
  static int16_t buf[inkwake::sound::kMaxRate / 100 * 2];
  // Perceived loudness ~ log: map 0-100 % onto -40..0 dBFS.
  const float amp_f = static_cast<float>(inkwake::sound::gain_q15(pct)) * (32767.0f / 32768.0f);
  const float step = 2.0f * static_cast<float>(M_PI) * kToneHz / static_cast<float>(g_rate);
  static float phase = 0;  // radians into the tone period
  for (uint32_t done = 0; done < ms; done += 10, pos_ms += 10) {
    const bool on = inkwake::beep_on(pos_ms);
    for (uint32_t i = 0; i < chunk; ++i) {
      int16_t s = 0;
      if (on) {
        s = static_cast<int16_t>(amp_f * std::sin(phase));
        phase += step;
        if (phase >= 2.0f * static_cast<float>(M_PI)) phase -= 2.0f * static_cast<float>(M_PI);
      }
      buf[2 * i] = buf[2 * i + 1] = s;
    }
    g_i2s.write(reinterpret_cast<uint8_t*>(buf), chunk * 2 * sizeof buf[0]);
  }
}

void play(int16_t* samples, size_t n, uint8_t pct) {
  if (!g_running) return;
  inkwake::sound::apply_gain(samples, n, inkwake::sound::gain_q15(pct));
  constexpr size_t kFrames = 256;
  int16_t buf[kFrames * 2];  // the codec takes stereo frames: both slots get the sample
  for (size_t done = 0; done < n;) {
    const size_t m = n - done < kFrames ? n - done : kFrames;
    for (size_t i = 0; i < m; ++i) buf[2 * i] = buf[2 * i + 1] = samples[done + i];
    g_i2s.write(reinterpret_cast<uint8_t*>(buf), m * 2 * sizeof buf[0]);
    done += m;
  }
}

}  // namespace audio
