# TideTime — Web Admin Panel & WiFi Provisioning: Implementation Plan

> **Historical document.** This plan records the design as it was originally implemented
> and is kept for reference. The LED layer has since moved from FastLED to Adafruit
> NeoPixel to cut clean build times from ~290 s to ~160 s — see "Build times" in
> `README.md`. Where this plan mentions FastLED / `CRGBPalette256` / `lerp8by8`, read
> it as a description of the approach, not of the current code.

**Target:** ESP32 (arduino-esp32 core), Arduino framework, single sketch folder `TideTime/`.
**Goal:** Replace hardcoded credentials/config with a persistent settings store and a captive-portal-style web admin panel.

---

## 0. Current State (audit of existing code)

File: `TideTime.ino` (278 lines, single file, no other sources).

| Concern | Current state | Problem |
|---|---|---|
| WiFi creds | `wifiMulti.addAP("TNCAPCE0FAA", "y4PYgmp3RppedRGf")` (line 168) | Hardcoded, leaked into git, no way to change |
| Station ID | Hardcoded in URL `...stationId=0546` (line 126) | Not configurable |
| LED count | `#define NUM_LEDS 48` (line 30) | Compile-time; `CRGB leds[NUM_LEDS]` fixed-size global array |
| Brightness | `FastLED.setBrightness(255)` (line 180) | Hardcoded |
| Gradient palette | `DEFINE_GRADIENT_PALETTE(pal_gp)` macro (lines 39–44) | Compile-time macro; cannot be built at runtime |
| Timezone | `#define TZ_INFO "Europe/London"` (line 24) — **unused**; actual rules hardcoded via `ntp.ruleDST("BST", Last, Sun, Mar, 1, 60)` / `ntp.ruleSTD("GMT", Last, Sun, Oct, 2, 0)` (lines 174–175) | Not configurable; offset semantics odd (see §5) |
| Storage | None | Everything compile-time |
| LED update | `updateLEDs()` uses `fill` counter for a debug sweep, ignores real tide progress | Not our concern, but must not regress |

**Libraries already in use** (must remain compatible):
- `FastLED` — LED control
- `ArduinoJson` (v7 API — `JsonDocument`, `deserializeJson`, `.as<JsonArray>()`)
- `sstaub/NTP` — time sync (API verified: `begin()`, `update()` returns `bool`, `epoch()`, `formattedTime()`, `ruleDST(tzName, week, wday, month, hour, tzOffset)`, `ruleSTD(...)`, `timeZone(h,m)`, `isDST(bool)`)
- `HTTPClient`, `WiFiMulti`, `WiFiUDP`

**Target board:** ESP32 with WiFi + softAP support (matches `ci.yml` requiring `CONFIG_SOC_WIFI_SUPPORTED=y`).

---

## 0a. Verified Environment Facts (do not re-derive — these were checked against the installed toolchain)

| Fact | Value |
|---|---|
| Arduino ESP32 core | **3.3.11** at `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\3.3.11` |
| FastLED | **3.10.3** (in `%USERPROFILE%\Documents\Arduino\libraries\FastLED`) |
| ArduinoJson | **7.4.3** (v7 API: `JsonDocument`, `deserializeJson`, `.as<JsonArray>()`) |
| NTP lib | `sstaub/NTP` (folder `NTP` in the Arduino libraries dir) |
| `WebServer.h` | Ships with the core: `libraries\WebServer\src\WebServer.h` |
| `DNSServer.h` | Ships with the core: `libraries\DNSServer\src\DNSServer.h` |
| `Preferences.h` | Ships with the core: `libraries\Preferences\src\Preferences.h` |

**Core 3.3.11 API specifics that matter:**

- `WiFi.begin(const char *ssid, const char *passphrase = NULL, int32_t channel = 0, const uint8_t *bssid = NULL, bool tryConnect = true)` — **`tryConnect` defaults to `true`, so `WiFi.begin(ssid, psk)` is ALREADY NON-BLOCKING on this core.** Do not write a blocking connect loop. Call `WiFi.begin(...)` then poll `WiFi.status()` in `loop()`.
- `WiFi.softAP(...)` and `WiFi.softAPConfig(local_ip, gateway, subnet, dhcp_lease_start, dns)` both exist.
- `DNSServer`: `setErrorReplyCode(const DNSReplyCode&)`, `start(uint16_t port, const String &domainName, const IPAddress &resolvedIP)`, `stop()`, `processNextRequest()`.
- `WebServer`: `send_P(int code, PGM_P content_type, PGM_P content)` for PROGMEM HTML, `sendHeader(name, value, first)`, `arg(const String&)`, `hasArg(const String&)`.
- `Preferences`: `begin(const char *name, bool readOnly = false, const char *partition_label = NULL)`, `end()`, `clear()`, `remove(key)`, `isKey(key)`, and typed `getX(key, defaultValue)` / `putX(key, value)` pairs for every type. **`getX` takes a default value argument — use it so missing keys fall back to defaults without `isKey` checks per field.**

**FastLED 3.10.3 specifics that matter:**

- `CRGBPalette16` has **16** entries. `CRGBPalette256` has **256** entries, exposed as public `CRGB entries[256]`.
- `lerp8by8(uint8_t a, uint8_t b, fract8 frac)` — global namespace. **`lerp8` does not exist.**
- `fract8` = `fl::u8` = 0–255 representing 256ths.
- `ColorFromPalette(const CRGBPalette256 &pal, fl::u8 index, fl::u8 brightness = 255, TBlendType blendType = NOBLEND)` exists.

**Repo/build notes:**
- The repo contains only `TideTime.ino` and `ci.yml`. There is **no** `platformio.ini` and **no** `.github/workflows` directory — the CI is external. `ci.yml` is an ESP-IDF/PlatformIO-style `requires_any` block listing `CONFIG_SOC_WIFI_SUPPORTED=y` / `CONFIG_ESP_WIFI_REMOTE_ENABLED=y`.
- **Consequence: the project is built as an Arduino sketch.** The main file must stay `TideTime.ino` in a folder named `TideTime/`, and additional `.h`/`.cpp` files placed **in that same folder** are compiled together by the Arduino builder. Do **not** create a `src/` directory and do **not** rename the sketch.
- `Async_TCP` and `ESP_Async_WebServer` happen to be installed, but this plan deliberately does **not** use them (see §1).

---

## 1. Architecture Decision Summary (read this before implementing)

The single most important decision: **do not rewrite into a new framework**. Keep it an Arduino sketch. This plan adds:

1. **`Config` module** — a struct holding all runtime settings + NVS (Preferences) persistence.
2. **`Config::Palette`** — a runtime-constructible gradient palette replacing the `DEFINE_GRADIENT_PALETTE` macro.
3. **`WebServer` + `DNSServer`** — two servers, one AP (captive portal), one STA (admin panel).
4. **`Provisioner`** — state machine for the "connect me to your wifi" flow.
5. **Refactor `TideTime.ino`** to consume `Config` instead of `#define`s.

**Do NOT use ESPAsyncWebServer.** Reasons:
- It is a third-party dependency requiring a matching `AsyncTCP` version; version-skew causes compile failures that are painful for a cheaper model to debug.
- It is **not thread-safe** and interacts badly with `ArduinoJson` deserialization in the same tick.
- The built-in `WebServer` (`ESP32 Web Server` from the core) is synchronous, dependency-free, and sufficient: we have ~5 form endpoints, no need for websockets/SSE.

