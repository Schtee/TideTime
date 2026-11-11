#pragma once
#include <Arduino.h>

// Built-in timezone table.
//
// The ESP32 ships no tzdata, so IANA names like "Europe/London" cannot be
// resolved by the C library - the POSIX TZ variable needs either a POSIX
// string (e.g. "GMT0BST,M3.5.0/1,M10.5.0") or a zoneinfo file on the
// filesystem, and neither is available here.
//
// The sstaub/NTP library also ignores POSIX TZ strings entirely: it computes
// the UTC offset from its own ruleDST()/ruleSTD() parameters.
//
// So a "pick a timezone" control can only work by shipping a table that maps
// a friendly zone name to the four values the library actually understands:
//   - standard offset in minutes from UTC (absolute, not a delta)
//   - daylight offset in minutes from UTC (absolute)
//   - the month/week/hour that daylight time starts
//   - the month/week/hour that standard time resumes
//
// The offsets are absolute minutes from UTC because that is what
// NTP::ruleDST()/ruleSTD() expect - their `tzOffset` parameter is an absolute
// offset from UTC, not a difference from the standard offset.

// Month indices match the NTP library's month_t enum (Jan = 0).
// Week indices match its week_t enum (Last = 0, First = 1, Second = 2,
// Third = 3, Fourth = 4).
// Day of week is fixed to Sunday for every zone, matching the original
// sketch and the config schema (which has no day-of-week field).
//
// Almost every zone below follows the EU rule (last Sunday in March 01:00
// UTC / last Sunday in October 01:00 UTC) or the US rule (2nd Sunday in
// March 02:00 local / 1st Sunday in November 02:00 local), which is why only
// the differing values are listed.

struct TimeZoneEntry {
  const char* name;        // Display name, e.g. "United Kingdom"
  const char* stdName;     // Standard abbreviation, <= 5 chars (NTP library limit)
  const char* dstName;     // Daylight abbreviation, <= 5 chars
  int16_t stdOffset;       // minutes from UTC
  int16_t dstOffset;       // minutes from UTC
  uint8_t hasDst;
  uint8_t dstMonth;        // 0..11
  uint8_t dstWeek;         // 0..4
  uint8_t dstHour;         // 0..23
  uint8_t stdMonth;        // 0..11
  uint8_t stdWeek;         // 0..4
  uint8_t stdHour;         // 0..23
};

// Number of entries in the table.
uint8_t timeZoneCount();

// Entry at the given index, or nullptr if out of range.
const TimeZoneEntry* timeZoneAt(uint8_t index);

// Find the table entry that exactly matches the given settings.
// Returns the index, or -1 if the settings do not match any zone.
int timeZoneMatch(const char* stdName, const char* dstName, int16_t stdOffset,
                  int16_t dstOffset, uint8_t hasDst, uint8_t dstMonth,
                  uint8_t dstWeek, uint8_t dstHour, uint8_t stdMonth,
                  uint8_t stdWeek, uint8_t stdHour);

// Default zone (index of "United Kingdom") - used on first run.
uint8_t timeZoneDefaultIndex();
