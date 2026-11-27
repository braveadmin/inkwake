// SPDX-License-Identifier: GPL-3.0-only
#include "portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "provision.h"
#include "sound.h"
#include "sound_files.h"
#include "tz.h"
#include "web_page.h"

namespace inkwake::portal {

namespace {

constexpr uint32_t kConnectTimeoutMs = 15'000;
constexpr uint32_t kLingerMs = 6'000;  // after success, so the page can read the result
constexpr size_t kMaxNets = 20;
constexpr size_t kBodyMax = 512;
constexpr size_t kAlarmsJsonMax = 6144;  // 16 alarms with escaped labels and sounds
constexpr size_t kTzLen = 64;
constexpr size_t kSsidLen = provision::kSsidMax + 1;
constexpr size_t kPassLen = provision::kPassMax + 1;
const IPAddress kApIp(192, 168, 4, 1);

enum class State : uint8_t { Idle, Connecting, Connected, Failed };

struct Net {
  char ssid[kSsidLen];
  int8_t rssi;
  bool open;
};

// Shared between the web server task and run(). Guarded by `mu`.
struct Shared {
  std::mutex mu;
  // Shown to the page.
  char ap[16];
  char ssid[kSsidLen];        // the network being tried
  provision::WifiList saved;  // saved networks; passwords never leave the clock
  char tz[kTzLen];
  bool tz_saved;
  State state;
  Net nets[kMaxNets];
  size_t net_count;
  bool scanning;
  // Queued for run().
  bool want_scan;
  bool want_tz;
  char new_tz[kTzLen];
  time_t offered_time;  // 0 = none
  bool want_connect;
  char try_ssid[kSsidLen];
  char try_pass[kPassLen];
  bool want_save;  // `saved` was edited from the page
  bool want_close;
  // Alarms (#2).
  char alarms_json[kAlarmsJsonMax];
  char alarm_error[64];
  uint32_t alarm_rev;  // bumped after each applied edit
  bool want_alarm;
  char alarm_form[kBodyMax];
};

Shared g;
std::atomic<uint32_t> g_last_activity{0};

void touch() { g_last_activity = millis(); }

const char* state_name(State s) {
  switch (s) {
    case State::Connecting:
      return "connecting";
    case State::Connected:
      return "connected";
    case State::Failed:
      return "failed";
    default:
      return "idle";
  }
}

esp_err_t send_json(httpd_req_t* req, const char* status, const char* body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, body);
}

esp_err_t send_error(httpd_req_t* req, const char* status, const char* msg) {
  char esc[96], buf[128];
  provision::json_escape(msg, esc, sizeof esc);
  snprintf(buf, sizeof buf, "{\"error\":\"%s\"}", esc);
  return send_json(req, status, buf);
}

// Reads a form body into buf. False (and an error sent) if it is too long or broken.
bool read_body(httpd_req_t* req, char* buf, size_t n) {
  if (req->content_len >= n) {
    send_error(req, "413 Payload Too Large", "request too long");
    return false;
  }
  size_t got = 0;
  while (got < req->content_len) {
    const int r = httpd_req_recv(req, buf + got, req->content_len - got);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      send_error(req, "400 Bad Request", "request cut short");
      return false;
    }
    got += static_cast<size_t>(r);
  }
  buf[got] = '\0';
  return true;
}

esp_err_t handle_index(httpd_req_t* req) {
  touch();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char*>(web::kIndexGz), web::kIndexGzLen);
}

// ["name",...] of the saved networks, in try order. Caller holds g.mu.
// Returns the length written; `n` must hold kMaxNetworks escaped names.
size_t saved_json(char* buf, size_t n) {
  size_t o = static_cast<size_t>(snprintf(buf, n, "["));
  for (size_t i = 0; i < g.saved.count; ++i) {
    char esc[kSsidLen * 6];
    provision::json_escape(g.saved.nets[i].ssid, esc, sizeof esc);
    o += static_cast<size_t>(snprintf(buf + o, n - o, "%s\"%s\"", i ? "," : "", esc));
  }
  o += static_cast<size_t>(snprintf(buf + o, n - o, "]"));
  return o;
}

