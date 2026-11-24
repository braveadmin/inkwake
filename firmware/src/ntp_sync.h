// SPDX-License-Identifier: GPL-3.0-only
// One NTP exchange over Wi-Fi (#13): Wi-Fi on, DNS, SNTP request/reply, Wi-Fi
// off. Packet and schedule logic: lib/ntp.
//
// Saved networks (#33): the first network in the list (the last one that
// connected) is tried directly. If it does not connect, one scan finds which
// other saved networks are in range, and those are tried in list order. At
// home that costs nothing extra; after a move it costs the first try's timeout
// (10 s) plus a scan (~2-3 s), once, since the network that connects then moves
// to the top. Worst case, five saved networks in range that all fail: ~55 s.
// A hidden network is only tried while it is first (scans don't name it).
#pragma once

#include <cstdint>

#include "ntp.h"
#include "wifi_list.h"

namespace inkwake::ntp_sync {

enum class Status : uint8_t {
  Ok = 1,  // 0 is kept for "no sync on this wake" in the wake log
  NoWifiConfig,
  WifiFailed,  // not connected within the timeout (wrong password, out of range)
  DnsFailed,
  NoReply,   // no answer from the server on any try
  BadReply,  // answered, but parse_reply() rejected it (see `reply`)
};
const char* status_name(Status s);
// One letter for the wake log: + ok, W Wi-Fi, D DNS, N no reply, B bad reply, C no config.
char status_letter(Status s);

struct Result {
  Status status;
  ntp::Reply reply;   // the last reply's verdict when status is BadReply
  int64_t offset_us;  // to add to the system clock (Ok only)
  int64_t rtt_us;     // round trip of the exchange used (Ok only)
  int8_t network;     // index in the list of the network that connected, -1 if none
  uint8_t tried;      // networks tried
  uint32_t wifi_ms;   // Wi-Fi start to connected (or to the last timeout)
  uint32_t total_ms;  // the whole call
};

Result run(const provision::WifiList& nets, const char* server);

}  // namespace inkwake::ntp_sync