**Do NOT use LittleFS for settings.** `Preferences` (NVS) is simpler, atomic, and avoids a filesystem partition — important because the board's partition table is not under our control and adding a `spiffs` partition could fail to flash.

**Key architectural constraint (important for the sub-agent):**
The ESP32 radio cannot be in two modes at once. Use `WiFi.mode(WIFI_AP_STA)` so the AP stays up *while* the STA side connects to the user's network. This is what makes the whole flow work without a reboot dance.

---

## 2. Mode State Machine

There are exactly **three** operating modes. Model this as an enum + explicit transitions.

```
                 ┌──────────────────────────────────────────┐
                 │                                          │
                 ▼                                          │
            ┌─────────┐   no creds in NVS   ┌──────────┐  │
            │ BOOT    │────────────────────►│ SETUP_AP │  │
            └─────────┘                     └──────────┘  │
                 │  creds present                  │       │
                 ▼                                 │       │
          ┌─────────────┐   creds submitted        │       │
          │ CONNECT_STA │◄─────────────────────────┘       │
          └─────────────┘         (via AP)                │
                 │                                          │
        success │                        failure            │
                 ▼                                          │
          ┌─────────────┐   disconnect / fail              │
          │ RUNNING     │──────────────────────────┐       │
          └─────────────┘                           │       │
                 │                                 ▼       │
                 │                          ┌─────────────┐│
                 │                          │ SETUP_AP    ││
                 │                          └─────────────┘│
                 │                                          │
                 └──────────────────────────────────────────┘
```

| Mode | WiFi mode | SoftAP | WebServer | DNS server | Notes |
|---|---|---|---|---|---|
| `SETUP_AP` | `WIFI_AP_STA` | ON (`TideTime-XXXX`) | ON, port 80 | ON, port 53 | Captive portal. STA may or may not be connected. |
| `CONNECT_STA` | `WIFI_AP_STA` | ON | ON, port 80 | ON | AP stays up so the user can see progress/retry. Blocking-ish connect with timeout. |
| `RUNNING` | `WIFI_STA` | OFF | ON, port 80 | OFF | Normal operation. Admin panel at the LAN IP. |

### Transition rules (must be exact)

