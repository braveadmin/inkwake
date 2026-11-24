// SPDX-License-Identifier: GPL-3.0-only
#include "wifi_list.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace inkwake::provision {

namespace {

// Moves nets[i] to nets[0], shifting the ones above it down by one.
void to_top(WifiList& list, size_t i) {
  const Network n = list.nets[i];
  memmove(&list.nets[1], &list.nets[0], i * sizeof(Network));
  list.nets[0] = n;
}

}  // namespace

int find(const WifiList& list, const char* ssid) {
  for (size_t i = 0; i < list.count && i < kMaxNetworks; ++i) {
    if (strcmp(list.nets[i].ssid, ssid) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool remember(WifiList& list, const char* ssid, const char* pass, char* dropped) {
  if (dropped != nullptr) dropped[0] = '\0';
  if (!ssid_valid(ssid) || !pass_valid(pass)) return false;
  const int known = find(list, ssid);
  size_t i;
  if (known >= 0) {
    i = static_cast<size_t>(known);
  } else if (list.count < kMaxNetworks) {
    i = list.count++;
  } else {
    i = kMaxNetworks - 1;  // the one tried last
    if (dropped != nullptr) snprintf(dropped, kSsidMax + 1, "%s", list.nets[i].ssid);
  }
  snprintf(list.nets[i].ssid, sizeof list.nets[i].ssid, "%s", ssid);
  snprintf(list.nets[i].pass, sizeof list.nets[i].pass, "%s", pass);
  to_top(list, i);
  return true;
}

bool promote(WifiList& list, size_t i) {
  if (i >= list.count) return false;
  to_top(list, i);
  return true;
}

bool move_up(WifiList& list, size_t i) {
  if (i == 0 || i >= list.count) return false;
  const Network n = list.nets[i];
  list.nets[i] = list.nets[i - 1];
  list.nets[i - 1] = n;
  return true;
}

bool remove(WifiList& list, size_t i) {
  if (i >= list.count) return false;
  memmove(&list.nets[i], &list.nets[i + 1], (list.count - i - 1) * sizeof(Network));
  list.count--;
  memset(&list.nets[list.count], 0, sizeof(Network));  // no stale password left behind
  return true;
}

size_t others_in_range(const WifiList& list, const char* const* seen, size_t n_seen, uint8_t* out) {
  size_t o = 0;
  for (size_t i = 1; i < list.count; ++i) {
    for (size_t j = 0; j < n_seen; ++j) {
      if (strcmp(list.nets[i].ssid, seen[j]) == 0) {
        out[o++] = static_cast<uint8_t>(i);
        break;
      }
    }
  }
  return o;
}

size_t encode(const WifiList& list, uint8_t* buf, size_t n) {
  if (list.count > kMaxNetworks) return 0;
  size_t o = 0;
  auto put = [&](const void* p, size_t len) {
    if (o + len > n) return false;
    memcpy(buf + o, p, len);
    o += len;
    return true;
  };
  const uint8_t head[2] = {kBlobVersion, list.count};
  if (!put(head, 2)) return 0;
  for (size_t i = 0; i < list.count; ++i) {
    const Network& net = list.nets[i];
    for (const char* s : {net.ssid, net.pass}) {
      const uint8_t len = static_cast<uint8_t>(strnlen(s, kPassMax + 1));
      if (!put(&len, 1) || !put(s, len)) return 0;
    }
  }
  return o;
}

bool decode(const uint8_t* buf, size_t n, WifiList& list) {
  list = WifiList{};
  if (n < 2 || buf[0] != kBlobVersion || buf[1] > kMaxNetworks) return false;
  size_t o = 2;
  WifiList out{};
  out.count = buf[1];
  for (size_t i = 0; i < out.count; ++i) {
    Network& net = out.nets[i];
    for (auto [dst, cap] : {std::pair<char*, size_t>{net.ssid, sizeof net.ssid},
                            std::pair<char*, size_t>{net.pass, sizeof net.pass}}) {
      if (o >= n) return false;
      const size_t len = buf[o++];
      if (len >= cap || o + len > n) return false;
      memcpy(dst, buf + o, len);
      dst[len] = '\0';
      o += len;
    }
    if (!ssid_valid(net.ssid) || !pass_valid(net.pass) ||
        find(out, net.ssid) != static_cast<int>(i)) {
      return false;  // broken, or a duplicate name
    }
  }
  if (o != n) return false;
  list = out;
  return true;
}

}  // namespace inkwake::provision
