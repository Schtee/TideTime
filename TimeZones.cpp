#include "TimeZones.h"

// Common timezones.
//
// Daylight saving rules:
//   EU  - starts last Sunday in March at 01:00 UTC, ends last Sunday in October
//         at 01:00 UTC (the table below uses each zone's local clock hour)
//   US  - starts 2nd Sunday in March 02:00 local, ends 1st Sunday in November 02:00 local
//   AU  - starts 1st Sunday in October 02:00 local, ends 1st Sunday in April 03:00 local
//         (southern hemisphere: daylight spans the new year)
//   NZ  - starts last Sunday in September 02:00 local, ends 1st Sunday in April 03:00 local
//   CL  - starts 1st Sunday in September 00:00 local, ends 1st Sunday in April 00:00 local
//
// Offsets are absolute minutes from UTC.
//
// Week indices follow the NTP library's week_t enum: Last=0, First=1, Second=2,
// Third=3, Fourth=4. (An earlier version of this table used the opposite
// convention, which made most zones switch on the wrong Sunday.)
//
// Note on the "hour" fields: NTP::ruleDST()/ruleSTD() take the local hour at
// which the rule changes, so for zones whose offset differs from UTC the
// transition instant differs. The NTP library converts using the offsets it
// holds, so the values below are the local clock hours as commonly quoted.