- **`BOOT` → `SETUP_AP`**: NVS contains no usable SSID (key `ssid` missing or empty).
- **`BOOT` → `CONNECT_STA`**: NVS contains an SSID. Attempt connect with a **20-second** budget. On timeout → `SETUP_AP` (do NOT retry forever; surface the failure in the UI).
- **`SETUP_AP` → `CONNECT_STA`**: user submits the wifi form. Keep AP up.
- **`CONNECT_STA` → `RUNNING`**: `WiFi.status() == WL_CONNECTED`.
- **`CONNECT_STA` → `SETUP_AP`**: timeout or `WiFi.status() == WL_CONNECT_FAILED / WL_NO_SSID_AVAIL`.
- **`RUNNING` → `SETUP_AP`**: STA drops and cannot reconnect within the budget. (Optional; at minimum keep retrying STA in the background so a transient router reboot doesn't brick the device.)

**Re-entrancy rule:** `WiFi.mode()` and `WiFi.softAP()` calls must happen only on a mode transition, never inside `loop()` unconditionally.

---

## 3. Settings Store (`Config`)

### 3.1 Storage mechanism

Use `Preferences` (NVS). Namespace: **`tidetime`** (must be ≤ 15 chars — enforced).

Open the namespace **once** in `setup()` in read-write mode and keep it open for the lifetime of the sketch (one `Preferences` object; the docs warn only one namespace may be open at a time). Call `end()` only at shutdown, if at all.

### 3.2 Keys, types, defaults

All keys ≤ 15 characters.

| Key | NVS type | Default | Range / notes |
|---|---|---|---|
| `ssid` | String | `""` | Home wifi SSID |
| `psk` | String | `""` | Home wifi password (may be empty for open networks) |
| `stationId` | String | `"0546"` | EasyTide station ID. **Stored as String, not int** — IDs may have leading zeros or be alphanumeric (e.g. `0546`, `0412`, `Lochranza`). |
| `numLeds` | UShort | `48` | Range 1–300. Clamp on load. |
| `brightness` | UChar | `255` | Range 0–255. |
| `pal0r,pal0g,pal0b` | UChar ×3 | `31,255,160` (OCEAN) | Palette stop 0 |
| `pal1r,pal1g,pal1b` | UChar ×3 | `0,200,255` (CYAN) | Palette stop 1 |
| `pal2r,pal2g,pal2b` | UChar ×3 | `255,220,80` (YELLOW) | Palette stop 2 |
| `pal3r,pal3g,pal3b` | UChar ×3 | `255,100,0` (SAND) | Palette stop 3 |
| `palPos0..palPos3` | UChar ×4 | `0,85,170,255` | Position of each stop in the 0–255 gradient |
| `tzStdName` | String | `"GMT"` | STD tz abbreviation (≤5 chars — NTP library `char tzName[6]`) |
| `tzDstName` | String | `"BST"` | DST tz abbreviation (≤5 chars) |
| `tzStdOffset` | Int | `0` | Minutes from UTC for standard time |
| `tzDstOffset` | Int | `60` | Minutes from UTC for daylight saving time |
| `tzHasDst` | Bool | `true` | Whether DST rules apply at all |
| `tzDstMonth` | UChar | `2` (Mar) | 0–11, matches NTP lib `month_t` enum |
| `tzDstWeek` | UChar | `0` (Last) | 0–4, matches `week_t` enum |
| `tzDstHour` | UChar | `1` | Hour of day DST starts |
| `tzStdMonth` | UChar | `9` (Oct) | 0–11 |
| `tzStdWeek` | UChar | `0` (Last) | 0–4 |
| `tzStdHour` | UChar | `2` | Hour of day STD starts |
| `cfgVersion` | UShort | `1` | Schema version. Bump when adding keys; on mismatch, write defaults for missing keys but keep user values. |

**Important:** the existing sketch calls `ntp.ruleDST("BST", Last, Sun, Mar, 1, 60)` and `ntp.ruleSTD("GMT", Last, Sun, Oct, 2, 0)`. Note the DST offset (60) vs STD offset (0) — these are **absolute offsets from UTC**, not deltas. The default table above preserves this exact behaviour so the device does not change behaviour unexpectedly.

### 3.3 `Config` struct

```cpp
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

  PaletteStop palette[4];

  // timezone
  bool     tzHasDst;
  String   tzStdName;   // e.g. "GMT"
  String   tzDstName;   // e.g. "BST"
  int16_t  tzStdOffset; // minutes from UTC
  int16_t  tzDstOffset; // minutes from UTC
  uint8_t  tzDstMonth, tzDstWeek, tzDstHour;
  uint8_t  tzStdMonth, tzStdWeek, tzStdHour;

  void load();   // read from NVS, applying defaults for missing keys
  void save();   // write all fields to NVS
  bool hasWifi() const { return ssid.length() > 0; }
};
```

### 3.4 Load/save semantics

- `load()`: for each key, use the two-argument `getX(key, default)` form so a missing key yields the default without needing `isKey` checks for every field. Use `isKey("cfgVersion")` once to detect first-run and log it.
- `save()`: write every field, then `prefs.end()` is **not** called — NVS commits on `put*`. (Verify: `Preferences` flushes on `end()`. To be safe, call `prefs.end()` and re-`begin()` after a bulk save, or add a `flush()`-style helper that does `end()` + `begin()`.) **Implement `save()` as: write fields → `prefs.end()` → `prefs.begin("tidetime", false)`.** This guarantees the write is flushed to flash.
- Clamp all numeric values on load (e.g. `numLeds` into 1–300, `brightness` into 0–255, palette positions into 0–255). A corrupt NVS must never produce an out-of-range LED count.

### 3.5 Global instance

`Config config;` — a single global, accessed by the web handlers. Guard writes with a simple mutex/flag if the web handler could run concurrently with `loop()`; with the synchronous `WebServer` the handler runs on the same task, so this is safe, but **avoid calling `config.save()` from inside an interrupt or while `FastLED.show()` is mid-flight**.

---

## 4. Runtime Gradient Palette

### 4.1 The problem

`DEFINE_GRADIENT_PALETTE` is a **preprocessor macro**. It emits a compile-time initialised gradient palette. It cannot be built from runtime values.

### 4.2 Solution

**Verified against FastLED 3.10.3 (the installed version):**

- `CRGBPalette16` holds only **16** entries (`CRGB entries[16]`). It is NOT a 256-entry LUT.
- `CRGBPalette256` holds **256** entries and exposes them as a **public** member: `CRGB entries[256]`.
- `ColorFromPalette(const CRGBPalette256 &pal, fl::u8 index, fl::u8 brightness = 255, TBlendType blendType = NOBLEND)` — accepts a 0–255 index. This is the overload to use.
- `lerp8by8(uint8_t a, uint8_t b, fract8 frac)` is in the **global** namespace (`lib8tion.h`, included by `FastLED.h`). `fract8` is `fl::u8` aliased into the global namespace — a fraction of 256, so `frac` runs 0–255.
- **There is no `lerp8` function.** Using `lerp8` will not compile. Use `lerp8by8`.

Because the admin panel must support **custom stop positions** (not just 4 evenly-spaced stops), FastLED's `fill_gradient_RGB()` helper is not sufficient — it places stops at fixed 0/N/2N/N-1. Hand-roll the LUT.

**Use `CRGBPalette256` and fill `entries[]` directly:**

```cpp
CRGBPalette256 buildPalette(const PaletteStop stops[4]) {
  CRGBPalette256 pal;
  for (int i = 0; i < 256; ++i) {
    uint8_t p = (uint8_t)i;

    // Find the bracketing pair of stops (stops are sorted ascending by pos).
    int hi = 0;
    while (hi < 3 && stops[hi + 1].pos <= p) ++hi;
    int lo = (hi == 0) ? 0 : hi - 1;

    int span = (int)stops[hi].pos - (int)stops[lo].pos;
    // frac is in 1/256ths: 0 = fully at lo, 255 = fully at hi.
    uint8_t frac = (span == 0) ? 0 : (uint8_t)(((int)p - (int)stops[lo].pos) * 255 / span);

    pal.entries[i] = CRGB(lerp8by8(stops[lo].r, stops[hi].r, frac),
                          lerp8by8(stops[lo].g, stops[hi].g, frac),
                          lerp8by8(stops[lo].b, stops[hi].b, frac));
  }
  return pal;
}
```

**Critical detail:** `frac` must be scaled to **0–255** (1/256ths), not 0–100. `lerp8by8` treats `frac` as a 256th. Passing a 0–100 fraction would badly skew every gradient.

**Requirements on the stop positions:** they must be sorted ascending. The web handler must **sort the four stops by `pos` after parsing** and fix duplicates. Clamp `pos` to 0–255.

### 4.3 Usage

Replace the macro palette with:

```cpp
CRGBPalette256 palette;   // global
// in setup(), after config.load():
palette = buildPalette(config.palette);
```

And in `updateLEDs()`, use `ColorFromPalette(palette, cIndex)` as the existing commented-out code already does (lines 242–252). `ColorFromPalette` with a `CRGBPalette256` and a `uint8_t` index selects the `CRGBPalette256` overload automatically — but if the compiler complains about ambiguity, cast the index to `uint8_t` explicitly.

### 4.4 LED count

`CRGB leds[NUM_LEDS]` is a fixed-size global array of 48. To support a runtime count:

**Recommended approach:** allocate the maximum at compile time and use only the first `config.numLeds`:

```cpp
#define MAX_LEDS 300
CRGB leds[MAX_LEDS];
```

`CRGB` is 3 bytes → `300 * 3 = 900` bytes of RAM. The ESP32 has ~320KB of usable heap/DRAM, so 900 bytes is negligible.

Then:

```cpp
FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds);
```

**Critical:** `FastLED.addLeds` must be re-called whenever `numLeds` changes, and the old LED data beyond the new count must be cleared. When the user saves a new LED count:
1. `FastLED.clear()` and `FastLED.show()` to blank the strip (otherwise leftover pixels remain lit on the physical strip beyond the new count — they cannot be turned off otherwise).
2. Re-call `FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds)`.
3. `FastLED.setBrightness(config.brightness)`.
4. `FastLED.show()`.

FastLED's `addLeds` sets the strip length; calling it again with a different count is supported.

### 4.5 Brightness

`FastLED.setBrightness(config.brightness)` — FastLED's `setBrightness` takes 0–255. Apply on boot and on every change.

---

## 5. Timezone Handling

### 5.1 The `sstaub/NTP` API (verified from source)

```cpp
void ruleDST(const char* tzName, int8_t week, int8_t wday, int8_t month, int8_t hour, int tzOffset);
void ruleSTD(const char* tzName, int8_t week, int8_t wday, int8_t month, int8_t hour, int tzOffset);
void timeZone(int8_t tzHours, int8_t tzMinutes = 0);
void isDST(bool dstZone);
```

Enums (from `NTP.h`):
```cpp
enum week_t  {Last, First, Second, Third, Fourth};   // 0..4
enum dow_t   {Sun, Mon, Tue, Wed, Thu, Fri, Sat};    // 0..6
enum month_t {Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec}; // 0..11
```

`tzOffset` is **minutes from UTC** (absolute), not a delta from the STD offset.
`tzName` is copied into `char tzName[6]` → **must be ≤ 5 characters**. Truncate on save/load.

### 5.2 Applying the config to the NTP object

```cpp
void applyTimezone(NTP& ntp, const Config& c) {
  if (!c.tzHasDst) {
    ntp.timeZone((int8_t)(c.tzStdOffset / 60), (int8_t)(c.tzStdOffset % 60));
    ntp.isDST(false);
  } else {
    ntp.ruleDST(c.tzDstName.c_str(), (int8_t)c.tzDstWeek, Sun, (int8_t)c.tzDstMonth,
                (int8_t)c.tzDstHour, c.tzDstOffset);
    ntp.ruleSTD(c.tzStdName.c_str(), (int8_t)c.tzStdWeek, Sun, (int8_t)c.tzStdMonth,
                (int8_t)c.tzStdHour, c.tzStdOffset);
  }
}
```

**Note:** `wday` is hardcoded to `Sun` because the config does not expose a day-of-week field. This matches the existing sketch's behaviour (both rules use `Sun`). If a day-of-week field is desired, add `tzDstDow`/`tzStdDow` keys — but that is optional and adds UI complexity; **default to `Sun` and keep it out of the first implementation.**

**Note on the offset semantics:** `timeZone(hours, minutes)` — for a negative offset (e.g. America/New_York at UTC-5) use `timeZone(-5, 0)`. The sign must be handled; do not use unsigned arithmetic when converting `tzStdOffset` (minutes) into hours/minutes. Compute `hours = tzStdOffset / 60` using signed division, and `minutes = tzStdOffset % 60` (which may be negative — pass it through as signed).

### 5.3 The `TZ_INFO` define

`#define TZ_INFO "Europe/London"` (line 24) is **unused dead code** — the NTP library uses POSIX `tm` rules, not IANA zone strings, and ESP32's `setenv("TZ", ...)` would require a POSIX tzdata not present on Arduino-ESP32 by default. **Delete the `#define`.** Do not attempt IANA timezone support.

### 5.4 When to apply

`applyTimezone()` must be called **before** `ntp.begin()`, and again whenever the user changes the timezone (then `ntp.update()` to refresh immediately).

---

## 6. Web Servers

### 6.1 Includes

```cpp
#include <WebServer.h>     // ESP32 core: "ESP32 Web Server"
#include <DNSServer.h>     // ESP32 core: "DNSServer"
```

Both ship with the ESP32 Arduino core (2.x and 3.x). **No extra library installation required.**

### 6.2 The `WebServer` instance

One global:

```cpp
WebServer server(80);
```

It serves on whichever IP is active. In AP mode that is `192.168.4.1`; in STA mode it is the LAN IP from `WiFi.localIP()`. **The same handler map works in both modes** — this is the key simplification that keeps the design small.

### 6.3 DNS server (captive portal)

```cpp
DNSServer dnsServer;
```

Started only in `SETUP_AP` / `CONNECT_STA` modes, stopped in `RUNNING`.

```cpp
dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
dnsServer.start(53, "*", WiFi.softAPIP());
```

`setErrorReplyCode(DNSReplyCode::NoError)` makes the server answer **all** A queries with the AP IP, so any hostname the phone tries resolves to us. This is what makes the captive portal pop up.

### 6.4 Captive portal detection URLs

Android, iOS/macOS and Windows all probe specific URLs on connect. Register handlers that redirect all of them to the setup page:

```cpp
const char* captiveUrls[] = {
  "/generate_204",        // Android
  "/gen_204",            // Android (alt)
  "/hotspot-detect.html", // iOS
  "/library/test/success.html", // iOS
  "/redirect",            // Windows
  "/ncsi.txt",            // Windows
  "/connecttest.txt",     // Windows
  "/success.txt",         // Windows
  "/canonical.html",      // Firefox
  "/success",             // Firefox
  "/default"              // Firefox
};
for (const char* u : captiveUrls) {
  server.on(u, HTTP_GET, [this]() { server.sendHeader("Location", "/setup", true); server.send(302, "text/plain", ""); });
}
```

Also register `/favicon.ico` to return `204 No Content` so the browser doesn't log a 404.

### 6.5 Handler map

| Path | Method | Purpose | Mode |
|---|---|---|---|
| `/` | GET | Redirect to `/setup` or `/admin` depending on mode | any |
| `/setup` | GET | WiFi provisioning form | AP |
| `/setup` | POST | Submit SSID/PSK → save → begin connect | AP |
| `/scan` | GET | JSON list of visible networks | AP |
| `/status` | GET | JSON device status (mode, IP, SSID, signal, last error) | any |
| `/admin` | GET | Main settings form (palette, LEDs, brightness, tz, station) | STA (also reachable in AP) |
| `/admin` | POST | Save all settings | STA (also reachable in AP) |
| `/admin/api` | POST | JSON partial update (used for live palette/brightness preview) | STA |
| `/factory-reset` | POST | Clear NVS and reboot | STA |
| captive probe URLs | GET | 302 → `/setup` | AP |

**Handler registration must be done once** in `setup()` — do not re-register on every mode change (the `WebServer` accumulates routes; repeated registration causes the first-registered handler to win and leaks memory).

### 6.6 Form parsing

Use `server.arg("name")` for POST form fields and `server.hasArg("name")` to check presence. For JSON bodies, use `server.arg("plain")` and `deserializeJson`. **Prefer `application/x-www-form-urlencoded` POSTs** for the main form — simpler to implement correctly and to debug. Use the JSON endpoint only for the live palette preview if desired (optional; can be dropped to reduce scope).

### 6.7 Response helpers

```cpp
String jsonEscape(const String& s);  // for embedding strings in JSON
```

Never interpolate user-supplied strings (SSID, PSK) into HTML without escaping. At minimum, strip `<`, `>`, `&`, `"`, `'` from any value echoed back into HTML.

---

## 7. WiFi Provisioning Flow

### 7.1 SoftAP details

```cpp
String apSsid = "TideTime-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);
WiFi.softAP(apSsid.c_str());
```

No password (open AP) so the user can connect without extra friction — this is standard for captive portals. **Open AP is a security consideration; note it in the README.** Optional: a fixed password derived from the MAC.

Default AP IP is `192.168.4.1` (do not change it).

### 7.2 Scanning for networks

```cpp
int n = WiFi.scanNetworks(false, true);   // async=false, show_hidden=true
```

Build a JSON array of `{ssid, rssi, secure}` and serve at `/scan`. **Note:** scanning temporarily disrupts the STA connection — do not call it while `RUNNING` in a way that breaks things; a scan while connected is generally fine on ESP32 but will briefly pause traffic.

Cap the returned list at ~20 entries to keep the response small.

### 7.3 Connect sequence

On `POST /setup`:

1. Read `ssid` and `psk` from the form. Validate: SSID non-empty and ≤ 32 chars; PSK ≤ 63 chars and either empty or 8–63 chars (WPA2) — 64-hex WEP keys are an edge case, accept 10 or 26 hex too.
2. If SSID matches the AP's own SSID, reject with an error (the user picked the device's own AP).
3. Save `ssid`/`psk` to NVS **immediately** (before connecting) so a power cut during connect doesn't lose the credentials.
4. Respond `200` with a small HTML page saying "Connecting…" **and** have the page poll `/status` every 1s.
5. Set a flag `pendingConnect = true`; the actual `WiFi.begin()` happens in `loop()` (see below), because doing a long blocking connect inside the HTTP handler will cause the browser's request to time out and the page will hang.

