// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstring>
#include <vector>

#include "sound.h"

using namespace inkwake::sound;

void setUp() {}
void tearDown() {}

// ---- names ----

void test_name_valid() {
  TEST_ASSERT_TRUE(name_valid("birds"));
  TEST_ASSERT_TRUE(name_valid("rain_2"));
  TEST_ASSERT_TRUE(name_valid("soft-piano"));
  TEST_ASSERT_TRUE(name_valid("abcdefghijklmno"));    // 15
  TEST_ASSERT_FALSE(name_valid("abcdefghijklmnop"));  // 16
  TEST_ASSERT_FALSE(name_valid(""));
  TEST_ASSERT_FALSE(name_valid(nullptr));
  TEST_ASSERT_FALSE(name_valid("default"));
  TEST_ASSERT_FALSE(name_valid("Birds"));
  TEST_ASSERT_FALSE(name_valid("bi rds"));
  TEST_ASSERT_FALSE(name_valid("../x"));
  TEST_ASSERT_FALSE(name_valid("a.wav"));
  TEST_ASSERT_TRUE(alarm_sound_valid("default"));
  TEST_ASSERT_TRUE(alarm_sound_valid("birds"));
  TEST_ASSERT_FALSE(alarm_sound_valid("Birds"));
  TEST_ASSERT_FALSE(alarm_sound_valid(nullptr));
}

void test_path_for() {
  char p[kMaxPath];
  TEST_ASSERT_TRUE(path_for("birds", p, sizeof p));
  TEST_ASSERT_EQUAL_STRING("/sounds/birds.wav", p);
  TEST_ASSERT_TRUE(path_for("abcdefghijklmno", p, sizeof p));  // longest fits kMaxPath
  TEST_ASSERT_EQUAL_STRING("/sounds/abcdefghijklmno.wav", p);
  TEST_ASSERT_FALSE(path_for("default", p, sizeof p));
  TEST_ASSERT_FALSE(path_for("../etc", p, sizeof p));
  char small[8];
  TEST_ASSERT_FALSE(path_for("birds", small, sizeof small));
}

