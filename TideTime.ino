/**
 * TideTime - tide-height LED strip clock.
 *
 * Phase B rewrite: integrates Config (NVS settings), Palette (runtime
 * CRGBPalette256), Provisioning (AP / captive portal / WiFi connect state
 * machine) and WebUi (WebServer admin panel).
 *
 * Globals owned here (Provisioning.cpp uses them via extern declarations):
 *   WiFiUDP wifiUdp;
 *   NTP     ntp(wifiUdp);
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiUDP.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <FastLED.h>

#include "NTP.h"

#include <chrono>
#include <optional>

#include "Config.h"
#include "Palette.h"
#include "Provisioning.h"
#include "WebUi.h"

#define DEBUG_SERIAL 0  // 1 = verbose per-loop debug prints

WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

// Fixed-size buffer; config.numLeds (1..MAX_LEDS from Config.h) selects how
// many LEDs are actually driven.
CRGB leds[MAX_LEDS];

JsonDocument jsonDoc;

typedef std::chrono::sys_time<std::chrono::milliseconds> TideTime;

struct TideEvent {
  TideTime time;
  int type;
};

typedef std::optional<std::pair<TideEvent, TideEvent> > MaybeTideEventPair;

// parse8601: parses "YYYY-MM-DDTHH:MM:SS(.mmm)" into UTC sys_time.
//
// NOTE (deviation from the original iostream version): the original used
// istringstream + std::chrono::parse("%FT%T"), which drags in libstdc++
// iostream/locale (~237 KB of flash) and pushed the sketch past the default
// 1.31 MB app partition (TLS + WebServer already added ~250 KB). This version
// keeps the exact same observable semantics - the millisecond fraction is
// left unread, and any parse failure yields epoch 0 - at a fraction of the
// flash cost, using sscanf + header-only chrono calendar arithmetic.
std::chrono::sys_time<std::chrono::milliseconds> parse8601(std::string str) {
  int y = 1970, mo = 1, d = 1, h = 0, mi = 0, s = 0, ms = 0;
  sscanf(str.c_str(), "%d-%d-%dT%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &s, &ms);
  (void)ms;  // unchanged from the original %T behaviour: fraction not consumed
  const std::chrono::year_month_day ymd{std::chrono::year{y}, std::chrono::month{(unsigned)mo},
                                        std::chrono::day{(unsigned)d}};
  const std::chrono::sys_days sd = ymd;
  return std::chrono::sys_time<std::chrono::milliseconds>{
      sd + std::chrono::hours{h} + std::chrono::minutes{mi} + std::chrono::seconds{s}};
}

MaybeTideEventPair getPreviousAndNextEvent(JsonArray eventList) {
  TideEvent previous{};
  time_t now = ntp.epoch() * 1000;
  Serial.print("Now: ");
  Serial.println(now);

  for (auto x : eventList) {
    auto time = parse8601(x["dateTime"].as<const char*>());
    Serial.println(time.time_since_epoch().count());

    TideEvent e = { time, x["eventType"].as<int>() };

    if (time.time_since_epoch().count() > now) {
      return std::pair<TideEvent, TideEvent>(previous, e);
    }

    previous = e;
  }
  return std::nullopt;
}

MaybeTideEventPair currentTideEvents;

// Set by the web UI when the station ID changes; consumed in loop().
volatile bool tideRefetchRequested = false;

static const unsigned long TIDE_REFETCH_MS = 6UL * 60UL * 60UL * 1000UL;  // 6 hours
static const unsigned long TIDE_RETRY_MS   = 60UL * 1000UL;               // 1 min after a failed fetch
static unsigned long lastTideAttemptMs = 0;
static bool lastTideFetchOk = false;

// Trim + whitelist [A-Za-z0-9-], max 32 chars. Returns false when the stored
// station ID is not safe to put into a URL.
static bool sanitizeStationId(String& out) {
  String s = config.stationId;
  s.trim();
  if (s.length() == 0 || s.length() > 32) {
    return false;
  }
  for (unsigned int i = 0; i < s.length(); ++i) {
    const char c = s[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || (c == '-');
    if (!ok) {
      return false;
    }
  }
  out = s;
  return true;
}

void setCurrentTideEvents() {
  String stationId;
  if (!sanitizeStationId(stationId)) {
    Serial.printf("[Tide] invalid stationId \"%s\" - not fetching\n",
                  config.stationId.c_str());
    return;
  }

  const String url =
      "https://easytide.admiralty.co.uk/Home/GetPredictionData?stationId=" + stationId;
  Serial.printf("[Tide] fetching %s\n", url.c_str());

  // Core 3.x: https:// requires a WiFiClientSecure; without a CA pin we use
  // setInsecure() so the TLS handshake actually succeeds.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setUserAgent("TideTime/1.0");

  const int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    const String payload = http.getString();
    const DeserializationError err = deserializeJson(jsonDoc, payload);
    if (err) {
      Serial.printf("[Tide] JSON error: %s\n", err.c_str());
    } else {
      JsonArray eventList = jsonDoc["tidalEventList"].as<JsonArray>();
      if (eventList.isNull() || eventList.size() == 0) {
        Serial.println("[Tide] no tidalEventList in response");
      } else {
        currentTideEvents = getPreviousAndNextEvent(eventList);
        lastTideFetchOk = currentTideEvents.has_value();
        if (lastTideFetchOk) {
          // Guarded dereference: has_value() checked directly above.
          Serial.println("[Tide] events set:");
          Serial.println(currentTideEvents.value().first.time.time_since_epoch().count());
          Serial.println(currentTideEvents.value().second.time.time_since_epoch().count());
        } else {
          Serial.println("[Tide] no upcoming events in list");
        }
      }
    }
  } else {
    Serial.printf("[Tide] GET failed, code: %d\n", httpCode);
  }

  http.end();
}

static float fill = 0.0f;

void updateLEDs() {
  // Never dereference currentTideEvents without this guard: it stays an empty
  // optional until the first successful fetch.
  if (!currentTideEvents.has_value()) {
    return;
  }

  const auto previous = currentTideEvents.value().first;
  const auto next = currentTideEvents.value().second;
  const auto now = ntp.epoch() * 1000;
  const auto timeFromPrevious = now - previous.time.time_since_epoch().count();
  const auto duration = next.time.time_since_epoch().count() - previous.time.time_since_epoch().count();

  const auto progress = timeFromPrevious / (float)duration;
  const auto eased = (1.0f - cos(PI * progress)) / 2.0f;
  // The real tide-proportional fill is still not enabled (unchanged from the
  // original sketch, which animates the "fill" debug sweep instead).
  (void)eased;

  float sea_fill = fill;
  fill += 0.01f;
  if (fill > 1.0f) {
    fill = 0.0f;
  }

#if DEBUG_SERIAL
  Serial.println(sea_fill);
  Serial.println(ntp.epoch());
  Serial.println(ntp.formattedTime("%d. %B %Y"));  // dd. Mmm yyyy
  Serial.println(ntp.formattedTime("%A %T"));      // Www hh:mm:ss
#endif

  const int numLeds = config.numLeds;
  const int firstSandIndex = (int)(sea_fill * numLeds);

  for (int i = 0; i < numLeds; ++i) {
    if (i == firstSandIndex) {
      leds[i] = CRGB(255, 0, 0);  // shoreline marker
    } else if (i < firstSandIndex) {
      // Sea = palette start (OCEAN with the default palette)...
      leds[i] = ColorFromPalette(palette, (uint8_t)0);
    } else {
      // ...sand = palette end (SAND with the default palette).
      leds[i] = ColorFromPalette(palette, (uint8_t)255);
    }
#if DEBUG_SERIAL
    Serial.printf("%i: %u %u %u\n", i, (unsigned)leds[i].r, (unsigned)leds[i].g,
                  (unsigned)leds[i].b);
#endif
  }

  FastLED.show();
}

void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %u...\n", t);
    Serial.flush();
    delay(1000);
  }

  // 1. Load persisted settings.
  config.load();

  Serial.println("[SETUP] Loaded settings:");
  Serial.printf("  ssid=\"%s\" (psk %u chars)\n", config.ssid.c_str(),
                (unsigned)config.psk.length());
  Serial.printf("  stationId=\"%s\"\n", config.stationId.c_str());
  Serial.printf("  numLeds=%u brightness=%u\n",
                (unsigned)config.numLeds, (unsigned)config.brightness);
  for (int i = 0; i < PALETTE_STOPS; ++i) {
    Serial.printf("  pal%d: pos=%u rgb=(%u,%u,%u)\n", i,
                  (unsigned)config.palette[i].pos, (unsigned)config.palette[i].r,
                  (unsigned)config.palette[i].g, (unsigned)config.palette[i].b);
  }
  Serial.printf("  tz: hasDst=%u std=\"%s\" (%d min) dst=\"%s\" (%d min)\n",
                (unsigned)config.tzHasDst, config.tzStdName.c_str(),
                (int)config.tzStdOffset, config.tzDstName.c_str(),
                (int)config.tzDstOffset);
  Serial.printf("  tz rules: dst m%u w%u h%u, std m%u w%u h%u\n",
                (unsigned)config.tzDstMonth, (unsigned)config.tzDstWeek,
                (unsigned)config.tzDstHour, (unsigned)config.tzStdMonth,
                (unsigned)config.tzStdWeek, (unsigned)config.tzStdHour);

  // 2. Timezone rules must be pushed into the NTP object before ntp.begin().
  applyTimezone();
  ntp.begin();

  // 3. Runtime gradient palette + LED strip.
  palette = buildPalette(config.palette);
  FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2500);
  FastLED.setBrightness(config.brightness);

  // 4. Web UI: routes registered exactly once, then start the server.
  setupRoutes();
  server.begin();
  Serial.println("[SETUP] HTTP server listening on port 80");

  // 5. Connect to the stored network, or start the setup AP.
  if (config.hasWifi()) {
    beginWifiConnect();
  } else {
    startAccessPoint();
  }
}

void loop() {
  // 1. Drive the WiFi connect state machine (non-blocking).
  pollWifiConnect();

  // 2. Captive-portal DNS only while the AP is up.
  if (deviceMode != MODE_RUNNING) {
    dnsServer.processNextRequest();
  }

  // 3. Web UI.
  server.handleClient();

  // 4. Time sync.
  if (!ntp.update()) {
#if DEBUG_SERIAL
    Serial.println("No valid time data");
#endif
  }

  // 5. Tide data: first fetch once WiFi + a valid clock are available, then
  //    every 6 hours (1-minute retry after a failure), plus immediately when
  //    the station ID was changed from the admin panel.
  if (WiFi.status() == WL_CONNECTED && ntp.epoch() > 1000000000UL) {
    const unsigned long nowMs = millis();
    const unsigned long interval = lastTideFetchOk ? TIDE_REFETCH_MS : TIDE_RETRY_MS;
    if (tideRefetchRequested || lastTideAttemptMs == 0 ||
        (nowMs - lastTideAttemptMs) >= interval) {
      tideRefetchRequested = false;
      lastTideAttemptMs = nowMs;
      setCurrentTideEvents();
    }
  }

  // 6. LEDs + pacing.
  updateLEDs();
  delay(1000);
}
