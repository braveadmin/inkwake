# 0012. Room temperature: on-board SHTC3

- Status: Proposed
- Date: 2026-11-02

## Decision

On-board SHTC3 (I2C 13/14) with an offset calibration in config. Fallback: external SHT40 on the header I2C in a vented pocket.

## Alternatives rejected

Separate SHT40 from the start.

## Rationale

Free and already wired. Board heat biases the reading; calibration against a reference thermometer covers a room reading.