void test_name_from_file() {
  char n[kMaxName + 1];
  TEST_ASSERT_TRUE(name_from_file("birds.wav", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("birds", n);
  TEST_ASSERT_TRUE(name_from_file("/sounds/rain.wav", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("rain", n);
  TEST_ASSERT_FALSE(name_from_file("birds.mp3", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_file("Birds.wav", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_file(".wav", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_file(".upload.tmp", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_file("default.wav", n, sizeof n));
}

void test_name_from_upload() {
  char n[kMaxName + 1];
  TEST_ASSERT_TRUE(name_from_upload("Morning Birds.WAV", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("morning-birds", n);
  TEST_ASSERT_TRUE(name_from_upload("C:\\Users\\me\\rain.wav", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("rain", n);
  TEST_ASSERT_TRUE(name_from_upload("  chimes (1).wav", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("chimes-1", n);
  TEST_ASSERT_TRUE(name_from_upload("a.very.long.sound.name.wav", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("a-very-long-sou", n);
  TEST_ASSERT_TRUE(name_from_upload("bowl", n, sizeof n));
  TEST_ASSERT_EQUAL_STRING("bowl", n);
  TEST_ASSERT_FALSE(name_from_upload("???.wav", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_upload("Default.wav", n, sizeof n));
  TEST_ASSERT_FALSE(name_from_upload("", n, sizeof n));
}

// ---- WAV parsing ----

struct Mem {
  std::vector<uint8_t> b;
};

size_t mem_read(void* ctx, uint32_t off, uint8_t* buf, size_t n) {
  const auto& b = static_cast<Mem*>(ctx)->b;
  if (off >= b.size()) return 0;
  const size_t m = b.size() - off < n ? b.size() - off : n;
  memcpy(buf, b.data() + off, m);
  return m;
}

void put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
  put16(b, static_cast<uint16_t>(v));
  put16(b, static_cast<uint16_t>(v >> 16));
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

struct WavSpec {
  uint16_t format = 1;
  uint16_t channels = 1;
  uint32_t rate = 22050;
  uint16_t bits = 16;
  uint32_t samples = 100;
  bool list_before_fmt = false;
  bool odd_chunk_before_data = false;
  bool extensible = false;
  uint16_t ext_sub = 1;
  bool fmt = true;
  bool data = true;
  int32_t data_size_delta = 0;  // header claims this many bytes more than present
};

Mem make_wav(const WavSpec& s) {
  std::vector<uint8_t> b;
  tag(b, "RIFF");
  put32(b, 0);  // patched below
  tag(b, "WAVE");
  if (s.list_before_fmt) {
    tag(b, "LIST");
    put32(b, 4);
    tag(b, "INFO");
  }
  if (s.fmt) {
    tag(b, "fmt ");
    put32(b, s.extensible ? 40 : 16);
    put16(b, s.extensible ? 0xFFFE : s.format);
    put16(b, s.channels);
    put32(b, s.rate);
    put32(b, s.rate * s.channels * s.bits / 8);
    put16(b, static_cast<uint16_t>(s.channels * s.bits / 8));
    put16(b, s.bits);
    if (s.extensible) {
      put16(b, 22);  // cbSize
      put16(b, s.bits);
      put32(b, 4);  // channel mask
      put16(b, s.ext_sub);
      for (int i = 0; i < 14; ++i) b.push_back(0);  // rest of the GUID
    }
  }
  if (s.odd_chunk_before_data) {
    tag(b, "junk");
    put32(b, 3);
    b.insert(b.end(), {1, 2, 3, 0});  // odd size + pad byte
  }
  if (s.data) {
    tag(b, "data");
    const uint32_t bytes = s.samples * 2;
    put32(b, static_cast<uint32_t>(static_cast<int64_t>(bytes) + s.data_size_delta));
    for (uint32_t i = 0; i < s.samples; ++i) put16(b, static_cast<uint16_t>(i));
  }
  const uint32_t riff = static_cast<uint32_t>(b.size() - 8);
  memcpy(b.data() + 4, &riff, 4);  // host is little-endian in CI
  return {b};
}

WavError parse(Mem m, Wav& w) {
  return parse_wav(mem_read, &m, static_cast<uint32_t>(m.b.size()), w);
}

void test_wav_plain() {
  Wav w{};
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav({}), w));
  TEST_ASSERT_EQUAL_UINT32(22050, w.rate);
  TEST_ASSERT_EQUAL_UINT32(44, w.data_offset);
  TEST_ASSERT_EQUAL_UINT32(200, w.data_bytes);
}

void test_wav_skips_other_chunks() {
  WavSpec s;
  s.list_before_fmt = true;
  s.odd_chunk_before_data = true;
  Wav w{};
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  TEST_ASSERT_EQUAL_UINT32(12 + 12 + 24 + 12 + 8, w.data_offset);
  TEST_ASSERT_EQUAL_UINT32(200, w.data_bytes);
}

void test_wav_extensible() {
  WavSpec s;
  s.extensible = true;
  Wav w{};
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  TEST_ASSERT_EQUAL_UINT32(12 + 48 + 8, w.data_offset);
  s.ext_sub = 3;  // float
  TEST_ASSERT_EQUAL(WavError::NotPcm, parse(make_wav(s), w));
}

void test_wav_refused() {
  Wav w{};
  WavSpec s;
  s.channels = 2;
  TEST_ASSERT_EQUAL(WavError::NotMono, parse(make_wav(s), w));
  s = {};
  s.bits = 8;
  TEST_ASSERT_EQUAL(WavError::Not16Bit, parse(make_wav(s), w));
  s = {};
  s.format = 85;  // MP3 in WAV
  TEST_ASSERT_EQUAL(WavError::NotPcm, parse(make_wav(s), w));
  s = {};
  s.rate = 8000;
  TEST_ASSERT_EQUAL(WavError::BadRate, parse(make_wav(s), w));
  s.rate = 96000;
  TEST_ASSERT_EQUAL(WavError::BadRate, parse(make_wav(s), w));
  s.rate = 16000;
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  s.rate = 48000;
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  s = {};
  s.fmt = false;
  TEST_ASSERT_EQUAL(WavError::NoFormat, parse(make_wav(s), w));
  s = {};
  s.data = false;
  TEST_ASSERT_EQUAL(WavError::NoData, parse(make_wav(s), w));
  s = {};
  s.samples = 0;
  TEST_ASSERT_EQUAL(WavError::Empty, parse(make_wav(s), w));

  Mem m = make_wav({});
  m.b[0] = 'X';
  TEST_ASSERT_EQUAL(WavError::NotRiff, parse(m, w));
  m.b.resize(10);
  TEST_ASSERT_EQUAL(WavError::TooShort, parse(m, w));
}

void test_wav_data_cut_to_file() {
  WavSpec s;
  s.data_size_delta = 1000;  // header claims more than the file has
  Wav w{};
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  TEST_ASSERT_EQUAL_UINT32(200, w.data_bytes);
  s.data_size_delta = -1;  // odd size: the last half sample is dropped
  TEST_ASSERT_EQUAL(WavError::None, parse(make_wav(s), w));
  TEST_ASSERT_EQUAL_UINT32(198, w.data_bytes);
}

void test_seconds() {
  TEST_ASSERT_EQUAL_UINT32(2, seconds({16000, 44, 64000}));
  TEST_ASSERT_EQUAL_UINT32(1, seconds({22050, 44, 22050 * 2 + 10}));
  TEST_ASSERT_EQUAL_UINT32(0, seconds({0, 44, 100}));
}

void test_max_file_bytes() {
  TEST_ASSERT_EQUAL_UINT32(kPartitionBytes - kReserveBytes, kMaxFileBytes);
  TEST_ASSERT_EQUAL_UINT32(kMaxFileBytes, max_file_bytes(kPartitionBytes));
  TEST_ASSERT_EQUAL_UINT32(kMaxFileBytes, max_file_bytes(kPartitionBytes * 2));
  TEST_ASSERT_EQUAL_UINT32(3'000'000 - kReserveBytes, max_file_bytes(3'000'000));
  TEST_ASSERT_EQUAL_UINT32(0, max_file_bytes(kReserveBytes));
  TEST_ASSERT_EQUAL_UINT32(0, max_file_bytes(0));
}

// ---- loop ----

void test_loop_wraps_without_gap() {
  const Wav w{16000, 44, 10};
  Loop l{0};
  Span s = loop_next(l, w, 6);
  TEST_ASSERT_EQUAL_UINT32(44, s.offset);
  TEST_ASSERT_EQUAL_UINT32(6, s.bytes);
  s = loop_next(l, w, 6);  // only 4 left: the tail first
  TEST_ASSERT_EQUAL_UINT32(50, s.offset);
  TEST_ASSERT_EQUAL_UINT32(4, s.bytes);
  TEST_ASSERT_EQUAL_UINT32(0, l.pos);
  s = loop_next(l, w, 2);  // then the start again
  TEST_ASSERT_EQUAL_UINT32(44, s.offset);
  TEST_ASSERT_EQUAL_UINT32(2, s.bytes);
}

void test_loop_odd_and_bad_input() {
  const Wav w{16000, 44, 10};
  Loop l{0};
  Span s = loop_next(l, w, 5);  // rounded down to whole samples
  TEST_ASSERT_EQUAL_UINT32(4, s.bytes);
  TEST_ASSERT_EQUAL_UINT32(0, loop_next(l, w, 1).bytes);
  l.pos = 99;  // out of range: restarts
  s = loop_next(l, w, 4);
  TEST_ASSERT_EQUAL_UINT32(44, s.offset);
  // A data chunk smaller than the request loops many times over.
  const Wav tiny{16000, 44, 2};
  Loop t{0};
  for (int i = 0; i < 5; ++i) {
    s = loop_next(t, tiny, 100);
    TEST_ASSERT_EQUAL_UINT32(44, s.offset);
    TEST_ASSERT_EQUAL_UINT32(2, s.bytes);
  }
}

// ---- gain ----

void test_gain() {
  TEST_ASSERT_EQUAL_UINT32(0, gain_q15(0));
  TEST_ASSERT_EQUAL_UINT32(32768, gain_q15(100));
  TEST_ASSERT_EQUAL_UINT32(32768, gain_q15(150));
  // 50 % = -20 dBFS = x0.1
  TEST_ASSERT_UINT32_WITHIN(2, 3277, gain_q15(50));
  TEST_ASSERT_TRUE(gain_q15(60) > gain_q15(59));

  int16_t s[] = {32767, -32768, 1000, -1000, 0};
  apply_gain(s, 5, 32768);
  TEST_ASSERT_EQUAL_INT16(32767, s[0]);
  TEST_ASSERT_EQUAL_INT16(-32768, s[1]);
  apply_gain(s, 5, 16384);
  TEST_ASSERT_EQUAL_INT16(16383, s[0]);
  TEST_ASSERT_EQUAL_INT16(-16384, s[1]);
  TEST_ASSERT_EQUAL_INT16(500, s[2]);
  TEST_ASSERT_EQUAL_INT16(-500, s[3]);
  TEST_ASSERT_EQUAL_INT16(0, s[4]);
  apply_gain(s, 5, 0);
  TEST_ASSERT_EQUAL_INT16(0, s[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_name_valid);
  RUN_TEST(test_path_for);
  RUN_TEST(test_name_from_file);
  RUN_TEST(test_name_from_upload);
  RUN_TEST(test_wav_plain);
  RUN_TEST(test_wav_skips_other_chunks);
  RUN_TEST(test_wav_extensible);
  RUN_TEST(test_wav_refused);
  RUN_TEST(test_wav_data_cut_to_file);
  RUN_TEST(test_seconds);
  RUN_TEST(test_max_file_bytes);
  RUN_TEST(test_loop_wraps_without_gap);
  RUN_TEST(test_loop_odd_and_bad_input);
  RUN_TEST(test_gain);
  return UNITY_END();
}
