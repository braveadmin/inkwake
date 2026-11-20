// SPDX-License-Identifier: GPL-3.0-only
// F12 NTP synchronization (#13), hardware-free part: the SNTP v4 packet
// (RFC 4330) and the schedule that decides when the clock syncs. The Wi-Fi and
// UDP side lives in firmware/src/ntp_sync.*.
//
// Times are Unix microseconds (int64) unless named otherwise.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::ntp {

constexpr size_t kPacketLen = 48;
constexpr uint16_t kPort = 123;

// Client request: LI 0, version 4, mode 3 (client), everything else zero
// except the transmit timestamp, which carries `nonce`. The server copies it
// into the reply's originate timestamp, which ties the reply to this request.
// RFC 9109 advises a random value there instead of the client's clock.
void build_request(uint64_t nonce, uint8_t out[kPacketLen]);

enum class Reply {
  Ok,
  Short,        // fewer than kPacketLen bytes
  NotServer,    // mode is not 4 (server)
  WrongNonce,   // originate timestamp is not our nonce: stale or forged reply
  Unsynced,     // leap indicator 3 (server clock not set) or stratum above 15
  KissOfDeath,  // stratum 0: the server asks us to back off or stop
  NoTime,       // receive or transmit timestamp is zero
};
const char* reply_name(Reply r);

struct ServerTimes {
  int64_t receive_us;   // T2: the server got the request
  int64_t transmit_us;  // T3: the server sent the reply
};

// Checks a reply against the request's nonce and extracts T2/T3.
Reply parse_reply(const uint8_t* buf, size_t n, uint64_t nonce, ServerTimes& out);

// 64-bit NTP timestamp (seconds since 1900 in the high word, binary fraction
// in the low word) to Unix microseconds. A seconds value with the top bit
// clear is taken as era 1 (from 2036-02-07), as RFC 4330 section 3 suggests,
// so this stays correct until 2104.
int64_t ntp_to_unix_us(uint64_t ntp);

// Offset to add to the local clock: ((T2 - T1) + (T3 - T4)) / 2, with T1 the
// local send time and T4 the local receive time. Round-trip delay: (T4 - T1) - (T3 - T2).
int64_t clock_offset_us(int64_t t1, const ServerTimes& s, int64_t t4);
int64_t round_trip_us(int64_t t1, const ServerTimes& s, int64_t t4);

// When to sync. Seconds on the system clock, which runs from the RTC once the
// time is set and from the build time before that; jumps when the time is set.
struct Schedule {
  int64_t last_try;  // 0 = not tried since power-on: sync at once
  int64_t last_ok;   // 0 = no successful sync since power-on
  uint8_t fails;     // failed tries in a row
};

// Retry delay after `fails` failures in a row (fails >= 1). While the time is
// not set it matters most: 1, 2, 4 ... minutes, at most an hour. With the time
// set: 15, 30, 60 ... minutes, at most 6 h or the regular interval if shorter.
int64_t retry_delay_s(uint8_t fails, bool time_set, int64_t interval_s);

// True if a sync is due at `now`. A clock that moved backwards since the last
// try (the time was set by hand) also makes it due.
bool due(const Schedule& s, int64_t now, bool time_set, int64_t interval_s);

// Records a try. `now` is the clock after a successful sync has set it.
void record(Schedule& s, int64_t now, bool ok);

}  // namespace inkwake::ntp
