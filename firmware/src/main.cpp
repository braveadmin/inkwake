// SPDX-License-Identifier: GPL-3.0-only
// M1 skeleton: proves the toolchain and links alarm_core on the device.
// Real tasks (clock, alarm, display, net) arrive in M2-M5.
#include <Arduino.h>

#include "alarm.h"

using namespace inkwake;

static std::vector<Alarm> g_alarms;

void setup() {
  Serial.begin(115200);
  delay(500);

  Alarm workday;
  workday.id = 1;
  workday.label = "Workday";
  g_alarms.push_back(workday);

  Serial.printf("inkwake skeleton, %u alarm(s), PSRAM %u bytes\n",
                static_cast<unsigned>(g_alarms.size()), static_cast<unsigned>(ESP.getPsramSize()));
}

void loop() {
  // Placeholder clock until the RTC driver lands (M2): Monday 06:00.
  const LocalTime now{0, 6, 0};
  if (const auto next = next_alarm(g_alarms, now)) {
    Serial.printf("next: %s in %u min\n", next->alarm->label.c_str(),
                  static_cast<unsigned>(next->in_minutes));
  }
  delay(60 * 1000);
}
