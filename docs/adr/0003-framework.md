# 0003. Firmware framework: PlatformIO + Arduino-ESP32 3.x

- Status: Proposed
- Date: 2026-11-02

## Decision

PlatformIO + Arduino-ESP32 3.x on ESP-IDF 5, work split into FreeRTOS tasks. Hardware-free logic lives in lib/ and builds natively.

## Alternatives rejected

ESPHome; bare ESP-IDF.

## Rationale

ESPHome fights a custom alarm state machine and widget engine. Arduino keeps the contributor pool and libraries; IDF APIs stay reachable.
