// SPDX-License-Identifier: GPL-3.0-only
// One NTP exchange over Wi-Fi (#13): Wi-Fi on, DNS, SNTP request/reply, Wi-Fi
// off. Blocks for up to about 15 s. Packet and schedule logic: lib/ntp.
#pragma once

#include <cstdint>

#include "ntp.h"

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
  uint32_t wifi_ms;   // Wi-Fi start to connected (or to the timeout)
  uint32_t total_ms;  // the whole call
};

Result run(const char* ssid, const char* pass, const char* server);

}  // namespace inkwake::ntp_sync
