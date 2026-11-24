// SPDX-License-Identifier: GPL-3.0-only
// Alarm beep on the on-board ES8311 codec, amp and speaker (ADR-0006, #2).
// The register values follow Espressif's esp_codec_dev ES8311 driver
// (Apache-2.0) as used in Waveshare's 07_Audio_Test for this board: slave
// mode, MCLK = 256 x fs from the ESP32, 16-bit I2S, DAC only.
#pragma once

#include <cstdint>

namespace audio {

// Codec and I2S on, amp on. False if the codec does not answer on I2C.
bool begin();
// Amp off, codec suspended, I2S released. Safe to call when not started.
void end();
bool running();

// Plays `ms` of the beep pattern at `pct` percent (0-100), continuing at
// pattern position `pos_ms`, which it advances. Blocks for about `ms`.
void beep(uint32_t ms, uint8_t pct, uint32_t& pos_ms);

}  // namespace audio
