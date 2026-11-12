# Implementation Brief for Sub-Agents

This file is the operational brief for implementing `PLAN.md`. Read `PLAN.md` in the same
directory first — it is the authoritative specification. This brief adds the
execution discipline and the exact API contracts that the modules must agree on.

**Target:** Arduino ESP32 sketch, folder `TideTime/`, main file `TideTime.ino`.
**Toolchain (verified):** Arduino ESP32 core **3.3.11**, FastLED **3.10.3**, ArduinoJson **7.4.3**,
`sstaub/NTP`.

---

## HARD RULES

1. **All files go flat in the `TideTime/` folder.** No `src/`, no `lib/`. The Arduino builder
   compiles every file in the sketch folder together.
2. **`TideTime.ino` stays named `TideTime.ino`** and stays in the folder `TideTime/`.
3. **Write `.h` + `.cpp` pairs**, not new `.ino` files. Every header needs an include guard.
4. **Do not modify `ci.yml`.**
5. **Do not add new third-party libraries.** `WebServer.h`, `DNSServer.h` and `Preferences.h`
   all ship with the ESP32 core. Do NOT use `ESP_Async_WebServer` / `Async_TCP` even though
   they happen to be installed.
6. **Do not use `lerp8`** — it does not exist. Use `lerp8by8(a, b, frac)` where `frac` is 0–255
   representing 256ths.
7. **Use `CRGBPalette256`** (256 entries, public `CRGB entries[256]`), not `CRGBPalette16`
   (only 16 entries).
8. **Do not rewrite the tide/LED display logic.** `setCurrentTideEvents()`,
   `getPreviousAndNextEvent()`, `parse8601()`, `updateLEDs()`, `TideEvent` and
   `MaybeTideEventPair` already work — keep them. Only the URL and the TLS client change.
9. **Never dereference `currentTideEvents.value()` without checking `.has_value()`** — it is
   an empty `std::optional` if the tide fetch failed, and dereferencing it reboots the device.
10. **No blocking `while (WiFi.status() != WL_CONNECTED)` loops.** On core 3.3.11
    `WiFi.begin(ssid, psk)` is non-blocking by default. Drive the connect from `loop()`.
11. **No `WiFi.mode()` or `WiFi.softAP()` calls outside of a mode transition.** Calling them
    unconditionally in `loop()` resets the radio every iteration.

---

## SHARED API CONTRACT

All modules must agree on exactly these signatures. Do not rename anything.

### `Config.h`

```cpp
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
```

### `Palette.h`

```cpp
#pragma once
#include <FastLED.h>
#include "Config.h"

// Builds a 256-entry palette from 4 stops, linearly interpolated.
// `stops` MUST already be sorted ascending by .pos.
CRGBPalette256 buildPalette(const PaletteStop stops[PALETTE_STOPS]);

// Builds and assigns into the global palette, then refreshes the strip.
void applyPalette(const PaletteStop stops[PALETTE_STOPS]);

extern CRGBPalette256 palette;   // global, defined in Palette.cpp
```

### `Provisioning.h`

```cpp
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "Config.h"

enum DeviceMode {
  MODE_SETUP_AP = 0,     // access point up, waiting for wifi credentials
  MODE_CONNECT_STA,      // access point up, connecting to home wifi
  MODE_RUNNING           // connected to home wifi, no access point
};

extern DeviceMode deviceMode;
extern String lastError;   // empty when there is no error

void startAccessPoint();      // brings up softAP + DNS captive portal
void stopAccessPoint();       // tears down softAP + DNS
void beginWifiConnect();     // non-blocking connect using config.ssid/config.psk
void pollWifiConnect();      // called from loop(); advances the connect state machine
void applyTimezone();        // pushes config's timezone rules into the NTP object
String apSsid();             // "TideTime-XXXX"
String wifiStatusText();     // human-readable WiFi status
```

### `WebUi.h`

```cpp
#pragma once
#include <WebServer.h>
#include "Config.h"

extern WebServer server;

void setupRoutes();   // register ALL routes exactly once, called from setup()
void handleAdminSave();
void handleSetupSave();
```

---

## NVS KEY NAMES (exact — shared between Config.cpp and the Web UI)