constexpr size_t kSavedJsonMax = provision::kMaxNetworks * (kSsidLen * 6 + 3) + 3;

esp_err_t handle_get_settings(httpd_req_t* req) {
  touch();
  char nets[kSavedJsonMax], tz[kTzLen * 6], ap[32], buf[kSavedJsonMax + 640];
  {
    std::lock_guard<std::mutex> lock(g.mu);
    saved_json(nets, sizeof nets);
    provision::json_escape(g.tz, tz, sizeof tz);
    provision::json_escape(g.ap, ap, sizeof ap);
    snprintf(buf, sizeof buf,
             "{\"networks\":%s,\"max\":%u,\"tz\":\"%s\",\"tz_saved\":%s,\"ap\":\"%s\"}", nets,
             static_cast<unsigned>(provision::kMaxNetworks), tz, g.tz_saved ? "true" : "false", ap);
  }
  return send_json(req, "200 OK", buf);
}

// Remove a saved network or move it up one place. Saved by run().
esp_err_t handle_networks(httpd_req_t* req) {
  touch();
  char body[kBodyMax];
  if (!read_body(req, body, sizeof body)) return ESP_OK;
  char op[8], ssid[kSsidLen];
  if (!provision::form_value(body, "op", op, sizeof op) ||
      !provision::form_value(body, "ssid", ssid, sizeof ssid)) {
    return send_error(req, "400 Bad Request", "op and ssid needed");
  }
  char nets[kSavedJsonMax], buf[kSavedJsonMax + 32];
  {
    std::lock_guard<std::mutex> lock(g.mu);
    const int i = provision::find(g.saved, ssid);
    if (i < 0) return send_error(req, "404 Not Found", "no saved network by that name");
    const auto idx = static_cast<size_t>(i);
    if (strcmp(op, "remove") == 0) {
      provision::remove(g.saved, idx);
    } else if (strcmp(op, "up") == 0) {
      provision::move_up(g.saved, idx);  // already first: nothing to do
    } else {
      return send_error(req, "400 Bad Request", "op: remove or up");
    }
    g.want_save = true;
    saved_json(nets, sizeof nets);
  }
  snprintf(buf, sizeof buf, "{\"networks\":%s}", nets);
  return send_json(req, "200 OK", buf);
}

