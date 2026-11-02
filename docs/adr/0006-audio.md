# 0006. Audio: I2S amplifier

- Status: Proposed
- Date: 2026-11-02

## Decision

MAX98357A I2S amp + small speaker, WAV/MP3 from LittleFS (about 9 MB free on 16 MB flash; SD module optional). Amp held in shutdown between alarms. Piezo buzzer on a GPIO as fallback.

## Alternatives rejected

DAC + analog amp.

## Rationale

Three pins, digital to the amp, no analog noise.
