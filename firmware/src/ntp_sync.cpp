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

Result run(const char* ssid, const char* pass, const char* server) {
  Result r{};
  r.reply = ntp::Reply::Ok;
  const uint32_t start = millis();
  if (ssid == nullptr || *ssid == '\0') {
    r.status = Status::NoWifiConfig;
    return r;
  }
  WiFi.persistent(false);  // credentials live in our own NVS keys, not the Wi-Fi driver's
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) delay(20);
  r.wifi_ms = millis() - start;
  if (WiFi.status() != WL_CONNECTED) {
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
