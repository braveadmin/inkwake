# 0007. Sunrise light: PWM warm-white LEDs

- Status: Proposed
- Date: 2026-11-02

## Decision

Warm-white LEDs powered from the 18650, PWM through a logic-level N-MOSFET on GPIO17. Upgrade path: SK6812 RGBW ring with a 5 V boost converter enabled from GPIO18.

## Alternatives rejected

SK6812 RGBW ring as default.

## Rationale

The board has no 5 V rail on battery. Plain LEDs need no boost and draw nothing when off. The RGBW ring adds a red-to-white sunrise at the cost of a boost converter.
