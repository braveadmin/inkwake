# 0015. Input: capacitive pads in the shell

- Status: Proposed
- Date: 2026-11-02

## Decision

Copper-tape pads inside the shell, wired to ESP32-S3 touch pins. Top pad: tap = snooze, hold 2 s = dismiss. Side pad: tap = night glow, hold 5 s = web UI.

## Alternatives rejected

Touch e-paper panel; mechanical buttons.

## Rationale

Easier to hit half asleep than a spot on a screen, almost free, and a pad can wake the chip from deep sleep. Affordable touch e-paper panels are 296x128, too small for widgets.
