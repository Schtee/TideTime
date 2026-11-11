#pragma once
#include <FastLED.h>
#include "Config.h"

// Builds a 256-entry palette from 4 stops, linearly interpolated.
// `stops` MUST already be sorted ascending by .pos.
CRGBPalette256 buildPalette(const PaletteStop stops[PALETTE_STOPS]);

// Builds and assigns into the global palette, then refreshes the strip.
void applyPalette(const PaletteStop stops[PALETTE_STOPS]);

extern CRGBPalette256 palette;   // global, defined in Palette.cpp
