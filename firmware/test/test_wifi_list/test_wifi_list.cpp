// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>

#include "wifi_list.h"

using namespace inkwake::provision;

void setUp() {}
void tearDown() {}

namespace {

WifiList make(std::initializer_list<const char*> names) {
  WifiList l{};
  // remember() puts each at the top: add in reverse to get `names` in order.
  for (auto it = std::rbegin(names); it != std::rend(names); ++it) remember(l, *it, "password");
  return l;
}

void assert_order(const WifiList& l, std::initializer_list<const char*> names) {
  TEST_ASSERT_EQUAL_UINT8(names.size(), l.count);
  size_t i = 0;
  for (const char* n : names) TEST_ASSERT_EQUAL_STRING(n, l.nets[i++].ssid);
}

void test_remember_new_goes_first() {
  WifiList l{};
  TEST_ASSERT_TRUE(remember(l, "Home", "password1"));
  TEST_ASSERT_TRUE(remember(l, "Office", ""));
  assert_order(l, {"Office", "Home"});
  TEST_ASSERT_EQUAL_STRING("", l.nets[0].pass);
  TEST_ASSERT_EQUAL_STRING("password1", l.nets[1].pass);
}

void test_remember_known_updates_and_moves_up() {
  WifiList l = make({"A", "B", "C"});
  TEST_ASSERT_TRUE(remember(l, "C", "newpassword"));
  assert_order(l, {"C", "A", "B"});
  TEST_ASSERT_EQUAL_STRING("newpassword", l.nets[0].pass);
}

void test_remember_full_drops_last() {
  WifiList l = make({"A", "B", "C", "D", "E"});
  char dropped[kSsidMax + 1] = "x";
  TEST_ASSERT_TRUE(remember(l, "F", "password", dropped));
  assert_order(l, {"F", "A", "B", "C", "D"});
  TEST_ASSERT_EQUAL_STRING("E", dropped);
  // A known name never drops anything, even when full.
  TEST_ASSERT_TRUE(remember(l, "D", "password", dropped));
  assert_order(l, {"D", "F", "A", "B", "C"});
  TEST_ASSERT_EQUAL_STRING("", dropped);
}

void test_remember_rejects_invalid() {
  WifiList l = make({"A"});
  TEST_ASSERT_FALSE(remember(l, "", "password"));
  TEST_ASSERT_FALSE(remember(l, "B", "short"));
  TEST_ASSERT_FALSE(remember(l, "123456789012345678901234567890123", "password"));
  assert_order(l, {"A"});
}

void test_promote_move_remove() {
  WifiList l = make({"A", "B", "C", "D"});
  TEST_ASSERT_TRUE(promote(l, 2));
  assert_order(l, {"C", "A", "B", "D"});
  TEST_ASSERT_TRUE(promote(l, 0));
  assert_order(l, {"C", "A", "B", "D"});
  TEST_ASSERT_FALSE(promote(l, 4));

  TEST_ASSERT_TRUE(move_up(l, 3));
  assert_order(l, {"C", "A", "D", "B"});
  TEST_ASSERT_FALSE(move_up(l, 0));
  TEST_ASSERT_FALSE(move_up(l, 4));

  TEST_ASSERT_TRUE(remove(l, 1));
  assert_order(l, {"C", "D", "B"});
  TEST_ASSERT_EQUAL_STRING("", l.nets[3].ssid);
  TEST_ASSERT_EQUAL_STRING("", l.nets[3].pass);
  TEST_ASSERT_TRUE(remove(l, 2));
  assert_order(l, {"C", "D"});
  TEST_ASSERT_FALSE(remove(l, 2));
  TEST_ASSERT_EQUAL_INT(1, find(l, "D"));
  TEST_ASSERT_EQUAL_INT(-1, find(l, "B"));
}

void test_others_in_range() {
  const WifiList l = make({"Home", "Office", "Phone", "Cabin"});
  const char* seen[] = {"Neighbour", "Cabin", "Home", "Office"};
  uint8_t out[kMaxNetworks];
  // List order, not signal order; the first network is never included.
  TEST_ASSERT_EQUAL_size_t(2, others_in_range(l, seen, 4, out));
  TEST_ASSERT_EQUAL_UINT8(1, out[0]);
  TEST_ASSERT_EQUAL_UINT8(3, out[1]);
  TEST_ASSERT_EQUAL_size_t(0, others_in_range(l, seen, 0, out));
}

void test_encode_decode_round_trip() {
  WifiList l = make({"Home", "Café wifi", "x"});
  remember(l, "Open", "");
  uint8_t buf[kBlobMax];
  const size_t n = encode(l, buf, sizeof buf);
  TEST_ASSERT_EQUAL_size_t(2 + (2 + 4 + 0) + (2 + 4 + 8) + (2 + 10 + 8) + (2 + 1 + 8), n);
  WifiList back{};
  TEST_ASSERT_TRUE(decode(buf, n, back));
  TEST_ASSERT_EQUAL_MEMORY(&l, &back, sizeof l);

  WifiList empty{};
  TEST_ASSERT_EQUAL_size_t(2, encode(empty, buf, sizeof buf));
  TEST_ASSERT_TRUE(decode(buf, 2, back));
  TEST_ASSERT_EQUAL_UINT8(0, back.count);
}

void test_encode_full_list_fits_blob_max() {
  WifiList l{};
  char name[] = "12345678901234567890123456789010";
  const char pass[] = "123456789012345678901234567890123456789012345678901234567890123";
  for (size_t i = 0; i < kMaxNetworks; ++i) {
    name[31] = static_cast<char>('0' + i);
    TEST_ASSERT_TRUE(remember(l, name, pass));
  }
  uint8_t buf[kBlobMax];
  TEST_ASSERT_EQUAL_size_t(kBlobMax, encode(l, buf, sizeof buf));
  TEST_ASSERT_EQUAL_size_t(0, encode(l, buf, sizeof buf - 1));
}

void test_decode_rejects_bad_blobs() {
  WifiList l = make({"Home", "Office"});
  uint8_t buf[kBlobMax];
  const size_t n = encode(l, buf, sizeof buf);
  WifiList back = l;
  TEST_ASSERT_FALSE(decode(buf, n - 1, back));  // truncated
  TEST_ASSERT_EQUAL_UINT8(0, back.count);       // emptied on failure
  TEST_ASSERT_FALSE(decode(buf, 1, back));
  uint8_t extra[kBlobMax + 1];
  memcpy(extra, buf, n);
  extra[n] = 0;
  TEST_ASSERT_FALSE(decode(extra, n + 1, back));  // trailing bytes

  uint8_t bad[kBlobMax];
  memcpy(bad, buf, n);
  bad[0] = kBlobVersion + 1;
  TEST_ASSERT_FALSE(decode(bad, n, back));
  memcpy(bad, buf, n);
  bad[1] = kMaxNetworks + 1;
  TEST_ASSERT_FALSE(decode(bad, n, back));
  memcpy(bad, buf, n);
  bad[3] = '\t';  // control character in the first name
  TEST_ASSERT_FALSE(decode(bad, n, back));

  // The same name twice.
  WifiList dup = make({"Home", "Office"});
  snprintf(dup.nets[1].ssid, sizeof dup.nets[1].ssid, "Home");
  const size_t dn = encode(dup, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, dn, back));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_remember_new_goes_first);
  RUN_TEST(test_remember_known_updates_and_moves_up);
  RUN_TEST(test_remember_full_drops_last);
  RUN_TEST(test_remember_rejects_invalid);
  RUN_TEST(test_promote_move_remove);
  RUN_TEST(test_others_in_range);
  RUN_TEST(test_encode_decode_round_trip);
  RUN_TEST(test_encode_full_list_fits_blob_max);
  RUN_TEST(test_decode_rejects_bad_blobs);
  return UNITY_END();
}
