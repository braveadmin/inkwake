// SPDX-License-Identifier: GPL-3.0-only
// Alarm scheduling on the UTC time line (#2): when does an alarm next fire,
// which alarm is due on this wake, plus the NVS storage codec and the text
// forms used by the serial commands and the setup page. Hardware-free.
//
// Clock-change policy (ADR-0017):
// - Skipped hour (spring forward): an alarm inside it fires at the jump, so
//   02:30 on a CET->CEST night rings at 03:00 CEST.
// - Repeated hour (fall back): fires once, at the first (daylight) instance.
// - Missed alarms (time not set, board off, late wake) are skipped, never
//   rung late: an alarm is due only within kMaxLateS of its fire time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "alarm.h"
#include "tz.h"

namespace inkwake::sched {

constexpr size_t kMaxAlarms = 16;
constexpr size_t kMaxLabel = 23;  // bytes (UTF-8)
constexpr size_t kMaxSound = 15;
constexpr int64_t kMaxLateS = 120;
constexpr size_t kBlobFixed = 9;  // bytes per alarm in encode() besides label and sound

// First instant strictly after `after_utc` at which `a` fires. nullopt when
// disabled.
std::optional<int64_t> next_fire(const Alarm& a, const tz::Zone& zone, int64_t after_utc);

struct Due {
  size_t index;
  int64_t fire_utc;
};

// One ring skipped with BOOT on the face (#55): alarm `id` stays on, but does
// not ring at `fire_utc`. Plain data for RTC memory; id 0 = no skip (alarm ids
// are 1-255).
struct Skip {
  uint8_t id;
  int64_t fire_utc;
};

// Alarm whose fire time lies in (prev_utc, now_utc] and is no more than
// kMaxLateS before now_utc. The earliest wins, ties go to the lowest index.
// The ring in `skip` is left out.
std::optional<Due> due(const std::vector<Alarm>& alarms, const tz::Zone& zone, int64_t prev_utc,
                       int64_t now_utc, const Skip& skip = {});

struct Upcoming {
  size_t index;
  int64_t fire_utc;
};
// Soonest enabled alarm after now_utc.
std::optional<Upcoming> upcoming(const std::vector<Alarm>& alarms, const tz::Zone& zone,
                                 int64_t now_utc);

enum class SkipState : uint8_t {
  None,     // no skip set
  Pending,  // the skipped ring is still ahead
  Passed,   // its time has come: clear the skip (a one-shot alarm turns off, as if it rang)
  Stale,    // the alarm was deleted, turned off or moved: clear the skip
};
SkipState skip_state(const std::vector<Alarm>& alarms, const tz::Zone& zone, const Skip& skip,
                     int64_t now_utc);

// BOOT on the face: skips the next ring of the soonest alarm, or, if that ring
// is already skipped, undoes it. Returns the new skip ({} = none).
Skip toggle_skip(const std::vector<Alarm>& alarms, const tz::Zone& zone, const Skip& skip,
                 int64_t now_utc);

// Lowest id 1..255 not used yet, 0 when none is free.
uint8_t free_id(const std::vector<Alarm>& alarms);
// Index of the alarm with this id.
std::optional<size_t> find(const std::vector<Alarm>& alarms, uint8_t id);

// Validation for a new or edited alarm (range, lengths). nullptr when ok,
// otherwise a short English reason.
const char* check(const Alarm& a);

// NVS blob: "AL", version 2, count, then per alarm id, flags (bit0 enabled,
// bit1 ramp), hour, minute, days, sunrise_minutes, volume, label length +
// bytes, sound length + bytes. Version 1 (before #43, no volume byte, ramp
// bit unused) still decodes, with the default volume and ramp.
std::vector<uint8_t> encode(const std::vector<Alarm>& alarms);
// False on a bad header, version, length or field; `out` is then untouched.
bool decode(const uint8_t* data, size_t len, std::vector<Alarm>& out);

// "07:30" / "7:30" -> hour, minute.
bool parse_time(const char* s, uint8_t& hour, uint8_t& minute);
// "daily", "weekdays", "weekends", "once", or a comma list of days and ranges
// ("mon,wed,fri", "mon-fri", "sat-sun", "fri-mon" wraps). Case-insensitive.
bool parse_days(const char* s, uint8_t& days);
// Inverse of parse_days: the shortest of the names above, ranges as "mon-thu".
std::string format_days(uint8_t days);

}  // namespace inkwake::sched
