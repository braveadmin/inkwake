// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstring>

#include "ntp.h"

using namespace inkwake::ntp;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint64_t kNonce = 0x0123456789ABCDEFull;
// 2026-11-20 12:00:00 UTC = Unix 1795176000 = NTP 4004164800.
constexpr int64_t kUnix = 1795176000;
constexpr uint64_t kNtpSecs = 4004164800ull;

void put64(uint64_t v, uint8_t* p) {
  for (int i = 7; i >= 0; i--) {
    p[i] = static_cast<uint8_t>(v);
    v >>= 8;
  }
}

// A valid server reply: LI 0, VN 4, mode 4, stratum 2.
void make_reply(uint8_t* b, uint64_t originate, uint64_t rx, uint64_t tx) {
  memset(b, 0, kPacketLen);
  b[0] = 0 << 6 | 4 << 3 | 4;
  b[1] = 2;
  put64(originate, b + 24);
  put64(rx, b + 32);
  put64(tx, b + 40);
}

}  // namespace

void test_request_layout() {
  uint8_t b[kPacketLen];
  memset(b, 0xAA, sizeof b);
  build_request(kNonce, b);
  TEST_ASSERT_EQUAL_HEX8(0x23, b[0]);  // LI 0, VN 4, mode 3
  for (size_t i = 1; i < 40; i++) TEST_ASSERT_EQUAL_HEX8(0, b[i]);
  const uint8_t nonce[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(nonce, b + 40, 8);
}

void test_ntp_to_unix() {
  TEST_ASSERT_EQUAL_INT64(kUnix * 1000000, ntp_to_unix_us(kNtpSecs << 32));
  // Half a second, and a fraction that has to round to the nearest microsecond.
  TEST_ASSERT_EQUAL_INT64(kUnix * 1000000 + 500000, ntp_to_unix_us(kNtpSecs << 32 | 0x80000000u));
  TEST_ASSERT_EQUAL_INT64(kUnix * 1000000 + 1, ntp_to_unix_us(kNtpSecs << 32 | 4295u));
  TEST_ASSERT_EQUAL_INT64(kUnix * 1000000 + 1000000,
                          ntp_to_unix_us(kNtpSecs << 32 | 0xFFFFFFFFu));  // rounds up
}

void test_era_rollover() {
  // 2036-02-07 06:28:15 UTC is the last second of era 0, 06:28:16 the first of era 1.
  TEST_ASSERT_EQUAL_INT64(int64_t{2085978495} * 1000000, ntp_to_unix_us(0xFFFFFFFFull << 32));
  TEST_ASSERT_EQUAL_INT64(int64_t{2085978496} * 1000000, ntp_to_unix_us(0));
  TEST_ASSERT_EQUAL_INT64(int64_t{2085978496 + 3600} * 1000000, ntp_to_unix_us(3600ull << 32));
}

void test_parse_ok() {
  uint8_t b[kPacketLen];
  make_reply(b, kNonce, kNtpSecs << 32, (kNtpSecs + 1) << 32);
  ServerTimes t{};
  TEST_ASSERT_EQUAL(Reply::Ok, parse_reply(b, sizeof b, kNonce, t));
  TEST_ASSERT_EQUAL_INT64(kUnix * 1000000, t.receive_us);
  TEST_ASSERT_EQUAL_INT64((kUnix + 1) * 1000000, t.transmit_us);
  // Longer packets (extension fields, MAC) are fine.
  uint8_t longer[68] = {};
  memcpy(longer, b, sizeof b);
  TEST_ASSERT_EQUAL(Reply::Ok, parse_reply(longer, sizeof longer, kNonce, t));
}

void test_parse_rejects() {
  uint8_t b[kPacketLen];
  ServerTimes t{};
  make_reply(b, kNonce, kNtpSecs << 32, kNtpSecs << 32);
  TEST_ASSERT_EQUAL(Reply::Short, parse_reply(b, kPacketLen - 1, kNonce, t));
  TEST_ASSERT_EQUAL(Reply::WrongNonce, parse_reply(b, sizeof b, kNonce + 1, t));

  b[0] = 0x23;  // mode 3: our own request echoed back
  TEST_ASSERT_EQUAL(Reply::NotServer, parse_reply(b, sizeof b, kNonce, t));

  make_reply(b, kNonce, kNtpSecs << 32, kNtpSecs << 32);
  b[0] |= 3 << 6;  // LI 3: server not synchronized
  TEST_ASSERT_EQUAL(Reply::Unsynced, parse_reply(b, sizeof b, kNonce, t));

  make_reply(b, kNonce, kNtpSecs << 32, kNtpSecs << 32);
  b[1] = 16;
  TEST_ASSERT_EQUAL(Reply::Unsynced, parse_reply(b, sizeof b, kNonce, t));

  make_reply(b, kNonce, kNtpSecs << 32, kNtpSecs << 32);
  b[1] = 0;  // kiss-of-death
  TEST_ASSERT_EQUAL(Reply::KissOfDeath, parse_reply(b, sizeof b, kNonce, t));

  make_reply(b, kNonce, 0, kNtpSecs << 32);
  TEST_ASSERT_EQUAL(Reply::NoTime, parse_reply(b, sizeof b, kNonce, t));
  make_reply(b, kNonce, kNtpSecs << 32, 0);
  TEST_ASSERT_EQUAL(Reply::NoTime, parse_reply(b, sizeof b, kNonce, t));
}

void test_offset_and_delay() {
  // Local clock 10 s behind, 40 ms each way, 2 ms in the server.
  const int64_t t1 = 1000000000, t4 = t1 + 82000;
  const ServerTimes s{t1 + 10000000 + 40000, t1 + 10000000 + 42000};
  TEST_ASSERT_EQUAL_INT64(10000000, clock_offset_us(t1, s, t4));
  TEST_ASSERT_EQUAL_INT64(80000, round_trip_us(t1, s, t4));
  // Local clock ahead: negative offset.
  const ServerTimes behind{t1 - 5000000 + 40000, t1 - 5000000 + 42000};
  TEST_ASSERT_EQUAL_INT64(-5000000, clock_offset_us(t1, behind, t4));
}

void test_retry_delays() {
  const int64_t six_h = 6 * 3600;
  // Time not set: 1, 2, 4 ... minutes, at most an hour.
  TEST_ASSERT_EQUAL_INT64(60, retry_delay_s(1, false, six_h));
  TEST_ASSERT_EQUAL_INT64(120, retry_delay_s(2, false, six_h));
  TEST_ASSERT_EQUAL_INT64(32 * 60, retry_delay_s(6, false, six_h));
  TEST_ASSERT_EQUAL_INT64(3600, retry_delay_s(7, false, six_h));
  TEST_ASSERT_EQUAL_INT64(3600, retry_delay_s(255, false, six_h));
  // Time set: 15, 30, 60 ... minutes, at most 6 h or the interval.
  TEST_ASSERT_EQUAL_INT64(15 * 60, retry_delay_s(1, true, six_h));
  TEST_ASSERT_EQUAL_INT64(30 * 60, retry_delay_s(2, true, six_h));
  TEST_ASSERT_EQUAL_INT64(4 * 3600, retry_delay_s(5, true, six_h));
  TEST_ASSERT_EQUAL_INT64(six_h, retry_delay_s(6, true, six_h));
  TEST_ASSERT_EQUAL_INT64(six_h, retry_delay_s(255, true, six_h));
  TEST_ASSERT_EQUAL_INT64(3600, retry_delay_s(9, true, 3600));  // short interval caps it
  // Daily interval: retries still capped at 6 h.
  TEST_ASSERT_EQUAL_INT64(4 * 3600, retry_delay_s(5, true, 24 * 3600));
  TEST_ASSERT_EQUAL_INT64(six_h, retry_delay_s(6, true, 24 * 3600));
  TEST_ASSERT_EQUAL_INT64(six_h, retry_delay_s(255, true, 24 * 3600));
}

void test_due_at_boot_and_interval() {
  const int64_t six_h = 6 * 3600;
  Schedule s{};
  TEST_ASSERT_TRUE(due(s, kUnix, false, six_h));  // power-on: at once
  record(s, kUnix, true);
  TEST_ASSERT_EQUAL_UINT8(0, s.fails);
  TEST_ASSERT_FALSE(due(s, kUnix + 60, true, six_h));
  TEST_ASSERT_FALSE(due(s, kUnix + six_h - 1, true, six_h));
  TEST_ASSERT_TRUE(due(s, kUnix + six_h, true, six_h));
  // The RTC lost its time after a good sync: sync again at once.
  TEST_ASSERT_TRUE(due(s, kUnix + 60, false, six_h));
}

void test_due_backoff() {
  const int64_t six_h = 6 * 3600;
  Schedule s{};
  record(s, kUnix, false);
  record(s, kUnix, false);  // 2 fails, time not set: next try after 2 min
  TEST_ASSERT_FALSE(due(s, kUnix + 119, false, six_h));
  TEST_ASSERT_TRUE(due(s, kUnix + 120, false, six_h));
  // Same failures with the time set (e.g. by `set`): 30 min.
  TEST_ASSERT_FALSE(due(s, kUnix + 29 * 60, true, six_h));
  TEST_ASSERT_TRUE(due(s, kUnix + 30 * 60, true, six_h));
  record(s, kUnix + 1800, true);
  TEST_ASSERT_EQUAL_UINT8(0, s.fails);
  TEST_ASSERT_EQUAL_INT64(kUnix + 1800, s.last_ok);
}

void test_due_after_clock_moved_back() {
  Schedule s{};
  record(s, kUnix, true);
  TEST_ASSERT_TRUE(due(s, kUnix - 3600, true, 6 * 3600));
}

void test_fails_saturate() {
  Schedule s{};
  for (int i = 0; i < 300; i++) record(s, kUnix, false);
  TEST_ASSERT_EQUAL_UINT8(255, s.fails);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_request_layout);
  RUN_TEST(test_ntp_to_unix);
  RUN_TEST(test_era_rollover);
  RUN_TEST(test_parse_ok);
  RUN_TEST(test_parse_rejects);
  RUN_TEST(test_offset_and_delay);
  RUN_TEST(test_retry_delays);
  RUN_TEST(test_due_at_boot_and_interval);
  RUN_TEST(test_due_backoff);
  RUN_TEST(test_due_after_clock_moved_back);
  RUN_TEST(test_fails_saturate);
  return UNITY_END();
}
