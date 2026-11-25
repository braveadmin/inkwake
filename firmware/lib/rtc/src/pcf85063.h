// SPDX-License-Identifier: GPL-3.0-only
// PCF85063 real-time clock: register map and the hardware-free conversion
// between its BCD time registers and Unix time. The I2C transport lives in
// firmware/src/rtc.*. Datasheet: NXP PCF85063A rev. 7 (register names as there).
//
// The chip counts years 00-99 and applies the every-4-years leap rule, which is
// right for 2000-2099, so that is the range this code accepts. The RTC keeps
// UTC; the timezone is applied when displaying (#13).
#pragma once

#include <cstddef>
#include <cstdint>

namespace inkwake::pcf85063 {

constexpr uint8_t kAddress = 0x51;  // 7-bit I2C address

constexpr uint8_t kRegControl1 = 0x00;
constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegOffset = 0x02;
constexpr uint8_t kRegSeconds = 0x04;  // seconds..years: 7 registers in a row
constexpr size_t kTimeRegs = 7;

// Control_1: STOP freezes the clock and clears the sub-second prescaler, so
// releasing it starts a fresh second. CAP_SEL picks the crystal load (board
// setting, preserved). 12_24 = 0 keeps 24-hour mode.
constexpr uint8_t kControl1Stop = 0x20;
constexpr uint8_t kControl1CapSel = 0x01;

// Control_2 written each time before sleeping: MI (minute interrupt) on, AF and
// TF cleared, alarm interrupt off, CLKOUT off (COF = 111) to save current.
// With Timer_mode TI_TP = 0 (reset default) INT stays low from the minute tick
// until TF is cleared by this write, so a level-triggered wake cannot miss it.
constexpr uint8_t kControl2MinuteInterrupt = 0x27;

// Seconds register bit 7: oscillator stopped since it was last cleared (power
// loss). Writing the time clears it.
constexpr uint8_t kSecondsOscStopped = 0x80;

constexpr int64_t kMinUnix = 946684800;   // 2000-01-01 00:00:00 UTC
constexpr int64_t kMaxUnix = 4102444799;  // 2099-12-31 23:59:59 UTC

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
int64_t days_from_civil(int year, unsigned month, unsigned day);
void civil_from_days(int64_t days, int& year, unsigned& month, unsigned& day);

// Registers seconds..years as read from kRegSeconds. False if the oscillator
// stopped (time lost) or any field is not a valid BCD date in 2000-2099.
bool decode(const uint8_t regs[kTimeRegs], int64_t& unix_seconds);

// Fills seconds..years (OS flag cleared, weekday 0 = Sunday). False if the time
// is outside 2000-2099; regs is then left untouched.
bool encode(int64_t unix_seconds, uint8_t regs[kTimeRegs]);

// Offset register (drift trim, #13). MODE = 0: one correction every 2 h, 4.34
// ppm per step, the lowest-current mode. OFFSET[6:0] is two's complement,
// -64..63. Sign as in Linux's rtc-pcf85063 driver and its sysfs ABI ("a
// positive value makes a day pass more slowly"): a clock that runs fast needs
// a positive value.
constexpr int kOffsetMin = -64, kOffsetMax = 63;
constexpr double kOffsetStepPpm = 4.34;

// Register value for `steps` in mode 0 (clamped to the range).
uint8_t encode_offset(int steps);
// Steps from a register value; mode 1 values (bit 7) are converted to mode-0 steps.
int decode_offset(uint8_t reg);

// Drift trim from two NTP syncs. `rtc_error_us`: how far the RTC was ahead of
// NTP time at the second sync (minus the correction the sync applied),
// `elapsed_s`: time since the first sync, which set the RTC exactly, with
// `current` steps in force all along. Writes the new step value to `next` and
// returns true if it should change. False (no change) when the span is below
// kTrimMinSpanS (too short to measure a 4 ppm step against NTP jitter), when
// the error implies more than kTrimMaxPpm (a jump, not drift), or when the
// remaining drift is under kTrimDeadband steps (avoids hunting between steps).
constexpr int64_t kTrimMinSpanS = 12 * 3600;
constexpr double kTrimMaxPpm = 200.0;
constexpr double kTrimDeadband = 0.75;
bool trim_offset(int current, int64_t rtc_error_us, int64_t elapsed_s, int& next);

}  // namespace inkwake::pcf85063
