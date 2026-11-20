// SPDX-License-Identifier: GPL-3.0-only
#include "provision.h"

#include <cstdio>
#include <cstring>

namespace inkwake::provision {

namespace {

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes [p, end) into out. False on bad %XX or overflow.
bool decode(const char* p, const char* end, char* out, size_t n) {
  size_t o = 0;
  while (p < end) {
    char c = *p++;
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (end - p < 2) return false;
      const int hi = hex_digit(p[0]);
      const int lo = hex_digit(p[1]);
      if (hi < 0 || lo < 0) return false;
      c = static_cast<char>(hi << 4 | lo);
      p += 2;
      if (c == '\0') return false;  // would cut the C string short
    }
    if (o + 1 >= n) return false;
    out[o++] = c;
  }
  out[o] = '\0';
  return true;
}

}  // namespace

void ap_name(const uint8_t mac[6], char* buf, size_t n) {
  snprintf(buf, n, "inkwake-%02X%02X", mac[4], mac[5]);
}

bool wifi_qr_text(const char* ssid, char* buf, size_t n) {
  static const char kHead[] = "WIFI:T:nopass;S:";
  size_t o = 0;
  auto put = [&](char c) {
    if (o + 1 >= n) return false;
    buf[o++] = c;
    return true;
  };
  for (const char* p = kHead; *p; ++p) {
    if (!put(*p)) return false;
  }
  for (const char* p = ssid; *p; ++p) {
    if (strchr("\\;,:\"", *p) != nullptr && !put('\\')) return false;
    if (!put(*p)) return false;
  }
  if (!put(';') || !put(';')) return false;
  buf[o] = '\0';
  return true;
}

bool ssid_valid(const char* ssid) {
  const size_t len = strlen(ssid);
  if (len == 0 || len > kSsidMax) return false;
  for (const char* p = ssid; *p; ++p) {
    const auto c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

bool pass_valid(const char* pass) {
  const size_t len = strlen(pass);
  if (len == 0) return true;
  if (len < kPassMin || len > kPassMax) return false;
  for (const char* p = pass; *p; ++p) {
    if (*p < 0x20 || *p > 0x7E) return false;
  }
  return true;
}

bool form_value(const char* body, const char* key, char* out, size_t n) {
  if (n == 0) return false;
  const size_t klen = strlen(key);
  const char* p = body;
  while (*p) {
    const char* amp = strchr(p, '&');
    const char* end = amp ? amp : p + strlen(p);
    const char* eq = static_cast<const char*>(memchr(p, '=', static_cast<size_t>(end - p)));
    const char* name_end = eq ? eq : end;
    if (static_cast<size_t>(name_end - p) == klen && strncmp(p, key, klen) == 0) {
      if (eq == nullptr) {
        out[0] = '\0';
        return true;
      }
      return decode(eq + 1, end, out, n);
    }
    if (!amp) break;
    p = amp + 1;
  }
  return false;
}

bool json_escape(const char* in, char* out, size_t n) {
  if (n == 0) return false;
  size_t o = 0;
  for (const char* p = in; *p; ++p) {
    const auto c = static_cast<unsigned char>(*p);
    char esc[8];
    size_t len;
    if (c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = static_cast<char>(c);
      len = 2;
    } else if (c < 0x20) {
      snprintf(esc, sizeof esc, "\\u%04x", c);
      len = 6;
    } else {
      esc[0] = static_cast<char>(c);
      len = 1;
    }
    if (o + len >= n) {
      // Cut inside a UTF-8 sequence: drop its first bytes too.
      if ((c & 0xC0) == 0x80) {
        while (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0x80) --o;
        if (o > 0 && static_cast<unsigned char>(out[o - 1]) >= 0xC0) --o;
      }
      out[o] = '\0';
      return false;
    }
    memcpy(out + o, esc, len);
    o += len;
  }
  out[o] = '\0';
  return true;
}

}  // namespace inkwake::provision
