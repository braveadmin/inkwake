// SPDX-License-Identifier: GPL-3.0-only
#include "sound_files.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace inkwake::sound_files {

namespace {

constexpr char kUploadTmp[] = "/sounds/.upload";

bool g_mounted = false;
std::atomic<bool> g_uploading{false};
File g_upload;
uint32_t g_upload_left = 0;
char g_upload_name[sound::kMaxName + 1];

File g_play;  // the one Player's file

size_t file_read(void* ctx, uint32_t offset, uint8_t* buf, size_t n) {
  File& f = *static_cast<File*>(ctx);
  if (!f.seek(offset)) return 0;
  return f.read(buf, n);
}

sound::WavError check_file(File& f, sound::Wav& w) {
  return sound::parse_wav(file_read, &f, static_cast<uint32_t>(f.size()), w);
}

uint32_t file_size(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const auto n = static_cast<uint32_t>(f.size());
  f.close();
  return n;
}

}  // namespace

bool mount(bool format) {
  if (g_mounted) return true;
  // Label "spiffs" is the data partition in default_16MB.csv.
  g_mounted = LittleFS.begin(format, "/littlefs", 4, "spiffs");
  if (g_mounted && !LittleFS.exists(sound::kDir)) LittleFS.mkdir(sound::kDir);
  return g_mounted;
}

bool mounted() { return g_mounted; }

bool usage(uint32_t& used, uint32_t& total) {
  if (!g_mounted) return false;
  used = static_cast<uint32_t>(LittleFS.usedBytes());
  total = static_cast<uint32_t>(LittleFS.totalBytes());
  return true;
}

size_t list(Info* out, size_t max) {
  if (!g_mounted) return 0;
  File dir = LittleFS.open(sound::kDir);
  if (!dir || !dir.isDirectory()) return 0;
  size_t n = 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    Info& i = out[n];
    if (f.isDirectory() || !sound::name_from_file(f.name(), i.name, sizeof i.name)) continue;
    i.bytes = static_cast<uint32_t>(f.size());
    i.wav = {};
    i.err = check_file(f, i.wav);
    ++n;
  }
  std::sort(out, out + n, [](const Info& a, const Info& b) { return strcmp(a.name, b.name) < 0; });
  return n;
}

size_t count() {
  if (!g_mounted) return 0;
  File dir = LittleFS.open(sound::kDir);
  if (!dir || !dir.isDirectory()) return 0;
  size_t n = 0;
  char name[sound::kMaxName + 1];
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory() && sound::name_from_file(f.name(), name, sizeof name)) ++n;
  }
  return n;
}

bool exists(const char* name) {
  char path[sound::kMaxPath];
  return g_mounted && sound::path_for(name, path, sizeof path) && LittleFS.exists(path);
}

const char* remove(const char* name) {
  char path[sound::kMaxPath];
  if (!sound::path_for(name, path, sizeof path)) return "bad sound name";
  if (!g_mounted) return "sound storage not ready";
  if (!LittleFS.exists(path)) return "no sound by that name";
  return LittleFS.remove(path) ? nullptr : "could not delete the file";
}

const char* upload_begin(const char* name, uint32_t size) {
  if (!sound::name_valid(name)) return "name: 1-15 of a-z, 0-9, - and _";
  if (size > sound::kMaxFileBytes) return "file too big: 1.5 MB at most";
  if (size < 46) return "file too short for a WAV";
  if (!g_mounted) return "sound storage not ready";
  bool expected = false;
  if (!g_uploading.compare_exchange_strong(expected, true)) return "another upload is running";
  char path[sound::kMaxPath];
  sound::path_for(name, path, sizeof path);
  uint32_t used = 0, total = 0;
  usage(used, total);
  LittleFS.remove(kUploadTmp);  // left over from a cut upload
  const bool replacing = LittleFS.exists(path);
  const uint32_t replaced = replacing ? file_size(path) : 0;
  if (!replacing && count() >= sound::kMaxFiles) {
    g_uploading = false;
    return "20 sounds already: delete one first";
  }
  const uint32_t free_bytes = total > used ? total - used : 0;
  if (static_cast<uint64_t>(size) + sound::kReserveBytes >
      static_cast<uint64_t>(free_bytes) + replaced) {
    g_uploading = false;
    return "not enough space: delete a sound first";
  }
  g_upload = LittleFS.open(kUploadTmp, "w");
  if (!g_upload) {
    g_uploading = false;
    return "could not create the file";
  }
  snprintf(g_upload_name, sizeof g_upload_name, "%s", name);
  g_upload_left = size;
  return nullptr;
}

bool upload_write(const uint8_t* data, size_t n) {
  if (!g_uploading || !g_upload || n > g_upload_left) return false;
  if (g_upload.write(data, n) != n) return false;
  g_upload_left -= static_cast<uint32_t>(n);
  return true;
}

const char* upload_end(bool complete) {
  if (!g_uploading) return "no upload running";
  const char* err = nullptr;
  if (g_upload) g_upload.close();
  if (!complete || g_upload_left != 0) {
    err = "upload cut short";
  } else {
    File f = LittleFS.open(kUploadTmp, "r");
    sound::Wav w{};
    const sound::WavError e = f ? check_file(f, w) : sound::WavError::ReadError;
    if (f) f.close();
    if (e != sound::WavError::None) {
      err = sound::wav_error_text(e);
    } else {
      char path[sound::kMaxPath];
      sound::path_for(g_upload_name, path, sizeof path);
      if (LittleFS.exists(path)) LittleFS.remove(path);
      if (!LittleFS.rename(kUploadTmp, path)) err = "could not save the file";
    }
  }
  if (err) LittleFS.remove(kUploadTmp);
  g_uploading = false;
  return err;
}

const char* Player::open(const char* name) {
  close();
  char path[sound::kMaxPath];
  if (!sound::path_for(name, path, sizeof path)) return "bad sound name";
  if (!g_mounted) return "sound storage not ready (no files uploaded yet?)";
  if (!LittleFS.exists(path)) return "sound file missing";
  g_play = LittleFS.open(path, "r");
  if (!g_play) return "could not open the sound file";
  const sound::WavError e = check_file(g_play, wav_);
  if (e != sound::WavError::None) {
    g_play.close();
    return sound::wav_error_text(e);
  }
  loop_ = {0};
  open_ = true;
  return nullptr;
}

void Player::close() {
  if (open_) g_play.close();
  open_ = false;
}

bool Player::read(int16_t* samples, size_t n) {
  if (!open_) return false;
  auto* out = reinterpret_cast<uint8_t*>(samples);
  uint32_t want = static_cast<uint32_t>(n * 2);
  while (want > 0) {
    const sound::Span s = sound::loop_next(loop_, wav_, want);
    if (s.bytes == 0 || file_read(&g_play, s.offset, out, s.bytes) != s.bytes) {
      close();
      return false;
    }
    out += s.bytes;
    want -= s.bytes;
  }
  // WAV is little-endian, like the ESP32: the bytes are the samples.
  return true;
}

}  // namespace inkwake::sound_files
