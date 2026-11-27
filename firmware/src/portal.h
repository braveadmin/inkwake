// SPDX-License-Identifier: GPL-3.0-only
// Wi-Fi setup mode (#20, ADR-0016): an open access point "inkwake-XXXX", a DNS
// server that answers every name with the clock (so phones open the captive
// page), and the settings page with its small API:
//
//   GET  /                 the page (gzipped, web/index.html)
//   GET  /api/settings     {"networks":[names],"max","tz","tz_saved","ap"}; never passwords
//   POST /api/settings     form: tz=POSIX, now=Unix UTC (only used if the time is not set)
//   GET  /api/scan[?fresh=1]  {"scanning", "networks":[{"ssid","rssi","open"}]}
//   POST /api/wifi         form: ssid, pass; starts a connection try
//   GET  /api/status       {"state":"idle|connecting|connected|failed","ssid"}
//   POST /api/networks     form: op=remove|up, ssid; edits the saved list (#33),
//                          answers {"networks":[names]}
//   POST /api/close        ends setup
//   GET  /api/alarms       {"rev":N,"error":"...","alarms":[...]} (#2; list from
//                          Hooks::alarms_json)
//   POST /api/alarms       form for Hooks::alarm_edit; the page polls GET until
//                          rev changes
//   GET  /api/sounds       {"ready","sounds":[{"name","bytes","seconds","rate","error"}],
//                          "used","total","max_file","max"} (#44)
//   POST /api/sounds       form: op=del, name
//   POST /api/sounds/upload?file=NAME.wav  body = the WAV file; answers {"name"}
//   anything else          302 to the page (captive portal probes)
//
// Web requests run in the server's own task and only queue work; run() does
// all Wi-Fi changes and calls the hooks on the caller's task. Sound files are
// the exception: they are listed, deleted and streamed in on the server's task
// (src/sound_files keeps that safe).
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "wifi_list.h"

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
  Closed,     // "Done" on the page
};

struct Hooks {
  void (*show)(Screen screen, const char* ap_name, const char* ssid);
  // Saved values for the page: the network list, TZ string, and whether a TZ
  // was ever saved (else the page suggests the phone's zone).
  void (*load)(provision::WifiList& nets, char* tz, size_t tz_n, bool& tz_saved);
  bool (*apply_tz)(const char* posix);  // checked already; false if saving failed
  void (*offer_time)(time_t utc);       // phone's clock, for a clock that lost its time
  // The whole list, after a network connected (it is then first) or the page
  // removed or moved one.
  bool (*save_networks)(const provision::WifiList& nets);
  bool (*cancel)();  // polled; true ends setup
  // Alarms (#2): JSON array of the alarms into buf (always NUL-terminated).
  void (*alarms_json)(char* buf, size_t n);
  // Applies a POST /api/alarms form body. nullptr when done, else a short reason.
  const char* (*alarm_edit)(const char* form);
};

const char* outcome_name(Outcome o);

// Blocks until a network connects, the page is idle for `idle_ms`, or cancel().
// Wi-Fi is off again on return.
Outcome run(const Hooks& hooks, uint32_t idle_ms);

}  // namespace inkwake::portal