`ssid`, `psk`, `stationId`, `numLeds`, `brightness`,
`pal0r` `pal0g` `pal0b` `palPos0`,
`pal1r` `pal1g` `pal1b` `palPos1`,
`pal2r` `pal2g` `pal2b` `palPos2`,
`pal3r` `pal3g` `pal3b` `palPos3`,
`tzStdName`, `tzDstName`, `tzStdOffset`, `tzDstOffset`, `tzHasDst`,
`tzDstMonth`, `tzDstWeek`, `tzDstHour`,
`tzStdMonth`, `tzStdWeek`, `tzStdHour`,
`cfgVersion`.

All ≤ 15 characters. Namespace `tidetime`.

**Defaults:** `ssid=""`, `psk=""`, `stationId="0546"`, `numLeds=48`, `brightness=255`,
palette stops = OCEAN(31,255,160)@0, CYAN(0,200,255)@85, YELLOW(255,220,80)@170, SAND(255,100,0)@255,
`tzStdName="GMT"`, `tzDstName="BST"`, `tzStdOffset=0`, `tzDstOffset=60`, `tzHasDst=true`,
`tzDstMonth=2` (Mar), `tzDstWeek=0` (Last), `tzDstHour=1`,
`tzStdMonth=9` (Oct), `tzStdWeek=0` (Last), `tzStdHour=2`, `cfgVersion=1`.

**Clamps on load:** `numLeds` → 1..300, `brightness` → 0..255, palette `pos` → 0..255,
`tz*Offset` → -720..840, `tz*Month` → 0..11, `tz*Week` → 0..4, `tz*Hour` → 0..23.
`tzStdName`/`tzDstName` truncated to 5 chars.

---

## HTML FORM FIELD NAMES (exact — shared between WebUi.cpp and the HTML)

**Setup form** (`POST /setup`): `ssid`, `psk`.

**Admin form** (`POST /admin`):
`stationId`, `numLeds`, `brightness`,
`pal0r` `pal0g` `pal0b` `palPos0`,
`pal1r` `pal1g` `pal1b` `palPos1`,
`pal2r` `pal2g` `pal2b` `palPos2`,
`pal3r` `pal3g` `pal3b` `palPos3`,
`tzHasDst` (present = on), `tzStdName`, `tzDstName`, `tzStdOffset`, `tzDstOffset`,
`tzDstMonth`, `tzDstWeek`, `tzDstHour`, `tzStdMonth`, `tzStdWeek`, `tzStdHour`.

**Month select values are the 0-based integers 0..11** (Jan=0). **Week select values are
0..4** (Last=0, First=1, Second=2, Third=3, Fourth=4) — matching the NTP library's
`week_t` enum exactly.

---

## PHASE A — Core modules (no web UI)

Files to create: `Config.h`, `Config.cpp`, `Palette.h`, `Palette.cpp`, `Provisioning.h`,
`Provisioning.cpp`.

### A1. `Config.cpp`

- Global `Config config;`.
- A file-static `Preferences prefs;`.
- `Config::load()`: `prefs.begin(NVS_NAMESPACE, false)` (read-write so a later `save()`
  works without reopening), then read every key with the **two-argument** `getX(key, default)`
  form, then clamp, then `prefs.end()`.
  **IMPORTANT:** `Preferences` allows only one open namespace at a time and `begin()` on an
  already-open namespace returns false. So `load()` and `save()` must each `begin()` and
  `end()` around their work, and must not leave the namespace open between calls.
- `Config::save()`: `prefs.begin(NVS_NAMESPACE, false)`, write every key, then `prefs.end()`.
  **`end()` is what flushes NVS to flash — do not skip it.**
- `Config::hasWifi()` as declared.

### A2. `Palette.cpp`

- Global `CRGBPalette256 palette;`
- `buildPalette()` exactly as specified in `PLAN.md` §4.2, using `lerp8by8` and
  `pal.entries[i] = CRGB(...)`.
- `applyPalette()`: `palette = buildPalette(stops); FastLED.show();`
  (only call `FastLED.show()` if the strip is initialised — guard with a flag if needed).

### A3. `Provisioning.cpp`

