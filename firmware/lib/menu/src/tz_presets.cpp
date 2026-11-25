// SPDX-License-Identifier: GPL-3.0-only
#include "tz_presets.h"

#include "tz.h"

namespace inkwake::menu {

const TzPreset kTzPresets[kTzPresetCount] = {
    {"Madrid / Paris / Berlin", kDefaultTz},
    {"London / Lisbon", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Athens / Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Tokyo", "<+09>-9"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC0"},
};

namespace {

bool same_rule(const tz::Rule& a, const tz::Rule& b) {
  return a.kind == b.kind && a.day == b.day && a.month == b.month && a.week == b.week &&
         a.time == b.time;
}

bool same_zone(const tz::Zone& a, const tz::Zone& b) {
  if (a.std_offset != b.std_offset || a.has_dst != b.has_dst) return false;
  if (!a.has_dst) return true;
  return a.dst_offset == b.dst_offset && same_rule(a.start, b.start) && same_rule(a.end, b.end);
}

}  // namespace

int find_tz_preset(const char* posix) {
  tz::Zone z;
  if (posix == nullptr || !tz::parse(posix, z)) return -1;
  for (size_t i = 0; i < kTzPresetCount; ++i) {
    tz::Zone p;
    if (tz::parse(kTzPresets[i].posix, p) && same_zone(z, p)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace inkwake::menu
