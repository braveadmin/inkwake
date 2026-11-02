# 0008. Integrations: MQTT, Open-Meteo

- Status: Proposed
- Date: 2026-11-02

## Decision

Home Assistant via MQTT discovery; weather from Open-Meteo; calendar via HA entity first, ICS later.

## Alternatives rejected

ESPHome native API; OpenWeatherMap.

## Rationale

Open-Meteo needs no API key, so the default config ships with no secrets. MQTT works with or without HA.
