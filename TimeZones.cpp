#include "TimeZones.h"

// Common timezones.
//
// Daylight saving rules:
//   EU  - starts last Sunday in March 01:00, ends last Sunday in October 01:00
//   US  - starts 2nd Sunday in March 02:00 local, ends 1st Sunday in November 02:00 local
//   AU  - starts 1st Sunday in October 02:00 local, ends 1st Sunday in April 03:00 local
//         (southern hemisphere: daylight spans the new year)
//
// Offsets are absolute minutes from UTC.
//
// Note on the "hour" fields: NTP::ruleDST()/ruleSTD() take the local hour at
// which the rule changes, so for zones whose offset differs from UTC the
// transition instant differs. The NTP library converts using the offsets it
// holds, so the values below are the local clock hours as commonly quoted.

// clang-format off
static const TimeZoneEntry kZones[] = {
  // name                       std   dst    stdOff dstOff hasDst dstM dstW dstH stdM stdW stdH
  { "United Kingdom",           "GMT",  "BST",     0,    60,   1,   2,  4,  1,   9,  4,  2 },
  { "Ireland",                  "GMT",  "IST",     0,    60,   1,   2,  4,  1,   9,  4,  2 },
  { "Portugal",                 "WET",  "WEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Spain",                    "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "France",                   "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Germany",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Italy",                    "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Netherlands",              "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Belgium",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Austria",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Switzerland",              "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Sweden",                   "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Norway",                   "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Denmark",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Poland",                   "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Czechia",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Hungary",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Slovakia",                 "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Slovenia",                 "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Croatia",                  "CET",  "CEST",   60,   120,   1,   2,  4,  1,   9,  4,  2 },
  { "Romania",                  "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Finland",                  "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Greece",                   "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Estonia",                  "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Latvia",                   "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Lithuania",                "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Ukraine",                  "EET",  "EEST",  120,   180,   1,   2,  4,  3,   9,  4,  4 },
  { "Turkey",                   "TRT",  "TRT",   180,   180,   0,   0,  4,  0,   0,  4,  0 },
  { "Russia (Moscow)",          "MSK",  "MSK",   180,   180,   0,   0,  4,  0,   0,  4,  0 },
  { "Israel",                   "IST",  "IDT",   120,   180,   1,   2,  4,  2,   9,  4,  2 },
  { "South Africa",             "SAST", "SAST",  120,   120,   0,   0,  4,  0,   0,  4,  0 },
  { "Morocco",                  "WET",  "WEST",   60,    60,   0,   0,  4,  0,   0,  4,  0 },
  { "US Eastern",               "EST",  "EDT",  -300,  -240,   1,   2,  1,  2,  10,  0,  2 },
  { "US Central",               "CST",  "CDT",  -360,  -300,   1,   2,  1,  2,  10,  0,  2 },
  { "US Mountain",              "MST",  "MDT",  -420,  -360,   1,   2,  1,  2,  10,  0,  2 },
  { "US Pacific",               "PST",  "PDT",  -480,  -420,   1,   2,  1,  2,  10,  0,  2 },
  { "US Alaska",                "AKST", "AKDT", -540,  -480,   1,   2,  1,  2,  10,  0,  2 },
  { "US Hawaii",                "HST",  "HST",  -600,  -600,   0,   0,  4,  0,   0,  4,  0 },
  { "Canada (Toronto)",         "EST",  "EDT",  -300,  -240,   1,   2,  1,  2,  10,  0,  2 },
  { "Canada (Vancouver)",       "PST",  "PDT",  -480,  -420,   1,   2,  1,  2,  10,  0,  2 },
  { "Mexico (Mexico City)",     "CST",  "CDT",  -360,  -300,   0,   0,  4,  0,   0,  4,  0 },
  { "Brazil (Sao Paulo)",       "BRT",  "BRT",  -180,  -180,   0,   0,  4,  0,   0,  4,  0 },
  { "Argentina (Buenos Aires)",  "ART",  "ART",  -180,  -180,   0,   0,  4,  0,   0,  4,  0 },
  { "Chile (Santiago)",         "CLT",  "CLST", -240,  -180,   1,   9,  0,  4,   3,  0,  3 },
  { "Colombia (Bogota)",         "COT",  "COT",  -300,  -300,   0,   0,  4,  0,   0,  4,  0 },
  { "Peru (Lima)",              "PET",  "PET",  -300,  -300,   0,   0,  4,  0,   0,  4,  0 },
  { "India",                    "IST",  "IST",   330,   330,   0,   0,  4,  0,   0,  4,  0 },
  { "Pakistan",                 "PKT",  "PKT",   300,   300,   0,   0,  4,  0,   0,  4,  0 },
  { "Bangladesh",               "BDT",  "BDT",   360,   360,   0,   0,  4,  0,   0,  4,  0 },
  { "Thailand",                 "ICT",  "ICT",   420,   420,   0,   0,  4,  0,   0,  4,  0 },
  { "Vietnam",                  "ICT",  "ICT",   420,   420,   0,   0,  4,  0,   0,  4,  0 },
  { "Singapore",                "SGT",  "SGT",   480,   480,   0,   0,  4,  0,   0,  4,  0 },
  { "China",                    "CST",  "CST",   480,   480,   0,   0,  4,  0,   0,  4,  0 },
  { "Hong Kong",                "HKT",  "HKT",   480,   480,   0,   0,  4,  0,   0,  4,  0 },
  { "Japan",                    "JST",  "JST",   540,   540,   0,   0,  4,  0,   0,  4,  0 },
  { "Korea",                    "KST",  "KST",   540,   540,   0,   0,  4,  0,   0,  4,  0 },
  { "Australia (Sydney)",       "AEST", "AEDT",  600,   660,   1,   9,  0,  2,   3,  0,  3 },
  { "Australia (Brisbane)",     "AEST", "AEST",  600,   600,   0,   0,  4,  0,   0,  4,  0 },
  { "Australia (Adelaide)",     "ACST", "ACDT",  570,   630,   1,   9,  0,  2,   3,  0,  3 },
  { "Australia (Perth)",        "AWST", "AWST",  480,   480,   0,   0,  4,  0,   0,  4,  0 },
  { "New Zealand",              "NZST", "NZDT",  720,   780,   1,   9,  0,  2,   3,  0,  3 },
  { "UTC",                      "UTC",  "UTC",     0,     0,   0,   0,  4,  0,   0,  4,  0 },
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
