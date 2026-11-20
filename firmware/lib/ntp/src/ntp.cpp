// SPDX-License-Identifier: GPL-3.0-only
#include "ntp.h"

#include <cstring>

namespace inkwake::ntp {

namespace {

constexpr int64_t kUnixMinusNtp = 2208988800;  // 1900-01-01 to 1970-01-01, seconds
constexpr int64_t kEra = int64_t{1} << 32;
constexpr int64_t kMaxRetrySetS = 6 * 3600;
constexpr size_t kOriginate = 24, kReceive = 32, kTransmit = 40;

uint64_t get64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = v << 8 | p[i];
  return v;
}

void put64(uint64_t v, uint8_t* p) {
  for (int i = 7; i >= 0; i--) {
    p[i] = static_cast<uint8_t>(v);
    v >>= 8;
  }
}

}  // namespace

void build_request(uint64_t nonce, uint8_t out[kPacketLen]) {
  memset(out, 0, kPacketLen);
  out[0] = 0 << 6 | 4 << 3 | 3;  // LI 0, VN 4, mode 3
  put64(nonce, out + kTransmit);
}

const char* reply_name(Reply r) {
  switch (r) {
    case Reply::Ok:
      return "ok";
    case Reply::Short:
      return "short packet";
    case Reply::NotServer:
      return "not a server reply";
    case Reply::WrongNonce:
      return "reply to another request";
    case Reply::Unsynced:
      return "server clock not synchronized";
    case Reply::KissOfDeath:
      return "server refused (kiss-of-death)";
    case Reply::NoTime:
      return "no timestamps in the reply";
  }
  return "?";
}

Reply parse_reply(const uint8_t* buf, size_t n, uint64_t nonce, ServerTimes& out) {
  if (n < kPacketLen) return Reply::Short;
  const uint8_t li = buf[0] >> 6, mode = buf[0] & 7, stratum = buf[1];
  if (mode != 4) return Reply::NotServer;
  if (get64(buf + kOriginate) != nonce) return Reply::WrongNonce;
  if (stratum == 0) return Reply::KissOfDeath;
  if (li == 3 || stratum > 15) return Reply::Unsynced;
  const uint64_t rx = get64(buf + kReceive), tx = get64(buf + kTransmit);
  if (rx == 0 || tx == 0) return Reply::NoTime;
  out.receive_us = ntp_to_unix_us(rx);
  out.transmit_us = ntp_to_unix_us(tx);
  return Reply::Ok;
}

int64_t ntp_to_unix_us(uint64_t ntp) {
  const uint32_t secs = static_cast<uint32_t>(ntp >> 32);
  const uint32_t frac = static_cast<uint32_t>(ntp);
  int64_t s = static_cast<int64_t>(secs) - kUnixMinusNtp;
  if (!(secs & 0x80000000u)) s += kEra;
  // frac / 2^32 seconds, rounded to the nearest microsecond.
  const int64_t us =
      static_cast<int64_t>((static_cast<uint64_t>(frac) * 1000000 + (1u << 31)) >> 32);
  return s * 1000000 + us;
}

int64_t clock_offset_us(int64_t t1, const ServerTimes& s, int64_t t4) {
  return ((s.receive_us - t1) + (s.transmit_us - t4)) / 2;
}

int64_t round_trip_us(int64_t t1, const ServerTimes& s, int64_t t4) {
  return (t4 - t1) - (s.transmit_us - s.receive_us);
}

int64_t retry_delay_s(uint8_t fails, bool time_set, int64_t interval_s) {
  const int64_t first = time_set ? 15 * 60 : 60;
  // With the time set, a missed sync is retried within 6 h even on a long interval.
  const int64_t cap =
      time_set ? (interval_s < kMaxRetrySetS ? interval_s : kMaxRetrySetS) : 60 * 60;
  int64_t d = first;
  for (uint8_t i = 1; i < fails && d < cap; i++) d *= 2;
  return d < cap ? d : cap;
}

bool due(const Schedule& s, int64_t now, bool time_set, int64_t interval_s) {
  if (s.last_try == 0 || now < s.last_try) return true;
  if (s.fails > 0) return now - s.last_try >= retry_delay_s(s.fails, time_set, interval_s);
  // Last try succeeded. Not set any more means the RTC lost its time since.
  return !time_set || now - s.last_ok >= interval_s;
}

void record(Schedule& s, int64_t now, bool ok) {
  s.last_try = now;
  if (ok) {
    s.last_ok = now;
    s.fails = 0;
  } else if (s.fails < 255) {
    s.fails++;
  }
}

}  // namespace inkwake::ntp