**Why connect in `loop()`:** the synchronous `WebServer` handler blocks the whole HTTP response until it returns. A 20-second `WiFi.begin()` inside the handler means the browser waits 20s and often shows a connection error. Instead: return immediately, and in `loop()` detect `pendingConnect` and drive the state machine.

### 7.4 The connect state machine in `loop()`

**On core 3.3.11 `WiFi.begin(ssid, psk)` is non-blocking by default** (`tryConnect = true`), so the HTTP server stays responsive throughout the connect attempt. This is the preferred path — use it.

```cpp
// on POST /setup:
pendingConnect = true;
connectDeadline = millis() + 20000;   // 20s budget
WiFi.begin(ssid.c_str(), psk.c_str());
mode = MODE_CONNECT_STA;
```

Then in `loop()`:

```cpp
if (mode == MODE_CONNECT_STA) {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    onConnected();                       // -> MODE_RUNNING
  } else if (millis() > connectDeadline) {
    // Failed. Record the reason and fall back to AP mode.
    lastError = wifiStatusToString(st);
    pendingConnect = false;
    mode = MODE_SETUP_AP;               // AP is still up, so the UI can show the error
  }
}
```

**The AP must stay up during `MODE_CONNECT_STA`** so the user can see progress and any error. Keep `WiFi.mode(WIFI_AP_STA)` for the whole connect attempt.

`WiFi.status()` returns: `WL_CONNECTED`, `WL_CONNECT_FAILED`, `WL_NO_SSID_AVAIL`, `WL_IDLE_STATUS`, `WL_NO_SHIELD`, `WL_CONNECTING`. Map these to short human-readable strings for the `/status` JSON `error` field.

