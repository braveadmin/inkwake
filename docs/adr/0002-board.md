# 0002. Board: all-in-one ESP32-S3 touch e-ink

- Status: Proposed (closes at M0)
- Date: 2026-11-02

## Decision

All-in-one ESP32-S3 touch e-ink board. M5Stack PaperS3 first candidate, LilyGO T5 4.7" S3 touch fallback. Final pick at the M0 gate.

## Alternatives rejected

ESP32-S3 DevKit + separate e-paper + touch panel.

## Rationale

Large touch e-ink panels are hard to source standalone; all-in-one boards bring touch, RTC and microSD on one PCB. Risk: spare GPIO for audio and LEDs.
