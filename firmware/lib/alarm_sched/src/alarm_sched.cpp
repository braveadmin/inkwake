// SPDX-License-Identifier: GPL-3.0-only
#include "alarm_sched.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "sound.h"

namespace inkwake::sched {

namespace {

constexpr int64_t kDay = 86400;

int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// 1970-01-01 was a Thursday; Monday = 0.
uint8_t weekday_of_day(int64_t day) { return static_cast<uint8_t>(((day + 3) % 7 + 7) % 7); }

// UTC instant for local wall time `local` with the ADR-0017 skipped-hour rule.
int64_t wall_to_utc(const tz::Zone& zone, int64_t local) {
  const int64_t utc = tz::to_utc(zone, local);
  if (tz::local_seconds(zone, utc) == local) return utc;
  // Skipped: to_utc read it with the pre-jump offset, so the jump lies in
  // (utc - gap, utc]. Find it among this and the neighbouring years' transitions.
  const int year = tz::to_civil(zone, utc).year;
  for (int y = year - 1; y <= year + 1; ++y) {
    int64_t s = 0, e = 0;
    if (!tz::transitions(zone, y, s, e)) break;
    for (int64_t t : {s, e}) {
      if (t <= utc && utc - t < kDay) return t;
    }
  }
  return utc;
}

}  // namespace

std::optional<int64_t> next_fire(const Alarm& a, const tz::Zone& zone, int64_t after_utc) {
  if (!a.enabled) return std::nullopt;
  const int64_t today = floor_div(tz::local_seconds(zone, after_utc), kDay);
  const int64_t at = a.hour * 3600 + a.minute * 60;
  // Up to 8 days: "same weekday next week" plus room for a DST shift.
  for (int64_t d = 0; d <= 8; ++d) {
    const int64_t day = today + d;
    if (a.days != kOneShot && !(a.days & (1u << weekday_of_day(day)))) continue;
    const int64_t utc = wall_to_utc(zone, day * kDay + at);
    if (utc > after_utc) return utc;
  }
  return std::nullopt;
}

std::optional<Due> due(const std::vector<Alarm>& alarms, const tz::Zone& zone, int64_t prev_utc,
                       int64_t now_utc, const Skip& skip) {
  // Fire times before the late window can never be due; starting there also
  // keeps a long-ago prev_utc (first check after a gap) from finding only them.
  const int64_t from = std::max(prev_utc, now_utc - kMaxLateS - 1);
  std::optional<Due> best;
  for (size_t i = 0; i < alarms.size(); ++i) {
    const auto f = next_fire(alarms[i], zone, from);
    if (!f || *f > now_utc || now_utc - *f > kMaxLateS) continue;
    if (skip.id != 0 && alarms[i].id == skip.id && *f == skip.fire_utc) continue;
    if (!best || *f < best->fire_utc) best = Due{i, *f};
  }
  return best;
}

std::optional<Upcoming> upcoming(const std::vector<Alarm>& alarms, const tz::Zone& zone,
                                 int64_t now_utc) {
  std::optional<Upcoming> best;
  for (size_t i = 0; i < alarms.size(); ++i) {
    const auto f = next_fire(alarms[i], zone, now_utc);
    if (f && (!best || *f < best->fire_utc)) best = Upcoming{i, *f};
  }
  return best;
}

std::optional<Upcoming> lead_in(const std::vector<Alarm>& alarms, const tz::Zone& zone,
                                int64_t now_utc, const Skip& skip, const Skip& quiet) {
  std::optional<Upcoming> best;
  for (size_t i = 0; i < alarms.size(); ++i) {
    const Alarm& a = alarms[i];
    if (a.lead_minutes == 0) continue;
    const auto f = next_fire(a, zone, now_utc);
    if (!f || *f - now_utc > static_cast<int64_t>(a.lead_minutes) * 60) continue;
    if (a.id == skip.id && *f == skip.fire_utc) continue;
    if (a.id == quiet.id && *f == quiet.fire_utc) continue;
    if (!best || *f < best->fire_utc) best = Upcoming{i, *f};
  }
  return best;
}

SkipState skip_state(const std::vector<Alarm>& alarms, const tz::Zone& zone, const Skip& skip,
                     int64_t now_utc) {
  if (skip.id == 0) return SkipState::None;
  const auto i = find(alarms, skip.id);
  if (!i || !alarms[*i].enabled) return SkipState::Stale;
  if (skip.fire_utc <= now_utc) return SkipState::Passed;
  // Still ahead: valid only while it is still the alarm's next ring.
  const auto f = next_fire(alarms[*i], zone, now_utc);
  return f && *f == skip.fire_utc ? SkipState::Pending : SkipState::Stale;
}

Skip toggle_skip(const std::vector<Alarm>& alarms, const tz::Zone& zone, const Skip& skip,
                 int64_t now_utc) {
  const auto u = upcoming(alarms, zone, now_utc);
  if (!u) return {};
  const Skip next{alarms[u->index].id, u->fire_utc};
  if (skip.id == next.id && skip.fire_utc == next.fire_utc) return {};  // undo
  return next;
}

uint8_t free_id(const std::vector<Alarm>& alarms) {
  for (int id = 1; id <= 255; ++id) {
    if (!find(alarms, static_cast<uint8_t>(id))) return static_cast<uint8_t>(id);
  }
  return 0;
}

std::optional<size_t> find(const std::vector<Alarm>& alarms, uint8_t id) {
  for (size_t i = 0; i < alarms.size(); ++i) {
    if (alarms[i].id == id) return i;
  }
  return std::nullopt;
}

const char* check(const Alarm& a) {
  if (a.id == 0) return "bad id";
  if (a.hour > 23 || a.minute > 59) return "bad time";
  if (a.days & ~kEveryDay) return "bad days";
  if (a.label.size() > kMaxLabel) return "label too long";
  if (a.sound.size() > kMaxSound) return "sound name too long";
  if (!sound::alarm_sound_valid(a.sound.c_str())) return "sound: default or a-z, 0-9, - and _";
  if (a.sunrise_minutes > 60) return "sunrise over 60 min";
  if (a.volume < kMinVolume || a.volume > kMaxVolume) return "volume 1-10";
  if (!lead_valid(a.lead_minutes)) return "lead-in 0, 5, 10, 15 or 20 min";
  for (char ch : a.label) {
    if (static_cast<unsigned char>(ch) < 0x20) return "control character in label";
  }
  return nullptr;
}

std::vector<uint8_t> encode(const std::vector<Alarm>& alarms) {
  std::vector<uint8_t> b{'A', 'L', 3, static_cast<uint8_t>(alarms.size())};
  for (const Alarm& a : alarms) {
    const auto flags = static_cast<uint8_t>((a.enabled ? 1 : 0) | (a.ramp ? 2 : 0));
    b.insert(b.end(),
             {a.id, flags, a.hour, a.minute, a.days, a.sunrise_minutes, a.volume, a.lead_minutes});
    b.push_back(static_cast<uint8_t>(a.label.size()));
    b.insert(b.end(), a.label.begin(), a.label.end());
    b.push_back(static_cast<uint8_t>(a.sound.size()));
    b.insert(b.end(), a.sound.begin(), a.sound.end());
  }
  return b;
}

bool decode(const uint8_t* data, size_t len, std::vector<Alarm>& out) {
  if (len < 4 || data[0] != 'A' || data[1] != 'L') return false;
  const uint8_t version = data[2];
  if (version < 1 || version > 3) return false;
  // v1 (before #43) has no volume byte, v2 (before #4) no lead-in byte.
  const size_t fixed = version == 1 ? 6 : version == 2 ? 7 : 8;
  const size_t n = data[3];
  if (n > kMaxAlarms) return false;
  std::vector<Alarm> v;
  size_t p = 4;
  auto str = [&](std::string& s, size_t max) {
    if (p >= len) return false;
    const size_t l = data[p++];
    if (l > max || p + l > len) return false;
    s.assign(reinterpret_cast<const char*>(data + p), l);
    p += l;
    return true;
  };
  for (size_t i = 0; i < n; ++i) {
    if (p + fixed > len) return false;
    Alarm a;
    a.id = data[p];
    a.enabled = data[p + 1] & 1;
    a.hour = data[p + 2];
    a.minute = data[p + 3];
    a.days = data[p + 4];
    a.sunrise_minutes = data[p + 5];
    if (version >= 2) {  // v1 keeps the defaults: level 8, rising (50 % -> 90 %)
      a.ramp = data[p + 1] & 2;
      a.volume = data[p + 6];
    }
    if (version >= 3) a.lead_minutes = data[p + 7];  // older: no lead-in
    p += fixed;
    if (!str(a.label, kMaxLabel) || !str(a.sound, kMaxSound)) return false;
    if (a.sound.empty()) a.sound = sound::kDefault;  // nothing chose one: the beep
    if (check(a) || find(v, a.id)) return false;
    v.push_back(a);
  }
  if (p != len) return false;
  out = std::move(v);
  return true;
}

bool parse_time(const char* s, uint8_t& hour, uint8_t& minute) {
  int h = 0, m = 0, digits = 0;
  const char* p = s;
  while (std::isdigit(static_cast<unsigned char>(*p)) && digits < 2)
    h = h * 10 + (*p++ - '0'), ++digits;
  if (digits == 0 || *p++ != ':') return false;
  if (!std::isdigit(static_cast<unsigned char>(p[0])) ||
      !std::isdigit(static_cast<unsigned char>(p[1])) || p[2] != '\0')
    return false;
  m = (p[0] - '0') * 10 + (p[1] - '0');
  if (h > 23 || m > 59) return false;
  hour = static_cast<uint8_t>(h);
  minute = static_cast<uint8_t>(m);
  return true;
}

namespace {
const char* const kDayNames[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

int day_index(const std::string& s) {
  for (int i = 0; i < 7; ++i) {
    if (s == kDayNames[i]) return i;
  }
  return -1;
}
}  // namespace

bool parse_days(const char* s, uint8_t& days) {
  std::string t;
  for (const char* p = s; *p; ++p)
    t += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
  if (t == "daily") return days = kEveryDay, true;
  if (t == "weekdays") return days = kWeekdays, true;
  if (t == "weekends") return days = kWeekend, true;
  if (t == "once") return days = kOneShot, true;
  if (t.empty()) return false;
  uint8_t mask = 0;
  size_t pos = 0;
  while (pos <= t.size()) {
    size_t comma = t.find(',', pos);
    if (comma == std::string::npos) comma = t.size();
    const std::string item = t.substr(pos, comma - pos);
    const size_t dash = item.find('-');
    if (dash == std::string::npos) {
      const int d = day_index(item);
      if (d < 0) return false;
      mask |= 1u << d;
    } else {
      const int a = day_index(item.substr(0, dash));
      const int b = day_index(item.substr(dash + 1));
      if (a < 0 || b < 0) return false;
      for (int d = a;; d = (d + 1) % 7) {
        mask |= 1u << d;
        if (d == b) break;
      }
    }
    pos = comma + 1;
  }
  days = mask;
  return true;
}

std::string format_days(uint8_t days) {
  days &= kEveryDay;
  if (days == kOneShot) return "once";
  if (days == kEveryDay) return "daily";
  if (days == kWeekdays) return "weekdays";
  if (days == kWeekend) return "weekends";
  std::string out;
  for (int d = 0; d < 7;) {
    if (!(days & (1u << d))) {
      ++d;
      continue;
    }
    int e = d;
    while (e + 1 < 7 && (days & (1u << (e + 1)))) ++e;
    if (!out.empty()) out += ',';
    out += kDayNames[d];
    if (e - d >= 2) {
      out += '-';
      out += kDayNames[e];
    } else if (e == d + 1) {
      out += ',';
      out += kDayNames[e];
    }
    d = e + 1;
  }
  return out;
}

}  // namespace inkwake::sched