esp_err_t handle_close(httpd_req_t* req) {
  touch();
  {
    std::lock_guard<std::mutex> lock(g.mu);
    g.want_close = true;
  }
  return send_json(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handle_post_settings(httpd_req_t* req) {
  touch();
  char body[kBodyMax];
  if (!read_body(req, body, sizeof body)) return ESP_OK;
  char tz_str[kTzLen], now_str[24];
  const bool has_tz = provision::form_value(body, "tz", tz_str, sizeof tz_str);
  if (has_tz) {
    tz::Zone z;
    if (!tz::parse(tz_str, z)) return send_error(req, "400 Bad Request", "not a valid timezone");
  }
  time_t now = 0;
  if (provision::form_value(body, "now", now_str, sizeof now_str)) {
    now = static_cast<time_t>(strtoll(now_str, nullptr, 10));
  }
  {
    std::lock_guard<std::mutex> lock(g.mu);
    if (has_tz) {
      snprintf(g.new_tz, sizeof g.new_tz, "%s", tz_str);
      g.want_tz = true;
    }
    if (now > 0) g.offered_time = now;
  }
  return send_json(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handle_scan(httpd_req_t* req) {
  touch();
  char query[32] = {}, fresh[4] = {};
  const bool want_fresh = httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK &&
                          httpd_query_key_value(query, "fresh", fresh, sizeof fresh) == ESP_OK &&
                          fresh[0] == '1';
  // Up to 20 networks of 32 bytes, each escaped up to 6x, plus framing.
  static char buf[kMaxNets * (kSsidLen * 6 + 48) + 64];
  std::lock_guard<std::mutex> lock(g.mu);
  if (want_fresh) {
    g.want_scan = true;
    g.scanning = true;
  }
  size_t o = static_cast<size_t>(
      snprintf(buf, sizeof buf, "{\"scanning\":%s,\"networks\":[", g.scanning ? "true" : "false"));
  for (size_t i = 0; i < g.net_count && !g.scanning; ++i) {
    char esc[kSsidLen * 6];
    provision::json_escape(g.nets[i].ssid, esc, sizeof esc);
    o += static_cast<size_t>(snprintf(buf + o, sizeof buf - o,
                                      "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}", i ? "," : "",
                                      esc, g.nets[i].rssi, g.nets[i].open ? "true" : "false"));
  }
  snprintf(buf + o, sizeof buf - o, "]}");
  return send_json(req, "200 OK", buf);
}

esp_err_t handle_wifi(httpd_req_t* req) {
  touch();
  char body[kBodyMax];
  if (!read_body(req, body, sizeof body)) return ESP_OK;
  char ssid[kSsidLen], pass[kPassLen];
  if (!provision::form_value(body, "ssid", ssid, sizeof ssid) || !provision::ssid_valid(ssid)) {
    return send_error(req, "400 Bad Request", "network name: 1 to 32 characters");
  }
  if (!provision::form_value(body, "pass", pass, sizeof pass)) pass[0] = '\0';
  if (!provision::pass_valid(pass)) {
    return send_error(req, "400 Bad Request", "password: 8 to 63 characters, or empty");
  }
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.state == State::Connecting || g.want_connect) {
    return send_error(req, "409 Conflict", "already connecting");
  }
  snprintf(g.try_ssid, sizeof g.try_ssid, "%s", ssid);
  snprintf(g.try_pass, sizeof g.try_pass, "%s", pass);
  snprintf(g.ssid, sizeof g.ssid, "%s", ssid);
  g.want_connect = true;
  g.state = State::Connecting;
  return send_json(req, "202 Accepted", "{\"ok\":true}");
}

esp_err_t handle_status(httpd_req_t* req) {
  touch();
  char ssid[kSsidLen * 6], buf[256];
  {
    std::lock_guard<std::mutex> lock(g.mu);
    provision::json_escape(g.ssid, ssid, sizeof ssid);
    snprintf(buf, sizeof buf, "{\"state\":\"%s\",\"ssid\":\"%s\"}", state_name(g.state), ssid);
  }
  return send_json(req, "200 OK", buf);
}

// Captive portal: every other URL (phones' connectivity checks included) is
// sent to the page. Doesn't count as activity: phones probe on their own.
esp_err_t handle_get_alarms(httpd_req_t* req) {
  touch();
  std::string body;
  {
    std::lock_guard<std::mutex> lock(g.mu);
    char err[sizeof g.alarm_error * 6];
    provision::json_escape(g.alarm_error, err, sizeof err);
    char head[sizeof err + 48];
    snprintf(head, sizeof head,
             "{\"rev\":%lu,\"error\":\"%s\",\"alarms\":", static_cast<unsigned long>(g.alarm_rev),
             err);
    body = head;
    body += g.alarms_json[0] ? g.alarms_json : "[]";
    body += "}";
  }
  return send_json(req, "200 OK", body.c_str());
}

esp_err_t handle_post_alarms(httpd_req_t* req) {
  touch();
  char body[kBodyMax];
  if (!read_body(req, body, sizeof body)) return ESP_OK;
  uint32_t rev;
  {
    std::lock_guard<std::mutex> lock(g.mu);
    if (g.want_alarm) return send_error(req, "503 Service Unavailable", "busy, try again");
    memcpy(g.alarm_form, body, sizeof body);
    g.want_alarm = true;
    rev = g.alarm_rev;
  }
  char buf[48];
  snprintf(buf, sizeof buf, "{\"rev\":%lu}", static_cast<unsigned long>(rev));
  return send_json(req, "202 Accepted", buf);
}

// ---- Sound files (#44) ----
// These run on the server's task: an upload has to be streamed as it comes.
// sound_files keeps them apart from the main task's playing (see there).

esp_err_t handle_get_sounds(httpd_req_t* req) {
  touch();
  static sound_files::Info list[sound::kMaxFiles];
  static char buf[sound::kMaxFiles * 128 + 192];
  const size_t n = sound_files::list(list, sound::kMaxFiles);
  uint32_t used = 0, total = 0;
  const bool ready = sound_files::usage(used, total);
  size_t o = static_cast<size_t>(
      snprintf(buf, sizeof buf, "{\"ready\":%s,\"sounds\":[", ready ? "true" : "false"));
  for (size_t i = 0; i < n; ++i) {
    const sound_files::Info& f = list[i];
    const bool ok = f.err == sound::WavError::None;
    o += static_cast<size_t>(snprintf(
        buf + o, sizeof buf - o,
        "%s{\"name\":\"%s\",\"bytes\":%lu,\"seconds\":%lu,\"rate\":%lu,\"error\":\"%s\"}",
        i ? "," : "", f.name, static_cast<unsigned long>(f.bytes),
        static_cast<unsigned long>(ok ? sound::seconds(f.wav) : 0),
        static_cast<unsigned long>(ok ? f.wav.rate : 0), ok ? "" : sound::wav_error_text(f.err)));
  }
  snprintf(buf + o, sizeof buf - o, "],\"used\":%lu,\"total\":%lu,\"max_file\":%lu,\"max\":%u}",
           static_cast<unsigned long>(used), static_cast<unsigned long>(total),
           static_cast<unsigned long>(sound::kMaxFileBytes),
           static_cast<unsigned>(sound::kMaxFiles));
  return send_json(req, "200 OK", buf);
}

// form: op=del, name
esp_err_t handle_post_sounds(httpd_req_t* req) {
  touch();
  char body[kBodyMax];
  if (!read_body(req, body, sizeof body)) return ESP_OK;
  char op[8] = "", name[sound::kMaxName + 1] = "";
  provision::form_value(body, "op", op, sizeof op);
  provision::form_value(body, "name", name, sizeof name);
  if (strcmp(op, "del") != 0) return send_error(req, "400 Bad Request", "op: del");
  if (const char* why = sound_files::remove(name)) return send_error(req, "400 Bad Request", why);
  return send_json(req, "200 OK", "{\"ok\":true}");
}

// POST /api/sounds/upload?file=NAME.wav, body = the file itself. The sound
// name comes from the file name (sound::name_from_upload).
esp_err_t handle_upload(httpd_req_t* req) {
  touch();
  char query[160] = "", file[128] = "", name[sound::kMaxName + 1];
  httpd_req_get_url_query_str(req, query, sizeof query);
  provision::form_value(query, "file", file, sizeof file);
  if (!sound::name_from_upload(file, name, sizeof name)) {
    return send_error(req, "400 Bad Request", "file name: use letters and digits");
  }
  if (const char* why = sound_files::upload_begin(name, static_cast<uint32_t>(req->content_len))) {
    return send_error(req, "400 Bad Request", why);
  }
  static uint8_t chunk[4096];
  size_t got = 0;
  bool ok = true;
  int timeouts = 0;  // a phone that went away mid-upload: give up after ~15 s
  while (ok && got < req->content_len) {
    const size_t want = std::min(sizeof chunk, req->content_len - got);
    const int r = httpd_req_recv(req, reinterpret_cast<char*>(chunk), want);
    if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
    if (r <= 0) {
      ok = false;
      break;
    }
    ok = sound_files::upload_write(chunk, static_cast<size_t>(r));
    got += static_cast<size_t>(r);
    touch();
  }
  if (const char* why = sound_files::upload_end(ok)) return send_error(req, "400 Bad Request", why);
  char buf[64];
  snprintf(buf, sizeof buf, "{\"ok\":true,\"name\":\"%s\"}", name);
  return send_json(req, "200 OK", buf);
}

esp_err_t handle_redirect(httpd_req_t* req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, nullptr, 0);
}

httpd_handle_t start_server() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 16;
  cfg.lru_purge_enable = true;  // phones open many sockets; drop the oldest
  cfg.max_open_sockets = 7;
  cfg.stack_size = 8192;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) return nullptr;
  const httpd_uri_t uris[] = {
      {"/", HTTP_GET, handle_index, nullptr},
      {"/api/settings", HTTP_GET, handle_get_settings, nullptr},
      {"/api/settings", HTTP_POST, handle_post_settings, nullptr},
      {"/api/scan", HTTP_GET, handle_scan, nullptr},
      {"/api/wifi", HTTP_POST, handle_wifi, nullptr},
      {"/api/status", HTTP_GET, handle_status, nullptr},
      {"/api/networks", HTTP_POST, handle_networks, nullptr},
      {"/api/close", HTTP_POST, handle_close, nullptr},
      {"/api/alarms", HTTP_GET, handle_get_alarms, nullptr},
      {"/api/alarms", HTTP_POST, handle_post_alarms, nullptr},
      {"/api/sounds", HTTP_GET, handle_get_sounds, nullptr},
      {"/api/sounds", HTTP_POST, handle_post_sounds, nullptr},
      {"/api/sounds/upload", HTTP_POST, handle_upload, nullptr},
      {"/*", HTTP_GET, handle_redirect, nullptr},  // last: wildcard
  };
  for (const httpd_uri_t& u : uris) httpd_register_uri_handler(server, &u);
  return server;
}

