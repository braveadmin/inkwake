// SPDX-License-Identifier: GPL-3.0-only
// Alarm beep and sound files (#44) on the on-board ES8311 codec, amp and
// speaker (ADR-0006, #2).
// The register values follow Espressif's esp_codec_dev ES8311 driver
// (Apache-2.0) as used in Waveshare's 07_Audio_Test for this board: slave
// mode, MCLK = 256 x fs from the ESP32, 16-bit I2S, DAC only. With MCLK at
// 256 x fs the codec's clock registers are the same for every sample rate
// (esp_codec_dev's coefficient table), so only the I2S rate changes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

constexpr uint32_t kBeepRate = 16000;

// Codec and I2S on at `rate` samples/s, amp on. False if the codec does not
// answer on I2C. Call end() first to change the rate.
bool begin(uint32_t rate = kBeepRate);
// Amp off, codec suspended, I2S released. Safe to call when not started.
void end();
bool running();

// Plays `ms` of the beep pattern at `pct` percent (0-100), continuing at
// pattern position `pos_ms`, which it advances. Blocks for about `ms`.
void beep(uint32_t ms, uint8_t pct, uint32_t& pos_ms);

// Codec DAC attenuation in 0.5 dB steps below full scale (lead-in, #4),
// on top of the sample scaling below; 0 = none, as after begin(). Clamped to
// the codec's range (95.5 dB). Ignored while not started.
void set_attenuation(uint8_t half_db);

// Plays `n` mono samples at `pct` percent (same scale as beep). The samples
// are scaled in place. Blocks until they are queued for the speaker.
void play(int16_t* samples, size_t n, uint8_t pct);

}  // namespace audio