// clang-format off
static const TimeZoneEntry kZones[] = {
  // name                       std   dst    stdOff dstOff hasDst dstM dstW dstH stdM stdW stdH
  { "United Kingdom",           "GMT",  "BST",     0,    60,   1,   2,  0,  1,   9,  0,  2 },
  { "Ireland",                  "GMT",  "IST",     0,    60,   1,   2,  0,  1,   9,  0,  2 },
  { "Portugal",                 "WET",  "WEST",    0,    60,   1,   2,  0,  1,   9,  0,  2 },
  { "Spain",                    "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "France",                   "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Germany",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Italy",                    "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Netherlands",              "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Belgium",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Austria",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Switzerland",              "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Sweden",                   "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Norway",                   "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Denmark",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Poland",                   "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Czechia",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Hungary",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Slovakia",                 "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Slovenia",                 "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Croatia",                  "CET",  "CEST",   60,   120,   1,   2,  0,  2,   9,  0,  3 },
  { "Romania",                  "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Finland",                  "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Greece",                   "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Estonia",                  "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Latvia",                   "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Lithuania",                "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Ukraine",                  "EET",  "EEST",  120,   180,   1,   2,  0,  3,   9,  0,  4 },
  { "Turkey",                   "TRT",  "TRT",   180,   180,   0,   0,  0,  0,   0,  0,  0 },
  { "Russia (Moscow)",          "MSK",  "MSK",   180,   180,   0,   0,  0,  0,   0,  0,  0 },
  { "Israel",                   "IST",  "IDT",   120,   180,   1,   2,  0,  2,   9,  0,  2 },
  { "South Africa",             "SAST", "SAST",  120,   120,   0,   0,  0,  0,   0,  0,  0 },
  { "Morocco",                  "WET",  "WEST",   60,    60,   0,   0,  0,  0,   0,  0,  0 },
  { "US Eastern",               "EST",  "EDT",  -300,  -240,   1,   2,  2,  2,  10,  1,  2 },
  { "US Central",               "CST",  "CDT",  -360,  -300,   1,   2,  2,  2,  10,  1,  2 },
  { "US Mountain",              "MST",  "MDT",  -420,  -360,   1,   2,  2,  2,  10,  1,  2 },
  { "US Pacific",               "PST",  "PDT",  -480,  -420,   1,   2,  2,  2,  10,  1,  2 },
  { "US Alaska",                "AKST", "AKDT", -540,  -480,   1,   2,  2,  2,  10,  1,  2 },
  { "US Hawaii",                "HST",  "HST",  -600,  -600,   0,   0,  0,  0,   0,  0,  0 },
  { "Canada (Toronto)",         "EST",  "EDT",  -300,  -240,   1,   2,  2,  2,  10,  1,  2 },
  { "Canada (Vancouver)",       "PST",  "PDT",  -480,  -420,   1,   2,  2,  2,  10,  1,  2 },
  { "Mexico (Mexico City)",     "CST",  "CDT",  -360,  -300,   0,   0,  0,  0,   0,  0,  0 },
  { "Brazil (Sao Paulo)",       "BRT",  "BRT",  -180,  -180,   0,   0,  0,  0,   0,  0,  0 },
  { "Argentina (Buenos Aires)",  "ART",  "ART",  -180,  -180,   0,   0,  0,  0,   0,  0,  0 },
  { "Chile (Santiago)",         "CLT",  "CLST", -240,  -180,   1,   8,  1,  0,   3,  1,  0 },
  { "Colombia (Bogota)",         "COT",  "COT",  -300,  -300,   0,   0,  0,  0,   0,  0,  0 },
  { "Peru (Lima)",              "PET",  "PET",  -300,  -300,   0,   0,  0,  0,   0,  0,  0 },
  { "India",                    "IST",  "IST",   330,   330,   0,   0,  0,  0,   0,  0,  0 },
  { "Pakistan",                 "PKT",  "PKT",   300,   300,   0,   0,  0,  0,   0,  0,  0 },
  { "Bangladesh",               "BDT",  "BDT",   360,   360,   0,   0,  0,  0,   0,  0,  0 },
  { "Thailand",                 "ICT",  "ICT",   420,   420,   0,   0,  0,  0,   0,  0,  0 },
  { "Vietnam",                  "ICT",  "ICT",   420,   420,   0,   0,  0,  0,   0,  0,  0 },
  { "Singapore",                "SGT",  "SGT",   480,   480,   0,   0,  0,  0,   0,  0,  0 },
  { "China",                    "CST",  "CST",   480,   480,   0,   0,  0,  0,   0,  0,  0 },
  { "Hong Kong",                "HKT",  "HKT",   480,   480,   0,   0,  0,  0,   0,  0,  0 },
  { "Japan",                    "JST",  "JST",   540,   540,   0,   0,  0,  0,   0,  0,  0 },
  { "Korea",                    "KST",  "KST",   540,   540,   0,   0,  0,  0,   0,  0,  0 },
  { "Australia (Sydney)",       "AEST", "AEDT",  600,   660,   1,   9,  1,  2,   3,  1,  3 },
  { "Australia (Brisbane)",     "AEST", "AEST",  600,   600,   0,   0,  0,  0,   0,  0,  0 },
  { "Australia (Adelaide)",     "ACST", "ACDT",  570,   630,   1,   9,  1,  2,   3,  1,  3 },
  { "Australia (Perth)",        "AWST", "AWST",  480,   480,   0,   0,  0,  0,   0,  0,  0 },
  { "New Zealand",              "NZST", "NZDT",  720,   780,   1,   8,  0,  2,   3,  1,  3 },
  { "UTC",                      "UTC",  "UTC",     0,     0,   0,   0,  0,  0,   0,  0,  0 },
};
// clang-format on

uint8_t timeZoneCount() {
  return (uint8_t)(sizeof(kZones) / sizeof(kZones[0]));
}

const TimeZoneEntry* timeZoneAt(uint8_t index) {
  if (index >= timeZoneCount()) {
    return nullptr;
  }
  return &kZones[index];
}

int timeZoneMatch(const char* stdName, const char* dstName, int16_t stdOffset,
                  int16_t dstOffset, uint8_t hasDst, uint8_t dstMonth,
                  uint8_t dstWeek, uint8_t dstHour, uint8_t stdMonth,
                  uint8_t stdWeek, uint8_t stdHour) {
  const uint8_t n = timeZoneCount();
  for (uint8_t i = 0; i < n; ++i) {
    const TimeZoneEntry& z = kZones[i];
    if (z.stdOffset != stdOffset || z.dstOffset != dstOffset ||
        z.hasDst != hasDst || z.dstMonth != dstMonth || z.dstWeek != dstWeek ||
        z.dstHour != dstHour || z.stdMonth != stdMonth || z.stdWeek != stdWeek ||
        z.stdHour != stdHour) {
      continue;
    }
    if (stdName && strcmp(stdName, z.stdName) != 0) {
      continue;
    }
    if (dstName && strcmp(dstName, z.dstName) != 0) {
      continue;
    }
    return (int)i;
  }
  return -1;
}

uint8_t timeZoneDefaultIndex() {
  // "United Kingdom" is the first entry and matches the original sketch's
  // hardcoded BST/GMT rules.
  for (uint8_t i = 0; i < timeZoneCount(); ++i) {
    if (strcmp(kZones[i].name, "United Kingdom") == 0) {
      return i;
    }
  }
  return 0;
}