// A scan started by start_scan() and not collected yet. Main task only. Without
// it, "no results" (WIFI_SCAN_FAILED, before a requested scan starts) read as
// a finished scan with no networks, and the page stopped waiting.
bool g_scan_active = false;

void start_scan() {
  g_scan_active = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
  if (!g_scan_active) {
    std::lock_guard<std::mutex> lock(g.mu);
    g.scanning = false;  // could not start: the page shows what it has
  }
}

// Copies finished scan results (strongest first, one entry per name) for the page.
void collect_scan() {
  if (!g_scan_active) return;
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  g_scan_active = false;
  std::lock_guard<std::mutex> lock(g.mu);
  g.net_count = 0;
  for (int i = 0; i < n && g.net_count < kMaxNets; ++i) {
    const String name = WiFi.SSID(i);
    if (name.isEmpty() || name.length() > provision::kSsidMax) continue;  // hidden networks
    bool dup = false;
    for (size_t j = 0; j < g.net_count && !dup; ++j) dup = name == g.nets[j].ssid;
    if (dup) continue;  // results come sorted by signal: the first copy is the strongest
    Net& net = g.nets[g.net_count++];
    snprintf(net.ssid, sizeof net.ssid, "%s", name.c_str());
    net.rssi = static_cast<int8_t>(WiFi.RSSI(i));
    net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  g.scanning = false;
}

}  // namespace