- Global `DeviceMode deviceMode = MODE_SETUP_AP;` and `String lastError;`
- `apSsid()`: `"TideTime-" + last 4 hex digits of the MAC`.
  Use `ESP.getEfuseMac()` and build the hex string manually (do not rely on `String(uint32_t, HEX)`
  zero-padding behaviour).
- `startAccessPoint()`:
  - `WiFi.mode(WIFI_AP_STA)` — AP and STA together, so the STA side can still try to connect.
  - `WiFi.softAP(apSsid().c_str())` — open AP, no password.
  - `dnsServer.setErrorReplyCode(DNSReplyCode::NoError);`
  - `dnsServer.start(53, "*", WiFi.softAPIP());`
  - `deviceMode = MODE_SETUP_AP;`
- `stopAccessPoint()`: `dnsServer.stop(); WiFi.softAPdisconnect(true);` then switch to
  `WiFi.mode(WIFI_STA)`.
- `beginWifiConnect()`:
  - `WiFi.mode(WIFI_AP_STA)` (keep the AP up during the attempt).
  - `WiFi.begin(config.ssid.c_str(), config.psk.c_str());` — **non-blocking on core 3.3.11**.
  - Set a 20-second deadline using `millis()`.
  - `deviceMode = MODE_CONNECT_STA;`
- `pollWifiConnect()`:
  - If `deviceMode != MODE_CONNECT_STA` return.
  - `wl_status_t st = WiFi.status();`
  - `WL_CONNECTED` → record success, **keep the AP up for 60 seconds** so the browser can
    poll `/status` one more time and show the LAN IP, then switch to `WiFi.mode(WIFI_STA)`
    and stop the DNS server. Implement this with a "transition at" timestamp, not a
    blocking `delay()`.
  - `WL_CONNECT_FAILED` / `WL_NO_SSID_AVAIL` → `lastError = wifiStatusText();` and fall back
    to `MODE_SETUP_AP` (the AP is still up, so the UI can show the error).
  - Timeout (20s) → `lastError = "Connection timed out";` → `MODE_SETUP_AP`.
- `wifiStatusText()`: map `wl_status_t` to a short string.
- `applyTimezone()`:
  ```cpp
  if (!config.tzHasDst) {
    ntp.timeZone((int8_t)(config.tzStdOffset / 60), (int8_t)(config.tzStdOffset % 60));
    ntp.isDST(false);
  } else {
    ntp.ruleDST(config.tzDstName.c_str(), (int8_t)config.tzDstWeek, Sun,
                (int8_t)config.tzDstMonth, (int8_t)config.tzDstHour, config.tzDstOffset);
    ntp.ruleSTD(config.tzStdName.c_str(), (int8_t)config.tzStdWeek, Sun,
                (int8_t)config.tzStdMonth, (int8_t)config.tzStdHour, config.tzStdOffset);
  }
  ```
  `ntp` is the global `NTP ntp(wifiUdp)` from `TideTime.ino` — declare it `extern` in
  `Provisioning.h` or move the declaration into a shared header. **Pick one and make it
  consistent**: recommended — keep `WiFiUDP wifiUdp;` and `NTP ntp(wifiUdp);` in
  `TideTime.ino`, and in `Provisioning.cpp` add `extern WiFiUDP wifiUdp; extern NTP ntp;`.
  Note the offsets are **absolute minutes from UTC**, and `timeZone()` needs **signed**
  hours/minutes — use signed division so `-300` becomes `-5 hours`.

---

## PHASE B — Web UI + main sketch integration

Files to create: `WebUi.h`, `WebUi.cpp`. Files to rewrite: `TideTime.ino`.

### B1. `WebUi.cpp`

Global `WebServer server(80);`

`setupRoutes()` — register every route **exactly once** (called once from `setup()`):
`/`, `/setup` (GET+POST), `/scan`, `/status`, `/admin` (GET+POST), `/factory-reset` (POST),
`/favicon.ico` (204), and the captive-portal probe URLs:
`/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`,
`/redirect`, `/ncsi.txt`, `/connecttest.txt`, `/success.txt`, `/canonical.html`, `/success`,
`/default` — each returns `302` with `Location: /setup`.

