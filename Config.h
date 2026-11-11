#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define PALETTE_STOPS 4
#define MAX_LEDS 300
#define NVS_NAMESPACE "tidetime"

struct PaletteStop {
  uint8_t pos;   // 0..255
  uint8_t r, g, b;
};

struct Config {
  String ssid;
  String psk;
  String stationId;

  uint16_t numLeds;
  uint8_t brightness;

  PaletteStop palette[PALETTE_STOPS];

  bool    tzHasDst;
  String  tzStdName;    // <= 5 chars, e.g. "GMT"
  String  tzDstName;    // <= 5 chars, e.g. "BST"
  int16_t tzStdOffset;  // minutes from UTC (can be negative)
  int16_t tzDstOffset;  // minutes from UTC (can be negative)
  uint8_t tzDstMonth;   // 0..11 (Jan=0)
  uint8_t tzDstWeek;    // 0..4 (Last=0)
  uint8_t tzDstHour;    // 0..23
  uint8_t tzStdMonth;   // 0..11
  uint8_t tzStdWeek;    // 0..4
  uint8_t tzStdHour;    // 0..23

  void load();   // read from NVS, apply defaults + clamps for missing/out-of-range values
  void save();   // write all fields to NVS and flush
  bool hasWifi() const { return ssid.length() > 0; }
};

extern Config config;   // single global instance, defined in Config.cpp
