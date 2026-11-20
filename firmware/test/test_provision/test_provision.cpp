// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstring>

#include "provision.h"
#include "qrcodegen.h"

using namespace inkwake::provision;

void setUp() {}
void tearDown() {}

namespace {

void test_ap_name() {
  const uint8_t mac[6] = {0x24, 0x0a, 0xc4, 0x12, 0xa1, 0x0b};
  char buf[16];
  ap_name(mac, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("inkwake-A10B", buf);
}

void test_qr_text() {
  char buf[96];
  TEST_ASSERT_TRUE(wifi_qr_text("inkwake-A10B", buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:inkwake-A10B;;", buf);
  TEST_ASSERT_TRUE(wifi_qr_text("a;b,c:d\\e\"f", buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:a\\;b\\,c\\:d\\\\e\\\"f;;", buf);
  // Exactly fits: 30 characters + NUL.
  TEST_ASSERT_TRUE(wifi_qr_text("inkwake-A10B", buf, 31));
  TEST_ASSERT_FALSE(wifi_qr_text("inkwake-A10B", buf, 30));
}

void test_qr_fits_version_2() {
  char text[64];
  TEST_ASSERT_TRUE(wifi_qr_text("inkwake-A10B", text, sizeof text));
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
  TEST_ASSERT_TRUE(
      qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO, true));
  // 25 modules: the setup screen draws it at 7 px per module.
  TEST_ASSERT_EQUAL_INT(25, qrcodegen_getSize(qr));
}

void test_ssid_valid() {
  TEST_ASSERT_TRUE(ssid_valid("Home"));
  TEST_ASSERT_TRUE(ssid_valid("My network 5G"));
  TEST_ASSERT_TRUE(ssid_valid("Café"));
  TEST_ASSERT_TRUE(ssid_valid("12345678901234567890123456789012"));
  TEST_ASSERT_FALSE(ssid_valid(""));
  TEST_ASSERT_FALSE(ssid_valid("123456789012345678901234567890123"));
  TEST_ASSERT_FALSE(ssid_valid("tab\there"));
}

void test_pass_valid() {
  TEST_ASSERT_TRUE(pass_valid(""));
  TEST_ASSERT_TRUE(pass_valid("12345678"));
  TEST_ASSERT_TRUE(pass_valid("pass word with spaces!"));
  TEST_ASSERT_TRUE(pass_valid("123456789012345678901234567890123456789012345678901234567890123"));
  TEST_ASSERT_FALSE(pass_valid("1234567"));
  TEST_ASSERT_FALSE(pass_valid("1234567890123456789012345678901234567890123456789012345678901234"));
  TEST_ASSERT_FALSE(pass_valid("contraseña"));
}

void test_form_value() {
  char out[40];
  const char* body =
      "ssid=My+Home%21&pass=a%26b%3Dc&tz=CET-1CEST%2CM3.5.0%2CM10.5.0%2F3&empty=&flag";
  TEST_ASSERT_TRUE(form_value(body, "ssid", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("My Home!", out);
  TEST_ASSERT_TRUE(form_value(body, "pass", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("a&b=c", out);
  TEST_ASSERT_TRUE(form_value(body, "tz", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", out);
  TEST_ASSERT_TRUE(form_value(body, "empty", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_TRUE(form_value(body, "flag", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(form_value(body, "ss", out, sizeof out));
  TEST_ASSERT_FALSE(form_value(body, "missing", out, sizeof out));
  TEST_ASSERT_FALSE(form_value("x=%4", "x", out, sizeof out));
  TEST_ASSERT_FALSE(form_value("x=%zz", "x", out, sizeof out));
  TEST_ASSERT_FALSE(form_value("x=%00", "x", out, sizeof out));
  TEST_ASSERT_TRUE(form_value("x=abc", "x", out, 4));
  TEST_ASSERT_FALSE(form_value("x=abcd", "x", out, 4));
  TEST_ASSERT_TRUE(form_value("x=caf%C3%A9", "x", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("café", out);
}

void test_json_escape() {
  char out[32];
  TEST_ASSERT_TRUE(json_escape("a\"b\\c\nd", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\u000ad", out);
  TEST_ASSERT_TRUE(json_escape("Café", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("Café", out);
  // "abé" is 4 bytes; with room for 3 the é is dropped whole.
  TEST_ASSERT_FALSE(json_escape("abé", out, 4));
  TEST_ASSERT_EQUAL_STRING("ab", out);
  TEST_ASSERT_FALSE(json_escape("abcd", out, 3));
  TEST_ASSERT_EQUAL_STRING("ab", out);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ap_name);
  RUN_TEST(test_qr_text);
  RUN_TEST(test_qr_fits_version_2);
  RUN_TEST(test_ssid_valid);
  RUN_TEST(test_pass_valid);
  RUN_TEST(test_form_value);
  RUN_TEST(test_json_escape);
  return UNITY_END();
}