- `/` → 302 to `/setup` if `deviceMode != MODE_RUNNING`, else 302 to `/admin`.
- `/setup` GET → serve `SETUP_HTML` from PROGMEM via `server.send_P(200, "text/html", SETUP_HTML)`.
  The page has an SSID text input, a password input, a "Scan" button that fetches `/scan`
  and populates a `<datalist>`, and a submit button. After submit it polls `/status` every
  1s and shows connecting/success/error.
- `/setup` POST → read `ssid`/`psk`; validate (SSID non-empty and ≤ 32 chars; PSK ≤ 63 chars
  and either empty or 8–63 chars); reject if the SSID equals the device's own AP SSID; save
  `config.ssid`/`config.psk` via `config.save()`; call `beginWifiConnect()`; respond with a
  short page that polls `/status`.
- `/scan` GET → `WiFi.scanNetworks(false, true)`; build a JSON array of
  `{"ssid":"...","rssi":-55,"secure":true}`; cap at 20 entries; `server.send(200, "application/json", json)`.
  Always call `WiFi.scanDelete()` afterwards.
- `/status` GET → JSON with `mode`, `ssid`, `ip`, `rssi`, `connecting`, `error`.
- `/admin` GET → serve `ADMIN_HTML` from PROGMEM, pre-filled with the current `config` values.
  The admin page contains:
  - **Palette**: 4 rows, each with `<input type="color">` + R/G/B number inputs +
    a position range slider 0–255. A live CSS `linear-gradient` preview bar that updates on
    every `input` event (pure JS, no round-trip).
  - **LED count**: number input 1–300.
  - **Brightness**: range slider 0–255 with a live readout.
  - **Timezone**: checkbox `tzHasDst`; text inputs for the two names (maxlength=5);
    number inputs for the two UTC offsets in minutes; selects for the four months
    (0–11) and two weeks (0–4); number inputs for the two hours (0–23).
    A visible note that the offsets are **minutes from UTC** (e.g. 0 for GMT, -300 for EST).
  - **Station ID**: text input.
  - The form posts to `/admin` as `application/x-www-form-urlencoded`.
  - The page must be responsive/mobile-friendly (the user configures this from a phone).
- `/admin` POST → parse every field per the shared contract; clamp everything; **sort the
  four palette stops by `pos`**; save; then apply live:
  - palette → `applyPalette(config.palette)`
  - brightness → `FastLED.setBrightness(config.brightness); FastLED.show();`
  - LED count → `FastLED.clear(); FastLED.show(); FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds); FastLED.show();`
  - timezone → `applyTimezone(); ntp.update();`
  - station ID → re-fetch the tide data
  Then respond with a success page.
- `/factory-reset` POST → clear the NVS namespace (`prefs.begin(...); prefs.clear(); prefs.end();`)
  and `ESP.restart()`.

**HTML/JS requirements:**
- All pages: a `<meta name="viewport" content="width=device-width, initial-scale=1">`.
- **Escape any user-supplied string** (SSID, PSK, station ID) before echoing it into HTML
  or JSON. Write a small `htmlEscape()` helper (escape `<`, `>`, `&`, `"`, `'`).
