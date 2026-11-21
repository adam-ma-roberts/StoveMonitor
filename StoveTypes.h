#pragma once
// Types used in function signatures live here so the Arduino IDE's
// auto-generated function prototypes can see them.
#include <stdint.h>

enum Level { L_UNKNOWN, L_COLD, L_GOOD, L_HIGH, L_HOT };

struct ProbeLimits {
  float cool;   // below this: BLUE
  float warn;   // cool..warn: GREEN, warn..hot: YELLOW
  float hot;    // at/above: RED
  float alarm;  // at/above: audible alarm
};

struct Config {
  uint32_t version;
  char ssid[33];
  char pass[65];
  char apName[33];
  char hostname[33];
  char tz[64];
  char ntp[64];
  float wakeTemp;
  ProbeLimits stove;
  ProbeLimits flue;
  float intWarn;
  float intAlarm;
  bool alarmEnabled;
  uint8_t alarmVolume;
  uint16_t snoozeMin;
  bool refuelEnabled;
  float refuelTemp;
  uint8_t refuelVolume;
  uint16_t refuelRepeatMin;
  uint16_t logInterval;
  bool ignoreShortFaults;
};

