#include "Palette.h"

// Global 256-entry gradient palette.
CRGBPalette256 palette;

CRGBPalette256 buildPalette(const PaletteStop stops[PALETTE_STOPS]) {
  CRGBPalette256 pal;
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

    pal.entries[i] = CRGB(lerp8by8(stops[lo].r, stops[hi].r, frac),
                          lerp8by8(stops[lo].g, stops[hi].g, frac),
                          lerp8by8(stops[lo].b, stops[hi].b, frac));
  }
  return pal;
}

void applyPalette(const PaletteStop stops[PALETTE_STOPS]) {
  palette = buildPalette(stops);
  // Only refresh the strip if FastLED.addLeds() has been called;
  // count() returns the number of registered controllers (0 before that).
  if (FastLED.count() > 0) {
    FastLED.show();
  }
}
