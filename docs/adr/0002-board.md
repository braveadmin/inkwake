# 0002. Board: Waveshare ESP32-S3-RLCD-4.2

- Status: Proposed
- Date: 2026-11-02

## Decision

Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3-WROOM-1-N16R8; 4.2" 400x300 reflective LCD; PCF85063 RTC; SHTC3; ES8311 + ES7210 audio; 8 ohm 2 W speaker; TF card; 18650 holder with charging). Fallback: FeatherS3 + Waveshare 4.2" V2 e-paper, only if the M0 sleep-current gate fails.

## Alternatives rejected

M5Stack PaperS3; FeatherS3 + e-ink as lead; generic DevKitC.

## Rationale

One board carries nearly every peripheral the clock needs for about USD 27 including battery and speaker. The 2x8 header leaves GPIO1, 2, 3, 17, 18 free; GPIO1-3 are touch-capable. Open: KEY/PWR GPIOs and a battery-sense pin, to confirm in the schematic.
