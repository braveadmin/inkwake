// SPDX-License-Identifier: GPL-3.0-only
// Alarm sound files on the LittleFS partition (#44, ADR-0006 amendment):
// /sounds/<name>.wav, WAV 16-bit PCM mono, 16-48 kHz (lib/sound). The
// partition ("spiffs" in default_16MB.csv, about 3.3 MB) is formatted by the
// firmware on first use, so there is no littlefs.bin to flash; Erase Flash
// wipes the files.
//
// Thread use: upload_* and remove() run on the web server's task while the
// main task may list or play; LittleFS has its own lock, and the upload writes
// to a temporary file that only appears under its name once it is complete.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sound.h"

namespace inkwake::sound_files {

// Mounts the partition. With `format`, an unformatted partition is formatted
// first (first use only: can take tens of seconds). The ring path never
// formats: an alarm must not wait for it.
bool mount(bool format);
bool mounted();
// Bytes used and total on the partition. False if not mounted.
bool usage(uint32_t& used, uint32_t& total);
// Largest file an upload may have (sound::max_file_bytes of this filesystem),
// whatever is stored now. kMaxFileBytes while not mounted.
uint32_t max_file();

struct Info {
  char name[sound::kMaxName + 1];
  uint32_t bytes;  // file size
  sound::Wav wav;  // valid when err == None
  sound::WavError err;
};
// Up to `max` sound files, sorted by name. 0 if not mounted.
size_t list(Info* out, size_t max);
// Number of sound files.
size_t count();
// True when /sounds/<name>.wav exists.
bool exists(const char* name);
// nullptr when deleted, else a short reason.
const char* remove(const char* name);

// Streamed upload: begin with the final name and size (checks the name, the
// per-file limit and free space), write the body in pieces, then end() checks
// the WAV header and moves the file into place (replacing a file of the same
// name). end(false) or a failed check deletes the partial file. One upload at
// a time.
const char* upload_begin(const char* name, uint32_t size);
bool upload_write(const uint8_t* data, size_t n);
const char* upload_end(bool complete);

// Plays one file in a loop (main task).
class Player {
 public:
  // nullptr when the file is open and its header is good, else why not.
  const char* open(const char* name);
  void close();
  bool is_open() const { return open_; }
  uint32_t rate() const { return wav_.rate; }
  // Fills `n` samples, looping at the end of the data without a gap. False on
  // a read error (the file is then closed).
  bool read(int16_t* samples, size_t n);

 private:
  bool open_ = false;
  sound::Wav wav_{};
  sound::Loop loop_{};
};

}  // namespace inkwake::sound_files
