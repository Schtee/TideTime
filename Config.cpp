#include "Config.h"

// Single global instance.
Config config;

// One Preferences object for the whole sketch. Only one namespace may be open
// at a time, and begin() on an already-open namespace returns false, so
// load() and save() each begin() and end() around their work. end() is what
// flushes NVS to flash.
static Preferences prefs;

static bool openPrefs() {
  if (prefs.begin(NVS_NAMESPACE, false)) {
    return true;
  }
  // Namespace may already be open (or the handle is stale) - close and retry once.
  prefs.end();
  return prefs.begin(NVS_NAMESPACE, false);
}

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void truncateTo5(String& s) {
  if (s.length() > 5) {
    s = s.substring(0, 5);
  }
}

void Config::load() {
  if (!openPrefs()) {
    Serial.println("[Config] ERROR: could not open NVS namespace '" NVS_NAMESPACE "', using compiled-in defaults");
    ssid = "";
    psk = "";
    stationId = "0546";
    numLeds = 48;
    brightness = 255;
    const PaletteStop defaultStops[PALETTE_STOPS] = {
      {   0,  31, 255, 160 },  // OCEAN
      {  85,   0, 200, 255 },  // CYAN
      { 170, 255, 220,  80 },  // YELLOW
      { 255, 255, 100,   0 }   // SAND
    };
    for (int i = 0; i < PALETTE_STOPS; ++i) {
      palette[i] = defaultStops[i];
    }
    tzHasDst = true;
    tzStdName = "GMT";
    tzDstName = "BST";
    tzStdOffset = 0;
    tzDstOffset = 60;
    tzDstMonth = 2;   // Mar
    tzDstWeek = 0;    // Last (week_t: Last=0)
    tzDstHour = 1;
    tzStdMonth = 9;   // Oct
    tzStdWeek = 0;    // Last (week_t: Last=0)
    tzStdHour = 2;
    return;
  }

  const bool firstRun = !prefs.isKey("cfgVersion");
  if (firstRun) {
    Serial.println("[Config] First run (no cfgVersion in NVS): applying defaults");
  }

  // Two-argument getX(key, default) form: missing keys fall back to defaults
  // without per-field isKey() checks.
  ssid      = prefs.getString("ssid", "");
  psk       = prefs.getString("psk", "");
  stationId = prefs.getString("stationId", "0546");

  numLeds    = prefs.getUShort("numLeds", 48);
  brightness = prefs.getUChar("brightness", 255);

  const PaletteStop defaultStops[PALETTE_STOPS] = {
    {   0,  31, 255, 160 },  // OCEAN
    {  85,   0, 200, 255 },  // CYAN
    { 170, 255, 220,  80 },  // YELLOW
    { 255, 255, 100,   0 }   // SAND
  };
  for (int i = 0; i < PALETTE_STOPS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "pal%dr", i);
    palette[i].r = prefs.getUChar(key, defaultStops[i].r);
    snprintf(key, sizeof(key), "pal%dg", i);
    palette[i].g = prefs.getUChar(key, defaultStops[i].g);
    snprintf(key, sizeof(key), "pal%db", i);
    palette[i].b = prefs.getUChar(key, defaultStops[i].b);
    snprintf(key, sizeof(key), "palPos%d", i);
    palette[i].pos = prefs.getUChar(key, defaultStops[i].pos);
  }

  tzHasDst     = prefs.getBool("tzHasDst", true);
  tzStdName    = prefs.getString("tzStdName", "GMT");
  tzDstName    = prefs.getString("tzDstName", "BST");
  tzStdOffset  = prefs.getShort("tzStdOffset", 0);
  tzDstOffset  = prefs.getShort("tzDstOffset", 60);
  tzDstMonth   = prefs.getUChar("tzDstMonth", 2);
  tzDstWeek    = prefs.getUChar("tzDstWeek", 0);
  tzDstHour    = prefs.getUChar("tzDstHour", 1);
  tzStdMonth   = prefs.getUChar("tzStdMonth", 9);
  tzStdWeek    = prefs.getUChar("tzStdWeek", 0);
  tzStdHour    = prefs.getUChar("tzStdHour", 2);

  // Flush and release the namespace.
  prefs.end();

  // ---- Clamps: a corrupt NVS must never produce out-of-range values. ----
  numLeds = clampInt(numLeds, 1, MAX_LEDS);
  // brightness is uint8_t, always 0..255 by construction.

  tzStdOffset = clampInt(tzStdOffset, -720, 840);
  tzDstOffset = clampInt(tzDstOffset, -720, 840);

  tzDstMonth = clampInt(tzDstMonth, 0, 11);
  tzStdMonth = clampInt(tzStdMonth, 0, 11);
  tzDstWeek  = clampInt(tzDstWeek, 0, 4);
  tzStdWeek  = clampInt(tzStdWeek, 0, 4);
  tzDstHour  = clampInt(tzDstHour, 0, 23);
  tzStdHour  = clampInt(tzStdHour, 0, 23);
  // Palette pos/r/g/b are uint8_t, always 0..255 by construction.

  // The NTP library stores tz names in char[6]: must be <= 5 characters.
  truncateTo5(tzStdName);
  truncateTo5(tzDstName);
}

void Config::save() {
  if (!openPrefs()) {
    Serial.println("[Config] ERROR: could not open NVS namespace '" NVS_NAMESPACE "' for writing");
    return;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("psk", psk);
  prefs.putString("stationId", stationId);

  prefs.putUShort("numLeds", numLeds);
  prefs.putUChar("brightness", brightness);

  for (int i = 0; i < PALETTE_STOPS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "pal%dr", i);
    prefs.putUChar(key, palette[i].r);
    snprintf(key, sizeof(key), "pal%dg", i);
    prefs.putUChar(key, palette[i].g);
    snprintf(key, sizeof(key), "pal%db", i);
    prefs.putUChar(key, palette[i].b);
    snprintf(key, sizeof(key), "palPos%d", i);
    prefs.putUChar(key, palette[i].pos);
  }

  prefs.putBool("tzHasDst", tzHasDst);

  // Defensive: the NTP library stores tz names in char[6] (<= 5 chars).
  String stdName = tzStdName;
  String dstName = tzDstName;
  truncateTo5(stdName);
  truncateTo5(dstName);
  prefs.putString("tzStdName", stdName);
  prefs.putString("tzDstName", dstName);

  prefs.putShort("tzStdOffset", tzStdOffset);
  prefs.putShort("tzDstOffset", tzDstOffset);
  prefs.putUChar("tzDstMonth", tzDstMonth);
  prefs.putUChar("tzDstWeek", tzDstWeek);
  prefs.putUChar("tzDstHour", tzDstHour);
  prefs.putUChar("tzStdMonth", tzStdMonth);
  prefs.putUChar("tzStdWeek", tzStdWeek);
  prefs.putUChar("tzStdHour", tzStdHour);

  prefs.putUShort("cfgVersion", 1);

  // end() is what flushes NVS to flash - do not skip it.
  prefs.end();
}
