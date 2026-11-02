# 0013. Power: battery-first on 18650

- Status: Proposed
- Date: 2026-11-02

## Decision

Included 18650 (about 2500-3000 mAh), charged over USB-C by the board. ESP32 sleeps between screen updates; amp off via PA_CTRL; LEDs off via MOSFET; Wi-Fi only for daily NTP, weather every 30 min, Home Assistant check-ins and the on-demand web UI. Target: whole-board sleep under 1.5 mA with the screen showing, 5-8 weeks per charge.

## Alternatives rejected

Mains-powered; pouch LiPo.

## Rationale

Cable-free except for charging. The 18650 leaves room for the screen's constant draw and is safer in a bedroom than a pouch cell. Consequences: Home Assistant becomes check-in based (no live snooze); the web UI is woken on demand.
