#include "Palette.h"

#include <Adafruit_NeoPixel.h>

// Global 256-entry gradient palette.
static Rgb paletteTable[256];

// The LED strip. A single NeoPixel controller drives the whole strip; the
// GPIO is fixed at 2 and the colour order is GRB, matching the original
// FastLED setup (WS2812, pin 2, GRB).
static Adafruit_NeoPixel strip(MAX_LEDS, 2, NEO_GRB + NEO_KHZ800);

Rgb leds[MAX_LEDS];
uint16_t ledCount = 0;

// How many trailing pixels of the controller's buffer are known to be black.
// The buffer is always MAX_LEDS long (the controller is constructed once),
// so the pixels past ledCount are pushed to the wire every frame and must
// be kept black.
static uint16_t pixelsClearedTo = 0;

// Linear interpolation between two 8-bit values with a 1/256 fraction.
// Matches FastLED's lerp8by8() exactly: a + ((b - a) * frac) >> 8.
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t frac) {
  if (b > a) {
    return (uint8_t)(a + (((uint16_t)(b - a) * frac) >> 8));
  }
  return (uint8_t)(a - (((uint16_t)(a - b) * frac) >> 8));
}

Rgb buildPalette(const PaletteStop stops[PALETTE_STOPS]) {
  Rgb table[256];
  for (int i = 0; i < 256; ++i) {
    const uint8_t p = (uint8_t)i;

    // Find the bracketing pair of stops (stops are sorted ascending by pos).
    int hi = 0;
    while (hi < PALETTE_STOPS - 1 && stops[hi + 1].pos <= p) ++hi;
    const int lo = (hi == 0) ? 0 : hi - 1;

    const int span = (int)stops[hi].pos - (int)stops[lo].pos;
    // frac is in 1/256ths: 0 = fully at lo, 255 = fully at hi.
    const uint8_t frac = (span == 0) ? 0
        : (uint8_t)(((int)p - (int)stops[lo].pos) * 255 / span);

    // Interpolate exactly the way FastLED's lerp8by8() did:
    //   result = a + ((b - a) * frac) >> 8
    // (the fraction is in 1/256ths, so frac=255 falls just short of the top
    // stop). Using the same arithmetic keeps the rendered colours
    // bit-identical to the FastLED version.
    table[i].r = lerp8(stops[lo].r, stops[hi].r, frac);
    table[i].g = lerp8(stops[lo].g, stops[hi].g, frac);
    table[i].b = lerp8(stops[lo].b, stops[hi].b, frac);
  }
  for (int i = 0; i < 256; ++i) {
    paletteTable[i] = table[i];
  }
  return table[0];
}

Rgb paletteAt(uint8_t index) {
  return paletteTable[index];
}

void applyPalette(const PaletteStop stops[PALETTE_STOPS]) {
  buildPalette(stops);
  showLeds();
}

void showLeds() {
  if (ledCount == 0) {
    return;
  }
  for (uint16_t i = 0; i < ledCount; ++i) {
    strip.setPixelColor(i, strip.Color(leds[i].r, leds[i].g, leds[i].b));
  }
  // Pixels at index >= ledCount are never driven, but the controller's
  // buffer still holds whatever was last written there. They are pushed to
  // the wire on every show(), so keep them black: a shrinking strip must
  // not leave stale colours behind. setLedCount() clears them once when
  // the count shrinks, so this only has to run for the pixels that were
  // just added.
  for (uint16_t i = ledCount; i < pixelsClearedTo; ++i) {
    strip.setPixelColor(i, 0);
  }
  pixelsClearedTo = ledCount;
  strip.show();
}

void setLedCount(uint16_t count) {
  if (count > MAX_LEDS) {
    count = MAX_LEDS;
  }
  if (count == 0) {
    count = 1;
  }
  if (ledCount != 0 && count != ledCount) {
    // Blank the whole old range first so shrinking the count cannot leave
    // stale lit pixels beyond the new end of the strip.
    strip.clear();
    strip.show();
    // Everything is black now, so nothing needs clearing in showLeds().
    pixelsClearedTo = 0;
  }
  ledCount = count;
}

void setLedBrightness(uint8_t brightness) {
  strip.setBrightness(brightness);
}