- JSON string values need a `jsonEscape()` helper (escape `"`, `\`, and control chars).
- Keep the JS vanilla — no frameworks, no external CDN (the device has no internet in AP mode).
- The admin preview gradient is built in JS from the 4 stops, sorted by position.

### B2. `TideTime.ino` (rewrite)

Structure:

```cpp
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

WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

#define MAX_LEDS 300          // from Config.h
CRGB leds[MAX_LEDS];
```

`setup()`:
1. `Serial.begin(115200)` + the existing countdown banner (keep it — useful for diagnosis).
2. `config.load();` and print every loaded setting to Serial.
3. `applyTimezone();` then `ntp.begin();`
4. `palette = buildPalette(config.palette);`
5. `FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds);`
6. `FastLED.setMaxPowerInVoltsAndMilliamps(5, 2500);` (keep the existing power limit)
7. `FastLED.setBrightness(config.brightness);`
8. `setupRoutes();` — register the routes **once**.
9. `server.begin();`
10. If `config.hasWifi()` → `beginWifiConnect();` else → `startAccessPoint();`

`loop()`:
1. `pollWifiConnect();` — drives the connect state machine.
2. `dnsServer.processNextRequest();` **only while the AP is up** (guard on
   `deviceMode != MODE_RUNNING`).
3. `server.handleClient();`
4. The existing NTP update + tide fetch logic, with the `.has_value()` guard added.
5. `updateLEDs();`
6. The existing `delay(1000)`.

**Tide fetch changes:**
- Build the URL from the station ID:
  ```cpp
  String url = "https://easytide.admiralty.co.uk/Home/GetPredictionData?stationId=" + config.stationId;
  ```
- **Fix the TLS bug.** The current code calls `http.begin("https://...")` with no CA
  certificate, which fails TLS verification on arduino-esp32 3.x and leaves
  `currentTideEvents` empty. Use:
  ```cpp
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setUserAgent("TideTime/1.0");
  ```
- Sanitise `config.stationId` before building the URL (trim whitespace; only
  `[A-Za-z0-9-]` allowed; ≤ 32 chars). Reject otherwise and log a warning.
- **Re-fetch periodically**, not just once: the tide events list is only valid for ~7 days,
  and the device may boot before the network is ready. Track the last successful fetch time
  and re-fetch every 6 hours, plus immediately when the station ID changes.
- Guard `currentTideEvents.value()` with `.has_value()` — the existing code at the
  `calculatedTideTime` block dereferences it unconditionally and will crash if the fetch failed.

**`updateLEDs()` changes:**
- Replace `NUM_LEDS` with `config.numLeds` in the loop bound.
- Use `ColorFromPalette(palette, cIndex)` (the `CRGBPalette256` overload) — the palette code
  is currently commented out at lines 242–252; keep the existing `firstSandIndex` /
  OCEAN / SAND behaviour, just make the count dynamic.
- Keep the debug `Serial.println`s but consider gating them behind `#define DEBUG_SERIAL 1`.
  **Do not change the visual behaviour** — this task is the admin panel, not a redesign.

**Remove:** `#define TZ_INFO`, `#define NUM_LEDS`, `#define OCEAN/CYAN/YELLOW/SAND`,
`DEFINE_GRADIENT_PALETTE(pal_gp)`, `CRGBPalette16 palette = pal_gp;`, the `WiFiMulti` include
and `wifiMulti.addAP(...)`, and the hardcoded station ID in the URL.
(Keep the `OCEAN`/`SAND` colour macros only if `updateLEDs()` still references them — if
so, keep those two and drop `CYAN`/`YELLOW` which are only used by the palette macro.)

---

## ACCEPTANCE CHECKLIST

The implementation is done when ALL of these hold:

- [ ] `TideTime.ino` + 4 header/cpp pairs exist, all flat in `TideTime/`.
- [ ] No `#include <WiFiMulti.h>`, no `wifiMulti`, no hardcoded SSID/password anywhere.
- [ ] No hardcoded `stationId=0546` — it comes from `config.stationId`.
- [ ] No `DEFINE_GRADIENT_PALETTE`, no `CRGBPalette16 palette`, no `lerp8`.
- [ ] `palette` is a `CRGBPalette256`.
- [ ] Every `currentTideEvents.value()` is preceded by a `.has_value()` check.
- [ ] The EasyTide fetch uses `WiFiClientSecure` and `http.begin(client, url)`.
- [ ] `WiFi.begin()` is never called in a blocking loop.
- [ ] `WiFi.mode()` / `WiFi.softAP()` are only called on a mode transition.
- [ ] `config.save()` calls `prefs.end()` so NVS is flushed.
- [ ] Palette stops are sorted by position before being applied.
- [ ] All form fields use the exact names in the shared contract above.
- [ ] User-supplied strings are HTML/JSON escaped.
- [ ] The admin page has a viewport meta tag and works on a phone.
- [ ] Every setting applies **without a reboot**.
- [ ] Settings survive a power cycle.
- [ ] A fresh device with no NVS comes up in AP mode with SSID `TideTime-XXXX`.
- [ ] The code compiles cleanly for the ESP32 target.

---

## HOW TO REPORT BACK

When finished, report:
1. The list of files created/modified.
2. Any place you had to deviate from this brief, and why.
3. Any assumption you made that the brief did not specify.
4. The exact compile command used, and whether it compiled without errors.
5. Anything you could NOT verify (e.g. you cannot flash hardware — say so plainly).
