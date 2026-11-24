// SPDX-License-Identifier: GPL-3.0-only
#include "ntp_sync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <sys/time.h>

namespace inkwake::ntp_sync {

namespace {

constexpr uint32_t kWifiTimeoutMs = 10'000;
constexpr uint32_t kReplyTimeoutMs = 1'500;
constexpr int kTries = 3;

int64_t now_us() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

// True once connected, false after kWifiTimeoutMs.
bool connect(const provision::Network& net) {
  WiFi.begin(net.ssid, net.pass);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kWifiTimeoutMs) delay(20);
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false);  // stop the driver's retries before the next step
  return false;
}

constexpr size_t kMaxSeen = 32;
char g_seen[kMaxSeen][provision::kSsidMax + 1];  // scan results, names only

// One blocking scan into g_seen. Returns how many names it holds.
size_t scan() {
  const int n = WiFi.scanNetworks(false);
  size_t o = 0;
  for (int i = 0; i < n && o < kMaxSeen; ++i) {
    const String name = WiFi.SSID(i);
    if (name.isEmpty() || name.length() > provision::kSsidMax) continue;  // hidden
    snprintf(g_seen[o++], sizeof g_seen[0], "%s", name.c_str());
  }
  WiFi.scanDelete();
  return o;
}

// Tries the saved networks as described in ntp_sync.h. Index of the one that
// connected, or -1.
int connect_any(const provision::WifiList& nets, Result& r) {
  r.tried = 1;
  if (connect(nets.nets[0])) return 0;
  if (nets.count < 2) return -1;
  const size_t n_seen = scan();
  const char* seen[kMaxSeen];
  for (size_t i = 0; i < n_seen; ++i) seen[i] = g_seen[i];
  uint8_t order[provision::kMaxNetworks];
  const size_t n = provision::others_in_range(nets, seen, n_seen, order);
  for (size_t i = 0; i < n; ++i) {
    r.tried++;
    if (connect(nets.nets[order[i]])) return order[i];
  }
  return -1;
}

void wifi_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Sends one request and waits for its reply. Replies to earlier tries carry
// another nonce and are skipped.
ntp::Reply exchange(WiFiUDP& udp, const IPAddress& ip, Result& r, bool& answered) {
  const uint64_t nonce = static_cast<uint64_t>(esp_random()) << 32 | esp_random();
  uint8_t buf[68];
  ntp::build_request(nonce, buf);
  const int64_t t1 = now_us();
  udp.beginPacket(ip, ntp::kPort);
  udp.write(buf, ntp::kPacketLen);
  udp.endPacket();
  ntp::Reply verdict = ntp::Reply::Short;
  const uint32_t t0 = millis();
  while (millis() - t0 < kReplyTimeoutMs) {
    if (udp.parsePacket() <= 0) {
      delay(2);
      continue;
    }
    const int64_t t4 = now_us();
    const int n = udp.read(buf, sizeof buf);
    ntp::ServerTimes st{};
    verdict = ntp::parse_reply(buf, n > 0 ? static_cast<size_t>(n) : 0, nonce, st);
    if (verdict == ntp::Reply::WrongNonce) continue;
    answered = true;
    if (verdict == ntp::Reply::Ok) {
      r.offset_us = ntp::clock_offset_us(t1, st, t4);
      r.rtt_us = ntp::round_trip_us(t1, st, t4);
    }
    return verdict;
  }
  return verdict;
}

}  // namespace

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok:
      return "ok";
    case Status::NoWifiConfig:
      return "no Wi-Fi configured";
    case Status::WifiFailed:
      return "Wi-Fi did not connect";
    case Status::DnsFailed:
      return "server name not found";
    case Status::NoReply:
      return "no reply from the server";
    case Status::BadReply:
      return "bad reply";
  }
  return "?";
}

char status_letter(Status s) {
  switch (s) {
    case Status::Ok:
      return '+';
    case Status::NoWifiConfig:
      return 'C';
    case Status::WifiFailed:
      return 'W';
    case Status::DnsFailed:
      return 'D';
    case Status::NoReply:
      return 'N';
    case Status::BadReply:
      return 'B';
  }
  return '?';
}

Result run(const provision::WifiList& nets, const char* server) {
  Result r{};
  r.reply = ntp::Reply::Ok;
  r.network = -1;
  const uint32_t start = millis();
  if (nets.count == 0) {
    r.status = Status::NoWifiConfig;
    return r;
  }
  WiFi.persistent(false);  // credentials live in our own NVS keys, not the Wi-Fi driver's
  WiFi.mode(WIFI_STA);
  r.network = static_cast<int8_t>(connect_any(nets, r));
  r.wifi_ms = millis() - start;
  if (r.network < 0) {
    r.status = Status::WifiFailed;
  } else {
    IPAddress ip;
    if (!WiFi.hostByName(server, ip)) {
      r.status = Status::DnsFailed;
    } else {
      WiFiUDP udp;
      // Random source port, so off-path replies are harder to guess.
      udp.begin(static_cast<uint16_t>(49152 + esp_random() % 16384));
      r.status = Status::NoReply;
      for (int i = 0; i < kTries; i++) {
        bool answered = false;
        const ntp::Reply v = exchange(udp, ip, r, answered);
        if (v == ntp::Reply::Ok) {
          r.status = Status::Ok;
          break;
        }
        if (answered) {
          r.status = Status::BadReply;
          r.reply = v;
          if (v == ntp::Reply::KissOfDeath) break;  // asked to back off: don't insist
        }
      }
      udp.stop();
    }
  }
  wifi_off();
  r.total_ms = millis() - start;
  return r;
}

}  // namespace inkwake::ntp_sync
