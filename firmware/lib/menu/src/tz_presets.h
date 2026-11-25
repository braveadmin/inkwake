// SPDX-License-Identifier: GPL-3.0-only
// Timezone presets for the button menu (#41): POSIX TZ strings with their DST
// rules, so the clock changes to summer time by itself. The strings match the
// setup page's list (web/index.html), which has more zones.
#pragma once

#include <cstddef>

namespace inkwake::menu {

struct TzPreset {
  const char* name;   // shown in the menu
  const char* posix;  // saved to NVS `inkwake`/`tz`, as serial `tz`
};

constexpr char kDefaultTz[] = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe, preset 0

constexpr size_t kTzPresetCount = 11;
extern const TzPreset kTzPresets[kTzPresetCount];

// Preset with the same offsets and DST rules as `posix` (the zone names may
// differ: WET0WEST and GMT0BST both give London / Lisbon), or -1 if none
// matches or the string does not parse. The menu shows -1 as "Custom".
int find_tz_preset(const char* posix);

}  // namespace inkwake::menu
