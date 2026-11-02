# 0004. Display: reflective LCD (ST7305)

- Status: Proposed
- Date: 2026-11-02

## Decision

4.2" 400x300 monochrome reflective LCD, ST7305 controller over SPI (DS 5, TE 6, SCK 11, DIN 12, CS 40, RESET 41). LVGL reconsidered for the UI.

## Alternatives rejected

Custom-only render layer designed around e-ink.

## Rationale

The panel refreshes fast with no ghosting or full-refresh flashes, so a standard UI toolkit fits. Unlike e-ink it stays powered at a small constant draw, covered by the power budget (ADR-0013).
