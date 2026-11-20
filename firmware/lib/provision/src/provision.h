// SPDX-License-Identifier: GPL-3.0-only
// Wi-Fi provisioning helpers without hardware (#20, ADR-0016): setup network
// name, the Wi-Fi QR code text, input checks, form decoding and JSON escaping
// for the setup page's API.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::provision {

constexpr size_t kSsidMax = 32;  // bytes, IEEE 802.11
constexpr size_t kPassMin = 8;   // WPA2 passphrase
constexpr size_t kPassMax = 63;

// "inkwake-A1B2" from the last two MAC bytes. `n` >= 13.
void ap_name(const uint8_t mac[6], char* buf, size_t n);

// Text of a QR code that joins an open network: WIFI:T:nopass;S:<ssid>;;
// with \ ; , : " escaped. False if it does not fit.
bool wifi_qr_text(const char* ssid, char* buf, size_t n);

// 1 to 32 bytes, no control characters.
bool ssid_valid(const char* ssid);
// Empty (open network) or 8 to 63 printable ASCII characters.
bool pass_valid(const char* pass);

// Value of `key` in an application/x-www-form-urlencoded body, decoded
// (+ is a space, %XX a byte). False if the key is missing, the encoding is
// broken, or the value does not fit (including the NUL).
bool form_value(const char* body, const char* key, char* out, size_t n);

// `in` as the inside of a JSON string. False if it does not fit; `out` then
// holds as much as fitted, cut at a character boundary.
bool json_escape(const char* in, char* out, size_t n);

}  // namespace inkwake::provision