**Do not** write a `while (WiFi.status() != WL_CONNECTED) delay(100);` loop inside the HTTP handler — it would block the response and the browser would show a timeout. The polling `/status` UI works precisely because the connect is non-blocking.

### 7.5 Status JSON

```json
{
  "mode": "SETUP_AP" | "CONNECT_STA" | "RUNNING",
  "ssid": "...",
  "ip": "192.168.4.1",
  "rssi": -55,
  "connecting": true|false,
  "error": "" 
}
```

### 7.6 Success flow

When the STA connects:
1. Switch to `WIFI_STA` mode (`WiFi.mode(WIFI_STA)`) — this tears down the softAP.
2. Stop the DNS server.
3. Log the new `WiFi.localIP()` to Serial and show it on the LED-strip-driven setup (see §11).
4. The `/status` poll returns `mode: RUNNING` and the new IP; the page then shows "Connected! Your device is at 192.168.x.x" and links to `/admin`.

**Gotcha:** calling `WiFi.mode(WIFI_STA)` while the AP is up kills the AP and the browser's connection to it drops. The `/status` request may fail. Handle this by having the JS treat a failed `/status` fetch as *possibly successful* and show "If the page disconnects, the device has connected — check your network at <ip>". Alternative: keep the AP up in RUNNING mode for 60 seconds (dual mode) then drop it. **Recommended: stay in `WIFI_AP_STA` for 30–60 seconds after a successful connect, then switch to `WIFI_STA`.** This gives the browser time to poll once more and display the LAN IP.

---

## 8. Admin Panel (`/admin`)

### 8.1 Settings exposed

1. **Gradient palette** — 4 stops. Each stop: a colour picker (`<input type="color">`, which yields `#rrggbb`) **and** a position slider (0–255). Plus the red/green/blue values in number inputs for precision.
2. **Number of LEDs** — number input, 1–300.
3. **Brightness** — range slider 0–255, with a live numeric readout.
4. **Timezone** — a group of fields (see §8.3).
5. **Station ID** — text input, sent to EasyTide.

### 8.2 Palette UI

Four rows, each: `[color input] [R] [G] [B] [position slider]`. Below, a live preview bar rendered with a CSS `linear-gradient` computed from the four stops — this gives immediate visual feedback without any ESP32 round-trip. Recompute the CSS on every input event (pure JS).

On submit, the browser sends the 4 colours and 4 positions as form fields:
`pal0r, pal0g, pal0b, pal0pos, pal1r, ... pal3pos`.

The handler parses them, **sorts by position**, clamps to 0–255, rebuilds the palette, and applies it live (`palette = buildPalette(...)` then `FastLED.show()`).

### 8.3 Timezone UI

| Field | Control | Notes |
|---|---|---|
| Enable DST | checkbox | `tzHasDst` |
| Standard time name | text (max 5 chars) | e.g. `GMT` |
| Daylight time name | text (max 5 chars) | e.g. `BST` |
| Standard offset (minutes from UTC) | number, -720..840 | e.g. `0` |
| Daylight offset (minutes from UTC) | number, -720..840 | e.g. `60` |
| DST start month | select Jan–Dec | |
| DST start week | select First/Second/Third/Fourth/Last | |
| DST start hour | number 0–23 | |
| STD start month | select Jan–Dec | |
| STD start week | select First/Second/Third/Fourth/Last | |
| STD start hour | number 0–23 | |

**Prefer minutes-from-UTC over "hours + minutes"** — it maps 1:1 onto the NTP library's `tzOffset` parameter and avoids sign/rounding bugs. Label it clearly ("UTC offset in minutes, e.g. 0 for GMT, -300 for EST").

**Day-of-week is fixed to Sunday** for both rules (matching the current sketch). Do not expose it.

### 8.4 Station ID UI

A text input, default `0546`. On save, the value is used to build the EasyTide URL:

```cpp
String url = "https://easytide.admiralty.co.uk/Home/GetPredictionData?stationId=" + stationId;
```

**Validate**: trim whitespace; reject empty; reject if it contains characters outside `[A-Za-z0-9-]` (it goes into a URL). Length ≤ 32.

The URL must be built with the stored value — replace the hardcoded string at line 126.

**Note on the HTTPS URL:** the current code calls `http.begin("https://...")` **without a CA certificate**. The commented-out `ca` certificate at lines 48–77 is for a different site (`howsmyssl.com`) and is expired. On Arduino-ESP32 2.x, `HTTPClient::begin(url)` for an `https://` URL will fail TLS verification and return a negative error code — **unless** `WiFiClientSecure::setInsecure()` is used. The sketch currently ignores the error and `currentTideEvents` stays empty.

**Action required:** the implementer must switch to `WiFiClientSecure` + `setInsecure()` (or install a valid CA bundle) so the EasyTide fetch actually works. This is a pre-existing bug, but it directly affects whether the station ID setting has any visible effect, so it must be fixed as part of this work.

```cpp
WiFiClientSecure client;
client.setInsecure();          // or client.setCACert(MODERN_CA);  <- preferred
HTTPClient http;
http.begin(client, url);
```

Also add a `User-Agent` header — some servers reject requests without one.

### 8.5 Apply-without-reboot

The plan should make **every** admin setting apply immediately, without a reboot:

| Change | Action |
|---|---|
| Palette | Rebuild `palette`, `FastLED.show()` |
| Brightness | `FastLED.setBrightness()`, `FastLED.show()` |
| LED count | `FastLED.clear(); FastLED.show(); FastLED.addLeds<...>(leds, n); FastLED.show();` |
| Timezone | `applyTimezone(ntp, config)`, `ntp.update()` |
| Station ID | Re-fetch tide data: `setCurrentTideEvents()` |
| SSID/PSK | **Requires** a reconnect — handled separately (§7) |

### 8.6 Factory reset

`POST /factory-reset` → `prefs.clear()` → `ESP.restart()`. The device then comes up with no SSID and falls back to `SETUP_AP`. This is the escape hatch for "I changed my wifi password and the device is bricked".

