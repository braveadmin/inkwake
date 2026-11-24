// SPDX-License-Identifier: GPL-3.0-only
// Saved Wi-Fi networks (#33): up to kMaxNetworks, kept in the order they are
// tried. A network that connects moves to the top, so the list stays in order
// of last success; the web UI can also remove networks and move them up.
// Stored in NVS as one blob (encode/decode below).
#pragma once

#include <cstddef>
#include <cstdint>

#include "provision.h"

namespace inkwake::provision {

constexpr size_t kMaxNetworks = 5;

struct Network {
  char ssid[kSsidMax + 1];
  char pass[kPassMax + 1];  // empty for an open network
};

struct WifiList {
  Network nets[kMaxNetworks];  // nets[0] is tried first
  uint8_t count;
};

// Index of the network called `ssid`, or -1.
int find(const WifiList& list, const char* ssid);

// Puts the network at the top: a new one, or a known one with its (maybe new)
// password. A full list forgets its last network to make room; its name is
// copied into `dropped` (if not null, kSsidMax + 1 bytes), else `dropped` is
// set empty. False (list unchanged) if the name or password is not valid.
bool remember(WifiList& list, const char* ssid, const char* pass, char* dropped = nullptr);

// Moves network i to the top (it just connected). False if i is out of range.
bool promote(WifiList& list, size_t i);

// Swaps network i with the one above it. False if i is 0 or out of range.
bool move_up(WifiList& list, size_t i);

// False if i is out of range.
bool remove(WifiList& list, size_t i);

// Indexes of the networks after the first whose names are in `seen` (a scan's
// results), in list order. Returns how many were written to `out` (room for
// kMaxNetworks). The first network is tried before any scan, so it is left out.
size_t others_in_range(const WifiList& list, const char* const* seen, size_t n_seen, uint8_t* out);

// Blob layout: version, count, then per network: ssid length, ssid bytes,
// password length, password bytes. No terminating NULs.
constexpr uint8_t kBlobVersion = 1;
constexpr size_t kBlobMax = 2 + kMaxNetworks * (2 + kSsidMax + kPassMax);

// Bytes written, or 0 if `n` is too small.
size_t encode(const WifiList& list, uint8_t* buf, size_t n);

// False (list emptied) on an unknown version, a truncated or overlong blob, or
// a name or password that fails ssid_valid()/pass_valid().
bool decode(const uint8_t* buf, size_t n, WifiList& list);

}  // namespace inkwake::provision
