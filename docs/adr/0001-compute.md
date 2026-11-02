# 0001. Compute platform: ESP32-S3

- Status: Accepted
- Date: 2026-11-02

## Decision

ESP32-S3 with at least 8 MB PSRAM.

## Alternatives rejected

Raspberry Pi Zero 2 W.

## Rationale

Instant boot, no SD-card wear, low idle draw. PSRAM holds the framebuffer, TLS buffers and MP3 decode at once.
