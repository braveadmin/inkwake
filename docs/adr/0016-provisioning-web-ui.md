# 0016. Wi-Fi provisioning and the web UI: one web app, two ways in

- Status: Accepted
- Date: 2026-11-20
- Amended by ADR-0018 (2026-11-25): Wi-Fi is optional (Advanced mode); first boot opens the date & time editor instead of setup (#34). KEY held 5 s still opens setup, also from the button menu.

## Context

The clock needs the home Wi-Fi credentials for NTP (F12) and later weather and Home Assistant. The same browser page should grow into the settings site: timezone now, then custom alarm tones, widgets, weather, temperature offset (F3, F5, F8). Wi-Fi stays off except for short syncs (ADR-0013).

## Decision

- **Setup mode (#20):** the clock opens its own access point `inkwake-XXXX` (last MAC bytes) with a captive portal. The network is **open**: it exists only for a few minutes. The screen shows a QR code `WIFI:T:nopass;S:inkwake-XXXX;;` so scanning it joins the network directly, and the captive page opens.
- **Entry:** first boot with no network saved, or KEY held 5 s (side pad held 5 s once the shell exists, ADR-0015). It closes after 10 min without activity.
- **Flow:** the page lists nearby networks and the timezone. On save the clock tries to connect while keeping its access point up (AP+STA) and reports the result on the page; on failure the page asks for the password again. On success: "Connected" on the screen, NTP sync, Wi-Fi off, back to the face.
- **Up to five networks** stored in NVS as one list in try order (#33, first version stored one). A sync tries the first network directly; if it fails, one scan picks the other saved networks in range, tried in list order. The network that connects moves to the top, so the list stays in order of last success. Connecting a new network from the page puts it first; a full list forgets its last one. The page lists the saved networks (names only) with "Move up" and "Remove".
- **Later failures** (password changed, router down) never reopen setup by themselves (battery). The face keeps running from the RTC and shows a small Wi-Fi-with-`!` mark in a corner until a sync succeeds; the user re-enters setup with the KEY hold.
- **Web UI mode (F5, M5):** the same pages and API served on the home network at `http://inkwake.local`. Optional PIN, off by default, set and cleared from the web UI.
- **One app, a settings API:** static HTML/JS that calls `GET /api/settings`, `POST /api/settings` (partial JSON), `GET /api/scan`, and streamed uploads for files (tones). Each feature adds a section to the page and keys to the API. Settings stay in NVS until the LittleFS config (ADR-0005) arrives.
- **Pages built into the firmware** (gzipped, generated header), not on LittleFS, so setup works on a blank or wiped clock. Uploaded files (tones) go to LittleFS later; the default_16MB partition table already has the data partition.
- **Server:** ESP-IDF `esp_http_server` (ships with Arduino-ESP32, no extra dependency): several sockets at once for phones' captive-portal probes, chunked request bodies for uploads. DNS: Arduino `DNSServer` answering every name with the clock's address.
- The `wifi` serial command stays as a fallback for development.

## Alternatives rejected

- BLE provisioning (Espressif app, Improv BLE): needs an app or Chrome, adds 100-300 KB for one screen.
- SmartConfig / ESP-Touch: unreliable, poorly maintained apps.
- WPA2 setup network with a password on screen: safer against sniffing, but the user prefers a one-scan join for a network that lives a few minutes.
- ESPAsyncWebServer: third-party, uneven maintenance; Arduino `WebServer`: one client at a time, stalls on parallel captive probes.
- Pages on LittleFS: setup would break on an empty or corrupt filesystem.

## Consequences

- On the open setup network the home Wi-Fi password travels unencrypted: someone sniffing nearby during those minutes could read it. Accepted risk; revisit if it matters.
- The web UI on the home network has no login unless the user sets the PIN.
- Every later settings feature is a page section plus API keys, no new server work.
- Flash layout and flashing steps do not change.