const char* outcome_name(Outcome o) {
  switch (o) {
    case Outcome::Connected:
      return "connected";
    case Outcome::Timeout:
      return "timed out";
    case Outcome::Cancelled:
      return "cancelled";
    case Outcome::Closed:
      return "closed";
  }
  return "?";
}

namespace {

// Saves the list if the page edited it. Main task only.
void save_if_edited(const Hooks& hooks) {
  static provision::WifiList copy;
  {
    std::lock_guard<std::mutex> lock(g.mu);
    if (!g.want_save) return;
    g.want_save = false;
    copy = g.saved;
  }
  hooks.save_networks(copy);  // a failed NVS write is rare; the edit is then lost at reboot
  memset(&copy, 0, sizeof copy);
}

}  // namespace

Outcome run(const Hooks& hooks, uint32_t idle_ms) {
  {
    std::lock_guard<std::mutex> lock(g.mu);
    g.state = State::Idle;
    g.ssid[0] = '\0';
    g.net_count = 0;
    g.scanning = true;
    g.want_scan = g.want_tz = g.want_connect = g.want_save = g.want_close = false;
    g.offered_time = 0;
    hooks.load(g.saved, g.tz, sizeof g.tz, g.tz_saved);
    g.want_alarm = false;
    g.alarm_error[0] = '\0';
    hooks.alarms_json(g.alarms_json, sizeof g.alarms_json);
  }
  WiFi.persistent(false);  // credentials live in our own NVS keys
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  provision::ap_name(mac, g.ap, sizeof g.ap);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(g.ap);  // no password: open network (ADR-0016)
  DNSServer dns;
  dns.start(53, "*", kApIp);
  start_scan();
  httpd_handle_t server = start_server();
  hooks.show(Screen::Setup, g.ap, nullptr);
  touch();

  Outcome outcome = Outcome::Timeout;
  for (;;) {
    dns.processNextRequest();
    collect_scan();
    save_if_edited(hooks);
    bool want_scan, want_tz, want_connect, want_close;
    char new_tz[kTzLen], ssid[kSsidLen], pass[kPassLen];
    time_t offered;
    {
      std::lock_guard<std::mutex> lock(g.mu);
      want_scan = g.want_scan;
      want_tz = g.want_tz;
      want_connect = g.want_connect;
      want_close = g.want_close;
      offered = g.offered_time;
      memcpy(new_tz, g.new_tz, sizeof new_tz);
      memcpy(ssid, g.try_ssid, sizeof ssid);
      memcpy(pass, g.try_pass, sizeof pass);
      g.want_scan = g.want_tz = g.want_connect = false;
      g.offered_time = 0;
    }
    if (want_scan && !g_scan_active) start_scan();
    if (want_tz && hooks.apply_tz(new_tz)) {
      std::lock_guard<std::mutex> lock(g.mu);
      snprintf(g.tz, sizeof g.tz, "%s", new_tz);
      g.tz_saved = true;
    }
    if (offered != 0) hooks.offer_time(offered);
    // Alarm edits: applied here on the caller's task (NVS, display), then the
    // list is rebuilt for the page.
    static char form[kBodyMax];
    bool want_alarm;
    {
      std::lock_guard<std::mutex> lock(g.mu);
      want_alarm = g.want_alarm;
      if (want_alarm) memcpy(form, g.alarm_form, sizeof form);
    }
    if (want_alarm) {
      const char* err = hooks.alarm_edit(form);
      static char json[kAlarmsJsonMax];
      hooks.alarms_json(json, sizeof json);
      std::lock_guard<std::mutex> lock(g.mu);
      memcpy(g.alarms_json, json, sizeof json);
      snprintf(g.alarm_error, sizeof g.alarm_error, "%s", err ? err : "");
      g.alarm_rev++;
      g.want_alarm = false;
    }
    if (want_connect) {
      hooks.show(Screen::Connecting, g.ap, ssid);
      WiFi.scanDelete();
      if (g_scan_active) {
        g_scan_active = false;
        std::lock_guard<std::mutex> lock(g.mu);
        g.scanning = false;
      }
      WiFi.begin(ssid, pass);
      const uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < kConnectTimeoutMs) {
        dns.processNextRequest();
        delay(20);
      }
      bool ok = WiFi.status() == WL_CONNECTED;
      if (ok) {
        // Goes first in the list; a full list forgets its last network.
        static provision::WifiList copy;
        {
          std::lock_guard<std::mutex> lock(g.mu);
          provision::remember(g.saved, ssid, pass);
          g.want_save = false;  // saved below with any earlier edits
          copy = g.saved;
        }
        ok = hooks.save_networks(copy);
        memset(&copy, 0, sizeof copy);
      }
      {
        std::lock_guard<std::mutex> lock(g.mu);
        g.state = ok ? State::Connected : State::Failed;
      }
      if (ok) {
        hooks.show(Screen::Connected, g.ap, ssid);
        const uint32_t t1 = millis();
        while (millis() - t1 < kLingerMs) {
          dns.processNextRequest();
          delay(20);
        }
        outcome = Outcome::Connected;
        break;
      }
      WiFi.disconnect(false);  // stop retrying; the access point stays up
      hooks.show(Screen::Failed, g.ap, ssid);
      touch();
    }
    if (want_close) {
      outcome = Outcome::Closed;
      break;
    }
    if (hooks.cancel()) {
      outcome = Outcome::Cancelled;
      break;
    }
    if (millis() - g_last_activity.load() >= idle_ms) break;
    delay(10);
  }

  if (server != nullptr) httpd_stop(server);
  save_if_edited(hooks);  // an edit that came in during the last loop
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  {
    std::lock_guard<std::mutex> lock(g.mu);
    memset(g.try_pass, 0, sizeof g.try_pass);
    memset(&g.saved, 0, sizeof g.saved);
  }
  return outcome;
}

}  // namespace inkwake::portal
