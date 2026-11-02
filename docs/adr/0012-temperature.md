# 0012. Room temperature sensor: SHT40

- Status: Proposed
- Date: 2026-11-02

## Decision

Sensirion SHT40 on the board's I2C header, mounted in a walled-off vented pocket at the bottom of the shell.

## Alternatives rejected

ESP32 internal temperature sensor; DHT22.

## Rationale

The internal sensor reads chip temperature, not the room. SHT40 is I2C, about +/-0.2 C, a few euro; DHT22 is slower and less reliable. Board and LED heat still bias readings, so the shell isolates the sensor and config carries an offset.