Also provide a **physical fallback**: on boot, if the device has been in `SETUP_AP` mode for a long time... no, better — add a documented reset procedure: hold the button on the ESP32 during power-on to trigger a factory reset. If the board has no button available, skip. (Check the board; the sketch doesn't reference any GPIO inputs, so a button is optional. **Do not add GPIO dependencies the hardware may not have.**)

---

## 9. File Layout

The plan should **restructure into multiple files** so each file is small and a cheaper model can work on one at a time.

**Verified constraint: this is an Arduino sketch build** (no `platformio.ini`, no `.github/workflows`, main file `TideTime.ino` in a folder named `TideTime/`). The Arduino builder compiles **all** `.ino`/`.cpp`/`.h` files in the sketch folder together. Therefore:

- All new files go **directly in the `TideTime/` folder** — flat, no `src/`, no `lib/`.
- **Do not** create a `src/` subdirectory and **do not** rename `TideTime.ino`.
- `.ino` files are auto-preprocessed (function prototypes inserted), but `.cpp` files are **not**. To keep it simple and predictable, **write everything as `.h` + `.cpp` pairs** (with include guards) rather than adding new `.ino` files. `TideTime.ino` remains the only `.ino`.
- Declare in the header, define in the `.cpp`. Include the header from `TideTime.ino`.

```
TideTime/
├── TideTime.ino        // main: setup(), loop(), mode state machine
├── Config.h            // Config struct, PaletteStop, NVS load/save declarations
├── Config.cpp          // Config::load(), Config::save()
├── Palette.h           // buildPalette(), applyPalette() declarations
├── Palette.cpp         // buildPalette() implementation
├── Provisioning.h      // AP/DNS/connect state machine declarations
├── Provisioning.cpp    // AP start/stop, DNS, connect attempt
├── WebUi.h             // route registration + handler declarations
├── WebUi.cpp           // handlers, PROGMEM HTML/JS
└── README.md           // usage, AP SSID, how to reset
```

### 9.1 HTML delivery

Serve HTML from `PROGMEM` string literals to save RAM:

```cpp
static const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>...
)rawliteral";
```

Use `server.send_P(200, "text/html", SETUP_HTML)` to send PROGMEM content directly (the `send_P` overload exists in core 3.3.11).

For larger pages (the admin form is big), consider serving from a `LittleFS`/`SPIFFS` partition — **but** this adds a filesystem partition requirement and flashing complexity. **Recommendation: keep everything in PROGMEM for v1.** The admin form HTML is maybe 8–12 KB of source, which is acceptable.

---

## 10. Refactoring `TideTime.ino`

The main sketch must be changed to:

1. **Remove** `#define TZ_INFO`, `#define NUM_LEDS`, `#define OCEAN/CYAN/YELLOW/SAND`, `DEFINE_GRADIENT_PALETTE(pal_gp)`, and the hardcoded `wifiMulti.addAP(...)`.
2. **Add** `#define MAX_LEDS 300` and `CRGB leds[MAX_LEDS];`.
3. **Replace** the `WiFiMulti` usage with the single `Config`-driven AP/STA flow. `WiFiMulti` is no longer needed — it exists to try several APs; we now have exactly one stored SSID. **Remove the `WiFiMulti` include.**
4. **Replace** the hardcoded EasyTide URL with the station-ID-driven URL.
5. **Call** `config.load()` early in `setup()`, then `applyTimezone(ntp, config)`, `ntp.begin()`, `FastLED.addLeds<WS2812,2,GRB>(leds, config.numLeds)`, `FastLED.setBrightness(config.brightness)`, `palette = buildPalette(config.palette)`.
6. **Keep** `setCurrentTideEvents()`, `getPreviousAndNextEvent()`, `parse8601()`, `updateLEDs()` and the `TideEvent`/`MaybeTideEventPair` types — they are working. Only the URL and the TLS client change.
7. **Replace** the `wifiMulti.run()` wait loop with the provisioning state machine.

### 10.1 Known bugs to preserve or fix

Note these in the plan but mark them as **out of scope unless trivially fixable**, so the sub-agent doesn't get distracted:

- `loop()` calls `currentTideEvents.value()` on an empty optional if the tide fetch fails (line 265–266) → **crash risk**. Guard with `if (currentTideEvents.has_value())`. **Fix this** — it's a one-line guard and prevents a reboot loop.
- `updateLEDs()` currently animates a `fill` counter rather than using the tide progress. The real logic (`eased`, `sea_fill`) is commented out. **Leave the display logic alone** — the task is the admin panel. Just make sure it compiles and runs with a runtime palette and runtime LED count.
- The heavy `Serial.println` calls in `updateLEDs()` will slow the loop. Leave them, or gate behind a `DEBUG` flag. Recommend gating behind `#define DEBUG_SERIAL 1`.
- `parse8601` uses `std::chrono::parse("%FT%T", tp)` on a string like `"2026-11-10T05:15:58.333"` — the fractional seconds are not consumed by `%T`. This works but leaves `.333` unparsed. Not a problem for the existing use, but note it.

### 10.2 `ntp.update()` semantics

`NTP::update()` returns `true` only when a new update was received. The current `loop()` uses this to trigger the tide fetch once. With the new flow:

```cpp
if (ntp.update()) {
  if (!calculatedTideTime) { ... }
}
```

But `ntp.update()` also returns `false` when the first update fails (e.g. no network yet). The tide fetch should be retried periodically (e.g. every 15 minutes) rather than once, because EasyTide data expires (the service gives 7 days) and the device may boot before the network is ready.

**Recommend:** fetch tide data on successful NTP update, then re-fetch every 15 minutes (or every 6 hours — the data is daily). Also re-fetch immediately when the station ID changes.

---

## 11. LED Strip as a Setup Indicator

Nice-to-have, low cost, and it makes the device usable without a serial monitor. **Optional — include if time allows.**

- `SETUP_AP`: blink **blue** at 1 Hz.
- `CONNECT_STA`: blink **amber/orange** fast (2 Hz).
- `RUNNING`: the normal tide display.

Implement as a `mode` check at the top of `updateLEDs()`.

---

## 12. Security Considerations

| Issue | Mitigation |
|---|---|
| Open SoftAP | Anyone nearby can connect and change settings. Consider a password derived from the MAC (e.g. last 8 hex chars). Make it configurable via a compile-time flag. |
| WiFi PSK stored in plaintext NVS | Unavoidable with `Preferences`; NVS is not encrypted by default. Note it. (ESP32 NVS encryption requires IDF-level flash encryption — out of scope.) |
| No CSRF protection on the admin form | The admin panel is only reachable on the local network. Acceptable. Add a simple session token if desired. |
| Admin panel reachable from the internet | Not exposed — no port forwarding. Ensure no MDNS/UPnP is enabled. |
| HTML injection via SSID/PSK echo | Escape all values before echoing into HTML. |
| DNS server open while AP is up | Only while AP is up. Fine. |
| Factory reset endpoint | Restrict to POST and require the device to be reachable on the LAN. |

---

## 13. Implementation Phases (ordered, each independently testable)

**Phase 1 — Storage (no UI)**
- Create `Config.h`/`Config.cpp` with the struct, `load()`, `save()`.
- Add `load()`/`save()` calls to `setup()` with temporary `Serial` output of every loaded value.
- **Test:** flash, reboot, confirm values persist and defaults appear on first run. Confirm the LED count and brightness still compile-time-driven.

**Phase 2 — Runtime palette + runtime LED count**
- Implement `buildPalette()`.
- Switch to `MAX_LEDS` + `config.numLeds`.
- Apply `config.brightness`.
- **Test:** set palette and LED count in `Serial` temporarily (or hardcode in `load()`), confirm the strip renders the gradient and the correct pixel count.

**Phase 3 — Timezone + station ID from config**
- Implement `applyTimezone()`.
- Replace the EasyTide URL with the station-ID-driven URL; fix TLS with `WiFiClientSecure`.
- **Test:** confirm `ntp.formattedTime` shows the right local time and the tide fetch succeeds.

**Phase 4 — Web server, provisioning AP, captive portal**
- Implement `Provisioning` (AP start/stop, DNS, connect state machine).
- Implement `/setup` + `/scan` + `/status`.
- **Test:** erase NVS, reboot, connect to the AP from a phone, see the captive portal, scan, submit a known-good SSID, watch it connect.

**Phase 5 — Admin panel**
- Implement `/admin` GET/POST with all five setting groups, live palette preview in CSS, apply-without-reboot.
- **Test:** change every setting, confirm it applies and persists across a reboot.

**Phase 6 — Polish**
- Factory reset, serial logging, LED mode indicator, README.

---

## 14. Verification Checklist

- [ ] Fresh device (no NVS) boots into AP mode and the SSID `TideTime-XXXX` is visible.
- [ ] Connecting to the AP opens the setup page automatically (Android + iOS).
- [ ] `/scan` lists nearby networks with signal strength.
- [ ] Submitting valid credentials connects the device and it reboots into `RUNNING` mode with the LAN IP shown.
- [ ] Submitting incorrect credentials shows an error and stays in AP mode (no brick).
- [ ] The admin panel is reachable at `http://<lan-ip>/` after connecting.
- [ ] Changing the palette updates the strip **immediately**, without a reboot.
- [ ] Changing the LED count updates the strip immediately and does not leave stale lit pixels.
- [ ] Changing the brightness updates immediately.
- [ ] Changing the timezone updates the displayed local time immediately.
- [ ] Changing the station ID causes a re-fetch, and the tide data reflects the new station.
- [ ] All settings survive a power cycle.
- [ ] Factory reset returns the device to AP mode.
- [ ] The device does not crash when the tide fetch fails (no `optional` deref without a check).
- [ ] The device does not crash when NVS contains corrupt/out-of-range values.

---

## 15. Risks and Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| `WiFi.mode()` transitions drop the browser's connection mid-flow | High | Keep AP up for 60s after a successful connect before switching to `WIFI_STA`. |
| Core version differences in `WiFi.begin()` non-blocking semantics | Medium | Check the installed core version first; keep a blocking fallback path behind a `#if`. |
| `WiFiClientSecure` TLS failure on EasyTide | High (pre-existing) | Use `setInsecure()` as a fallback; prefer a current CA bundle. |
| `Preferences` write not flushed | Medium | `end()` + `begin()` after each bulk save. |
| `MAX_LEDS` 300 consumes RAM | Low | 900 bytes; negligible on ESP32. |
| `DEFINE_GRADIENT_PALETTE` removal breaks `ColorFromPalette` usage | Low | `CRGBPalette16` is still used; only the construction changes. |
| Web UI code is the largest chunk of work | High | Phase it (Phase 5 last), keep HTML in PROGMEM, no framework. |
| Sub-agent burns time on UI polish | Medium | The plan fixes the exact field names, ranges, and behaviours. |

---

## 16. Out of Scope (v1)

- Multiple stored wifi networks with a chooser.
- IANA timezone strings (e.g. `Europe/London`) — requires POSIX tzdata, not available.
- HTTPS on the admin panel (the device is on a LAN; use HTTP).
- OTA updates.
- A scheduler / "modes" feature (e.g. different palettes for different tide states).
- A mobile app — the web UI is mobile-first and responsive by design.
- Storing settings in a cloud service.

---

## 17. Implementation Record (post-implementation notes)

The plan above was executed in two phases (Phase A: core modules; Phase B: web UI +
integration) and **compiles clean** for `esp32:esp32:esp32` on core 3.3.11:
**1,143,308 bytes (87% of the 1.31 MB app partition), 53,436 bytes RAM (16%), zero errors.**

### 17.1 Deviation: `parse8601` rewritten to fit the flash partition

**This is the one substantive deviation from the original code and it is load-bearing.**

The original `parse8601()` used `std::istringstream` + `std::chrono::parse("%FT%T", tp)`.
That pulls in libstdc++ iostream/locale (~237 KB of flash). Measured empirically:

| Variant | Program storage | Result |
|---|---|---|
| Original iostream-based `parse8601` | 1,380,848 bytes (**105%**) | **Build FAILS** — "text section exceeds available space in board" |
| Rewritten `sscanf` + header-only chrono | 1,143,308 bytes (87%) | Compiles clean |

TLS (`WiFiClientSecure`) + `WebServer` together already added ~250 KB, which pushed the
original over the 1.2 MB default app partition. The rewrite preserves the **exact
observable semantics** — the `.333` millisecond fraction is still left unread (same as
`%T`), and a parse failure still yields epoch 0.

**Implication for the future:** there is only ~167 KB of flash headroom left. Adding
anything substantial (a filesystem, more libraries, richer HTML) will require either a
larger partition scheme (`PartitionScheme=min_spiffs` gives 1.9 MB APP, or `no_fs` gives
2 MB APP x2) or dropping TLS. **This constrains future work and should be recorded
against the project.**

### 17.2 Deviations (non-breaking)

- `/admin` GET is served via a token-substituted `String` + `server.send()` rather than
  `send_P()`, because the page must be pre-filled from live config. `ADMIN_HTML` still
  lives in PROGMEM. `SETUP_HTML` uses `send_P()` exactly as specified.
- The station-change tide re-fetch is deferred via a `tideRefetchRequested` flag consumed
  by `loop()`, not performed inside the HTTP handler — a synchronous TLS fetch would
  freeze the web server for seconds and time the browser out.
- A 1-minute retry backoff was added for failed tide fetches (the plan only specified the
  6-hour refetch; without backoff a failure would hammer EasyTide every second).
- The `OCEAN`/`CYAN`/`YELLOW`/`SAND` macros were removed. Sea and sand colours now come
  from `ColorFromPalette(palette, 0)` / `(palette, 255)` — pixel-identical with the
  default palette, but they now respond to user palette edits.
- Debug `Serial` prints are gated behind `#define DEBUG_SERIAL 0`.
- Factory reset adds a 500 ms delay between `server.send()` and `ESP.restart()` so the
  response flushes before the reboot.

### 17.3 Known bug: negative UTC offsets with a non-zero minutes remainder

`NTP::timeZone(tzHours, tzMinutes)` (NTP library, `NTP.cpp:300-308`) computes
`offset = tzHours * 3600` and **subtracts** the minutes when `tzHours < 0`, assuming
minutes are 0–59. `applyTimezone()` passes truncated division, so with `tzHasDst == false`
and `tzStdOffset = -305` it passes `(-5, -5)`, yielding `-18000 - (-300) = -17700 s`
= **-295 minutes — 10 minutes out**. Same class of bug for any negative offset with a
non-zero minutes remainder (`-315` → `-285 min`, 30 min out).

- Whole-hour negative offsets (`-300` → `-5, 0`) are **correct**.
- All positive offsets are **correct** (`+330` → `+5, +30`).
- The **DST-enabled path is unaffected** — `ruleDST`/`ruleSTD` take absolute minutes
  directly, which is the common case for UK/EU users.

**Fix if desired:** in `applyTimezone()`, normalise negative offsets so the minutes
remainder is non-negative, e.g. `hours = offset / 60; mins = offset % 60; if (mins < 0)
{ mins += 60; hours -= 1; }` and pass the absolute value of `mins`. Not done here because
Phase A code was deliberately left untouched during Phase B.

### 17.4 Bug found and fixed: web server started before lwIP was initialised

**Symptom (from the serial log):** the device booted, printed the loaded settings, then
immediately hit `assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))` and rebooted
in a loop.

**Root cause (confirmed by decoding the device backtrace against the ELF symbol table).**

The decoded backtrace was:

```
setup()
  → WebServer::begin()
    → NetworkServer::begin(port)
      → lwip_socket()
        → netconn_new_with_proto_and_callback()
          → tcpip_send_msg_wait_sem()      ← xQueueSemaphoreTake on a NULL queue
            → panic_abort
```

`WebServer::begin()` opens a listening socket via `lwip_socket()`, which requires the
**lwIP TCP/IP thread to be running**. That thread is started by `esp_netif_init()`, which the
Arduino core only calls from `NetworkManager::begin()`. The core's `initArduino()` does *not*
call it, and `WiFiGenericClass::mode()` only reaches it via `wifiLowLevelInit()` **the first
time the radio mode actually changes**.

The rewritten `setup()` called `server.begin()` *before* `startAccessPoint()` /
`beginWifiConnect()`, so no network stack existed yet and the socket creation asserted.

**The fix:**

1. Added `initNetwork()` to `Provisioning`, which calls `Network.begin()`
   (`NetworkManager::begin()`) explicitly. It is idempotent — `NetworkManager::begin()`
   guards with a static flag — and it guarantees `esp_netif_init()` has run before any
   socket is created.
2. `setup()` now calls `initNetwork()` **before** `startAccessPoint()` / `beginWifiConnect()`
   and before `server.begin()`.

**Verified at the machine-code level.** Disassembling the compiled ELF shows `setup()` now
calls, in program order:

```
Config::load()  →  applyTimezone()  →  buildPalette()
  →  initNetwork()        (→ NetworkManager::begin() → esp_netif_init())
  →  startAccessPoint()   (→ WiFi.mode())
  →  setupRoutes()
  →  WebServer::begin()   ← now strictly after lwIP is up
```

**Note on the earlier misdiagnosis.** The first attempt at this fix concluded that `ntp.begin()`
was the culprit, because it was the last thing logged before the crash. That was wrong — the
crash was in `server.begin()`, which runs *after* the settings print but produces no log
output of its own. The NTP ordering change it introduced was still correct on its own merits
(see below), but it was not the fix for this assert. **The backtrace should have been decoded
first.**

### 17.4a Related change: NTP client start ordering

Independently of the above, deferring `ntp.begin()` until the network is up is correct and
was retained:

- `ntp.begin()` moved out of `setup()` into `startNtp()`, called from `pollWifiConnect()`
  the first time `WiFi.status()` reports `WL_CONNECTED`. The `sstaub/NTP` library's
  `init()` opens a UDP socket and *immediately* attempts a sync; with no active interface
  the `bind()` fails, leaving a dead socket, and `NTP::init()` ignores `begin()`'s return
  value and calls `ntpUpdate()` on it anyway.
- A global `bool ntpStarted` gates `ntp.update()` in the main loop, because
  `NTP::update()` retries the sync on *every* call until one succeeds — each retry against a
  never-bound socket would crash. The admin panel's timezone-change handler is guarded the
  same way, since the admin page is reachable in AP mode where the client has not started.
- In `MODE_RUNNING` the link is now monitored. If it drops, the NTP client is stopped
  (`ntp.stop()`) *before* the interface disappears from under its socket, `ntpStarted` is
  cleared, and a STA-only reconnect is attempted.

New API in `Provisioning.h`:

```cpp
extern bool ntpStarted;
void startNtp();      // idempotent; calls applyTimezone() then ntp.begin()
bool ntpHasSynced();  // ntpStarted && ntp.epoch() > 1000000000
```

### 17.5 Timezone UI simplification (zone dropdown)

**Question raised:** the timezone section was complex - does it need offsets if a timezone
is selected?

**Answer: yes, the offsets are still needed in the data model, but not in the UI.**

- The ESP32 ships **no tzdata**, so IANA names (`Europe/London`) cannot be resolved. The
  C library's `tzset()` needs either a zoneinfo file on the filesystem or a POSIX `TZ`
  string; neither is present. ESP-IDF's own docs describe the POSIX form
  (`WET0WEST,M3.5.0/1,M10.5.0`), not IANA names.
- The `sstaub/NTP` library **never calls `setenv`/`tzset`**. It computes the UTC offset
  purely from its own `ruleDST()`/`ruleSTD()` parameters
  (`NTP.cpp:335-341`: `local = utcCurrent + dstOffset + timezoneOffset`).

So a "pick a timezone" control can only work by shipping a table that maps a friendly
name to the four values the library understands. That is what `TimeZones.h/.cpp` does -
62 common zones, each carrying the standard/daylight offsets and the DST rule dates.

**Implementation:**
- `TimeZones.h/.cpp`: the table plus `timeZoneCount()`, `timeZoneAt()`,
  `timeZoneMatch()`, `timeZoneDefaultIndex()`.
- New endpoint `GET /admin/tz` serves the table as JSON.
- The admin page gets a **Time zone** dropdown. Selecting a zone fills in every advanced
  field and shows a summary line (e.g. `UTC+0 (GMT / BST, daylight saving to UTC+1)`).
- A **"Custom (see advanced settings below)"** option appears when the values do not
  match any known zone, and the manual fields live in a collapsed `<details>` block.
- The admin POST handler reads `tzZone`: a valid index uses the table's values, `custom`
  or a missing/invalid index falls back to the manual fields.

**Bugs found and fixed while testing on the device:**
1. The rewritten timezone block dropped the `id` attributes from six inputs
   (`tzDstHour`, `tzStdHour`, `tzStdName`, `tzDstName`, `tzStdOffset`, `tzDstOffset`).
   The JS looks them up by id, so `getElementById(...)` returned null and the script threw
   `TypeError: Cannot set properties of null`, which stopped the zone list from loading at
   all. Restored the ids and made every JS lookup null-safe.
2. The `$()` helper was defined *after* the timezone script used it, so the timezone code
   ran before it existed. Moved the definition to the top of the script.
3. `updateTzSummary()` appended a duplicate "Custom" option on every call. Now it checks
   whether the option already exists.
4. `updateTzSummary()` ran before the zone list had loaded, marking everything
   "custom". Now it returns early while the list is empty.

**Verification of the DST rules.** The library's `week_t` enum's `Last` (0) was checked
against the real device: a test sketch embedding the library's `calcDateDST()` verbatim
showed it resolves to the true last Sunday in both 4-Sunday and 5-Sunday months
(March 2026 -> 29th, March 2027 -> 28th, March 2028 -> 26th). The US, Australian, New
Zealand and Chilean rules were verified against the tzdata source (`tzdata/europe`).

**Verified on the device:** the dropdown loads 62 zones, auto-selects the zone matching
the saved settings, applying a zone updates every field and the summary, manual edits
switch to "Custom", and the choice survives a page reload (saved to NVS).

### 17.7 Hardware verification (device present)

The device was flashed and exercised directly (COM7, CP210x). Confirmed working:

- Boots without the lwIP assert; `initNetwork()` logs
  `lwIP network stack initialised`.
- Joins the stored network, gets a LAN IP, and the NTP client starts only after the
  connection (`[NTP] Starting NTP client (network is up)`).
- The EasyTide TLS fetch now **succeeds** and tide events parse
  (`[Tide] events set:` followed by two epoch timestamps) - this fixes the pre-existing
  bug where the request used a plain `HTTPClient` with no CA.
- The admin page loads, the timezone dropdown populates from `/admin/tz`, the saved zone
  is auto-selected, changing zones updates every field and the summary, and the choice
  survives a reload (NVS round-trip confirmed).
- The NTP library's `Last` week semantics were confirmed on-device against the real
  `calcDateDST()` for 2026-2028.

Still not verified on hardware: the physical LED strip output and colours, the
captive-portal auto-popup on Android/iOS/Windows, `/scan` behaviour while the AP is up,
and NVS persistence across a genuine power cycle (only a page reload was tested).

### 17.8 Build command

```powershell
& "C:\Users\schte\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --clean --fqbn esp32:esp32:esp32 --warnings all "C:\Users\schte\Documents\Arduino\TideTime\TideTime"
```

The only warnings are two `-Wtype-limits` warnings **inside FastLED 3.10.3's own source**
(`fl/corkscrew.cpp`, a 2D-matrix feature this project does not use). They appear on any
build of any sketch using FastLED and cannot be removed without editing the third-party
library.
