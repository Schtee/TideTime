#pragma once
#include <Arduino.h>
#include "Config.h"

// A single RGB colour.
//
// This is a plain struct rather than FastLED's CRGB on purpose: FastLED is a
// large library and the Arduino build system compiles every .cpp file inside
// a library folder whether or not the sketch uses it. Switching to the much
// smaller Adafruit_NeoPixel library cuts a clean build from ~290 s to ~160 s
// (see README.md, "Build times").
struct Rgb {
  uint8_t r, g, b;
};

// Builds a 256-entry gradient palette from 4 stops, linearly interpolated.
// `stops` MUST already be sorted ascending by .pos.
Rgb buildPalette(const PaletteStop stops[PALETTE_STOPS]);

// Builds and assigns into the global palette, then refreshes the strip.
void applyPalette(const PaletteStop stops[PALETTE_STOPS]);

// Looks up a colour in the global 256-entry palette.
Rgb paletteAt(uint8_t index);

// The LED strip buffer. config.numLeds (1..MAX_LEDS) selects how many of
// these are actually driven.
extern Rgb leds[MAX_LEDS];

// Number of pixels currently driven.
extern uint16_t ledCount;

// Sets how many pixels are driven. Blank the strip first when the count
// shrinks, so stale pixels cannot linger past the new end of the strip.
void setLedCount(uint16_t count);

// Pushes the current leds[] contents to the strip.
void showLeds();

// Sets the global brightness (0..255).
void setLedBrightness(uint8_t brightness);
