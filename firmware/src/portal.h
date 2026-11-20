// SPDX-License-Identifier: GPL-3.0-only
// Wi-Fi setup mode (#20, ADR-0016): an open access point "inkwake-XXXX", a DNS
// server that answers every name with the clock (so phones open the captive
// page), and the settings page with its small API:
//
//   GET  /                 the page (gzipped, web/index.html)
//   GET  /api/settings     {"ssid","tz","tz_saved","ap"}; never the password
//   POST /api/settings     form: tz=POSIX, now=Unix UTC (only used if the time is not set)
//   GET  /api/scan[?fresh=1]  {"scanning", "networks":[{"ssid","rssi","open"}]}
//   POST /api/wifi         form: ssid, pass; starts a connection try
//   GET  /api/status       {"state":"idle|connecting|connected|failed","ssid"}
//   anything else          302 to the page (captive portal probes)
//
// Web requests run in the server's own task and only queue work; run() does
// all Wi-Fi changes and calls the hooks on the caller's task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace inkwake::portal {

enum class Screen : uint8_t {
  Setup,       // QR code to join the setup network
  Connecting,  // trying the home network
  Connected,
  Failed,  // QR code again, asking to retry
};

enum class Outcome : uint8_t {
  Connected,  // credentials saved and working
  Timeout,    // no page activity for the idle timeout
  Cancelled,  // KEY pressed
};

struct Hooks {
  void (*show)(Screen screen, const char* ap_name, const char* ssid);
  // Saved values for the page: network name (may be empty), TZ string, and
  // whether a TZ was ever saved (else the page suggests the phone's zone).
  void (*load)(char* ssid, size_t ssid_n, char* tz, size_t tz_n, bool& tz_saved);
  bool (*apply_tz)(const char* posix);  // checked already; false if saving failed
  void (*offer_time)(time_t utc);       // phone's clock, for a clock that lost its time
  bool (*save_wifi)(const char* ssid, const char* pass);
  bool (*cancel)();  // polled; true ends setup
};

const char* outcome_name(Outcome o);

// Blocks until a network connects, the page is idle for `idle_ms`, or cancel().
// Wi-Fi is off again on return.
Outcome run(const Hooks& hooks, uint32_t idle_ms);

}  // namespace inkwake::portal
