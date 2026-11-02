# 0013. Power: battery-first

- Status: Proposed
- Date: 2026-11-02

## Decision

2000-3000 mAh protected LiPo, charged over USB-C by the board's charger. Deep sleep between minute updates. MOSFET-switched rail for LED ring and amp. Wi-Fi only for daily NTP, weather every 30 min, Home Assistant check-ins every 15-30 min and the on-demand web UI. Targets: whole-clock sleep under 100 uA, 30-55 mAh/day, 5-8 weeks per charge.

## Alternatives rejected

Mains-powered; PaperS3 internal battery.

## Rationale

The clock should be cable-free except for charging. The big loads (LEDs, amp, Wi-Fi) are external on any board, so they need switching regardless of the board. Consequences: Home Assistant becomes check-in based (no live snooze), the web UI is woken on demand.
