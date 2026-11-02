# Web UI (M5)

Static, dependency-light UI served from LittleFS. Build output is gzipped and packed by `pio run -t buildfs`.
Talks to the device over a small JSON REST API; schema shared with `firmware/lib/config`.
