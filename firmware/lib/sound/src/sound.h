// SPDX-License-Identifier: GPL-3.0-only
// Alarm sound files (#44, F3): names, paths, WAV header parsing and the loop
// position, for files kept on the clock's LittleFS partition. Hardware-free,
// unit tested in test/test_sound.
//
// One format only: WAV, 16-bit PCM, mono, 16-48 kHz (no decoder, least CPU and
// battery while ringing). An alarm's `sound` is either kDefault (the built-in
// beep) or a file name: "birds" plays /sounds/birds.wav.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::sound {

constexpr char kDefault[] = "default";  // the built-in beep
constexpr char kDir[] = "/sounds";
constexpr char kExt[] = ".wav";
constexpr size_t kMaxName = 15;  // same as sched::kMaxSound (bytes)
constexpr size_t kMaxPath = sizeof kDir + kMaxName + sizeof kExt;  // "/sounds/" + name + ".wav\0"

constexpr uint32_t kMinRate = 16000;
constexpr uint32_t kMaxRate = 48000;
constexpr uint32_t kMaxFileBytes = 1536 * 1024;  // per file; the partition holds about 3.3 MB
constexpr size_t kMaxFiles = 20;
// Space kept free after an upload, for LittleFS metadata.
constexpr uint32_t kReserveBytes = 64 * 1024;

// A file sound name: 1-15 of a-z, 0-9, '-', '_', not kDefault.
bool name_valid(const char* name);
// What an alarm's sound may be: kDefault or a valid file name.
bool alarm_sound_valid(const char* name);
// "birds" -> "/sounds/birds.wav". False if the name is not valid or n is too small.
bool path_for(const char* name, char* out, size_t n);
// A directory entry "birds.wav" (or "/sounds/birds.wav") -> "birds". False for
// anything else (other extensions, bad names, upload temp files).
bool name_from_file(const char* file, char* out, size_t n);
// A file name picked on the phone -> a sound name: the directory part and the
// extension go, letters are lowercased, spaces and dots become '-', other
// characters are dropped, and it is cut to kMaxName. "Morning Birds.WAV" ->
// "morning-birds". False when nothing valid is left (or it would be kDefault).
bool name_from_upload(const char* file, char* out, size_t n);

enum class WavError : uint8_t {
  None,
  TooShort,   // smaller than a WAV header
  NotRiff,    // no "RIFF....WAVE" header
  NoFormat,   // no "fmt " chunk before the data
  NotPcm,     // compressed (format tag other than PCM)
  NotMono,    // channels != 1
  Not16Bit,   // bits per sample != 16
  BadRate,    // outside kMinRate .. kMaxRate
  NoData,     // no "data" chunk
  Empty,      // data chunk with less than one sample
  ReadError,  // the reader returned less than asked
};
// Short English reason, for the serial log and the setup page.
const char* wav_error_text(WavError e);

struct Wav {
  uint32_t rate;         // samples per second
  uint32_t data_offset;  // bytes from the file start to the first sample
  uint32_t data_bytes;   // even, and inside the file (a header that claims more is cut)
};

// Reads up to `n` bytes at `offset`; returns how many it read.
using ReadFn = size_t (*)(void* ctx, uint32_t offset, uint8_t* buf, size_t n);

// Walks the RIFF chunks of a file of `file_size` bytes. Unknown chunks (LIST,
// fact, cue ...) are skipped; WAVE_FORMAT_EXTENSIBLE with the PCM subformat
// counts as PCM.
WavError parse_wav(ReadFn read, void* ctx, uint32_t file_size, Wav& out);

// Seconds of sound in a parsed file, rounded down.
uint32_t seconds(const Wav& w);

// Loop position inside the data chunk. Fills `want` bytes as up to two reads
// (the tail, then the start again), so the loop has no gap. Plain data.
struct Loop {
  uint32_t pos;  // bytes into the data chunk, even
};
struct Span {
  uint32_t offset;  // file offset
  uint32_t bytes;
};
// Next span to read for `want` more bytes (want is rounded down to whole
// samples), and moves the position on, wrapping at the end of the data. Call
// again for the rest after a wrap. bytes == 0 only when want < 2.
Span loop_next(Loop& loop, const Wav& w, uint32_t want);

// Volume, on audio::beep's 0-100 % scale (-40..0 dBFS, 0 = silent), as a Q15
// gain: 32768 = 0 dB.
uint32_t gain_q15(uint8_t pct);
// Scales samples in place by a Q15 gain, saturating.
void apply_gain(int16_t* s, size_t n, uint32_t gain);

}  // namespace inkwake::sound
