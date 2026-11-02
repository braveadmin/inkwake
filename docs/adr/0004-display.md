# 0004. Display rendering: own layer on vendor driver

- Status: Proposed
- Date: 2026-11-02

## Decision

GxEPD2 over SPI with a thin render layer. Partial refresh each minute, full refresh hourly and on wake.

## Alternatives rejected

LVGL.

## Rationale

LVGL assumes fast redraws; e-ink wants batched dirty rectangles. Revisit if the UI outgrows it.
