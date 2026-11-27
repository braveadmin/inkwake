// SPDX-License-Identifier: GPL-3.0-only
#include "sound.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace inkwake::sound {

namespace {

bool name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool read_all(ReadFn read, void* ctx, uint32_t offset, uint8_t* buf, size_t n) {
  return read(ctx, offset, buf, n) == n;
}

// Case-insensitive suffix check.
bool ends_with_ci(const char* s, size_t len, const char* suffix) {
  const size_t m = strlen(suffix);
  if (len < m) return false;
  for (size_t i = 0; i < m; ++i) {
    char c = s[len - m + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

}  // namespace

bool name_valid(const char* name) {
  if (name == nullptr) return false;
  const size_t len = strlen(name);
  if (len == 0 || len > kMaxName || strcmp(name, kDefault) == 0) return false;
  for (size_t i = 0; i < len; ++i) {
    if (!name_char(name[i])) return false;
  }
  return true;
}

bool alarm_sound_valid(const char* name) {
  return name != nullptr && (strcmp(name, kDefault) == 0 || name_valid(name));
}

bool path_for(const char* name, char* out, size_t n) {
  if (!name_valid(name)) return false;
  const int w = snprintf(out, n, "%s/%s%s", kDir, name, kExt);
  return w > 0 && static_cast<size_t>(w) < n;
}

bool name_from_file(const char* file, char* out, size_t n) {
  if (file == nullptr) return false;
  const char* base = strrchr(file, '/');
  base = base ? base + 1 : file;
  const size_t len = strlen(base);
  const size_t ext = strlen(kExt);
  if (len <= ext || strcmp(base + len - ext, kExt) != 0) return false;
  const size_t name_len = len - ext;
  if (name_len > kMaxName || name_len + 1 > n) return false;
  memcpy(out, base, name_len);
  out[name_len] = '\0';
  return name_valid(out);
}

bool name_from_upload(const char* file, char* out, size_t n) {
  if (file == nullptr || n == 0) return false;
  const char* base = file;
  for (const char* p = file; *p; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  size_t len = strlen(base);
  if (ends_with_ci(base, len, kExt)) len -= strlen(kExt);
  size_t o = 0;
  for (size_t i = 0; i < len && o < kMaxName && o + 1 < n; ++i) {
    char c = base[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c == ' ' || c == '.') c = '-';
    if (!name_char(c)) continue;
    if (c == '-' && (o == 0 || out[o - 1] == '-')) continue;  // no leading or double dashes
    out[o++] = c;
  }
  while (o > 0 && out[o - 1] == '-') --o;  // nor trailing ones
  out[o] = '\0';
  return name_valid(out);
}

const char* wav_error_text(WavError e) {
  switch (e) {
    case WavError::None:
      return "ok";
    case WavError::TooShort:
      return "file too short for a WAV";
    case WavError::NotRiff:
      return "not a WAV file";
    case WavError::NoFormat:
      return "WAV without a format chunk";
    case WavError::NotPcm:
      return "compressed WAV: needs 16-bit PCM";
    case WavError::NotMono:
      return "stereo WAV: needs mono";
    case WavError::Not16Bit:
      return "WAV is not 16-bit";
    case WavError::BadRate:
      return "sample rate must be 16 to 48 kHz";
    case WavError::NoData:
      return "WAV without sound data";
    case WavError::Empty:
      return "WAV with no samples";
    case WavError::ReadError:
      return "could not read the file";
  }
  return "?";
}

WavError parse_wav(ReadFn read, void* ctx, uint32_t file_size, Wav& out) {
  if (file_size < 12 + 8) return WavError::TooShort;
  uint8_t h[12];
  if (!read_all(read, ctx, 0, h, sizeof h)) return WavError::ReadError;
  if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return WavError::NotRiff;
  bool have_fmt = false;
  uint32_t rate = 0;
  uint32_t off = 12;
  while (static_cast<uint64_t>(off) + 8 <= file_size) {
    uint8_t c[8];
    if (!read_all(read, ctx, off, c, sizeof c)) return WavError::ReadError;
    const uint32_t size = le32(c + 4);
    if (memcmp(c, "fmt ", 4) == 0) {
      if (size < 16) return WavError::NoFormat;
      uint8_t f[26] = {};  // 16 bytes PCM, up to 40 for EXTENSIBLE (subformat at 24)
      const size_t want = size >= sizeof f ? sizeof f : 16;
      if (!read_all(read, ctx, off + 8, f, want)) return WavError::ReadError;
      uint16_t tag = le16(f);
      if (tag == 0xFFFE) tag = size >= sizeof f ? le16(f + 24) : 0;  // EXTENSIBLE subformat
      if (tag != 1) return WavError::NotPcm;
      if (le16(f + 2) != 1) return WavError::NotMono;
      if (le16(f + 14) != 16) return WavError::Not16Bit;
      rate = le32(f + 4);
      if (rate < kMinRate || rate > kMaxRate) return WavError::BadRate;
      have_fmt = true;
    } else if (memcmp(c, "data", 4) == 0) {
      if (!have_fmt) return WavError::NoFormat;
      const uint32_t start = off + 8;
      uint32_t bytes = file_size - start;  // a header claiming more is cut to the file
      if (size < bytes) bytes = size;
      bytes &= ~1u;
      if (bytes < 2) return WavError::Empty;
      out = {rate, start, bytes};
      return WavError::None;
    }
    const uint64_t next = static_cast<uint64_t>(off) + 8 + size + (size & 1);
    if (next > file_size) break;
    off = static_cast<uint32_t>(next);
  }
  return have_fmt ? WavError::NoData : WavError::NoFormat;
}

uint32_t max_file_bytes(uint32_t total_bytes) {
  if (total_bytes <= kReserveBytes) return 0;
  const uint32_t room = total_bytes - kReserveBytes;
  return room < kMaxFileBytes ? room : kMaxFileBytes;
}

uint32_t seconds(const Wav& w) { return w.rate ? w.data_bytes / 2 / w.rate : 0; }

Span loop_next(Loop& loop, const Wav& w, uint32_t want) {
  want &= ~1u;
  if (want == 0 || w.data_bytes < 2) return {w.data_offset + loop.pos, 0};
  if (loop.pos >= w.data_bytes) loop.pos = 0;
  const uint32_t left = w.data_bytes - loop.pos;
  const uint32_t take = want < left ? want : left;
  const Span s{w.data_offset + loop.pos, take};
  loop.pos += take;
  if (loop.pos >= w.data_bytes) loop.pos = 0;
  return s;
}

uint32_t gain_q15(uint8_t pct) {
  if (pct == 0) return 0;
  if (pct >= 100) return 32768;
  return static_cast<uint32_t>(32768.0f * std::pow(10.0f, (pct - 100) * 0.4f / 20.0f) + 0.5f);
}

void apply_gain(int16_t* s, size_t n, uint32_t gain) {
  for (size_t i = 0; i < n; ++i) {
    int32_t v = static_cast<int32_t>((static_cast<int64_t>(s[i]) * gain) >> 15);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    s[i] = static_cast<int16_t>(v);
  }
}

}  // namespace inkwake::sound
