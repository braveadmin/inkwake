# 0005. Configuration: versioned JSON

- Status: Proposed
- Date: 2026-11-02

## Decision

Versioned JSON on LittleFS via ArduinoJson; alarms mirrored in NVS.

## Alternatives rejected

YAML on device.

## Rationale

No solid YAML parser on MCU. NVS copy keeps alarms alive if the filesystem corrupts. Web UI can export YAML.
