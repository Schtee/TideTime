# TideTime

A tide-height LED strip clock for the ESP32. Fetches tidal predictions from the
ADMIRALTY EasyTide service and renders them on a WS2812 LED strip.

The device ships with a **web admin panel**: on first boot it starts its own WiFi access
point so you can configure it from your phone without any special tooling.

---

## Getting started

### 1. First boot

On a fresh device (no saved settings) the ESP32 starts an access point:

| | |
|---|---|
| **SSID** | `TideTime-XXXX` (last 4 hex digits of the MAC) |
| **Password** | none — the network is open |
| **Address** | `http://192.168.4.1` |

Connect your phone to it. Most phones will pop up the setup page automatically
(captive portal). If not, open `http://192.168.4.1` manually.

### 2. Connect to your WiFi

The setup page scans for nearby networks as soon as it loads and shows them in a
dropdown, each with a signal strength rating and whether it is secured:

```
HomeNetwork  - Excellent (-42 dBm) [secured]
TNCAPCE0FAA  - Good (-58 dBm) [secured]
Cafe Guest   - Fair (-71 dBm) [open]
```

Pick one, enter the password, and submit. **Leave the password empty for an open
network.**

If your network isn't listed — a hidden SSID, or one the device can't see — choose
**"Other... enter network name manually"** and type the name yourself.

Press **Rescan for networks** to refresh the list.

Submit, and the device joins your network. The page polls for status and shows you the
IP address it got. **Note that the access point stays up for 60 seconds after connecting**
so the page can display the result — after that the AP shuts down and the device is only
reachable on your LAN.

Once the device is on your network, visiting `/` takes you to the admin panel, and
`/setup` redirects there too. The setup page only appears while the device is running
its own access point.

If the connection fails, the device falls back to access point mode and shows the error
(e.g. "Network not found"). You can correct the details and try again.

### 3. Admin panel

Once connected, the admin panel is at **`http://<your-device-ip>/`**. Settings you can
change, all applied immediately without a reboot:

| Setting | Notes |
|---|---|
| **Gradient palette** | 4 stops. Each has a colour picker, R/G/B inputs and a position slider (0–255). A live preview bar shows the gradient. |
| **Number of LEDs** | 1–300. |
| **Brightness** | 0–255. |
| **Timezone** | DST on/off, standard/daylight names, UTC offsets, and the start dates for each. |
| **Station ID** | The EasyTide station code, e.g. `0546`. Changing it re-fetches the tide data. |

All settings are stored in NVS (flash) and survive a power cycle.

---

## Timezone configuration

Pick your country or region from the **Time zone** dropdown. The device has a built-in
table of ~60 common zones; choosing one fills in the UTC offset and the daylight saving
rules automatically, and the summary line underneath shows what was applied:

```
UTC+0 (GMT / BST, daylight saving to UTC+1)
```

If your zone isn't listed, open **"Advanced - set the offset and daylight saving rules
manually"** and enter the values yourself. The dropdown then switches to "Custom".

### Why a dropdown rather than IANA names

The ESP32 ships no timezone database, so names like `Europe/London` cannot be resolved —
the C library needs either a zoneinfo file on the filesystem or a POSIX `TZ` string, and
neither is available on the device. The NTP library used here also ignores POSIX `TZ`
strings entirely: it computes the UTC offset from its own rule parameters.

So the device ships its own table, mapping a friendly name to the values the clock
actually uses: the standard and daylight UTC offsets, and the month/week/hour at which
each starts.

**Note on the offsets:** they are **minutes from UTC** — `0` for GMT, `60` for BST,
`-300` for EST, `330` for IST. This maps directly onto the underlying NTP library's
parameters and avoids ambiguity.

**Note on the day of week:** both daylight saving transitions use **Sunday**, matching
the behaviour of the original sketch. (UK and EU rules are all "last Sunday in March /
October", and the US rule is "2nd Sunday in March / 1st Sunday in November", so Sunday
is correct for all of them.)

### Rules used by the built-in zones

| Region | Daylight starts | Standard resumes |
|---|---|---|
| UK, Ireland, Portugal | last Sunday in March, 01:00 | last Sunday in October, 02:00 |
| Central Europe (CET) | last Sunday in March, 01:00 | last Sunday in October, 02:00 |
| Eastern Europe (EET) | last Sunday in March, 01:00 | last Sunday in October, 02:00 |
| USA, Canada | 2nd Sunday in March, 02:00 | 1st Sunday in November, 02:00 |
| Australia | 1st Sunday in October, 02:00 | 1st Sunday in April, 03:00 |
| New Zealand | last Sunday in September, 02:00 | 1st Sunday in April, 03:00 |

The "last Sunday" rules are handled correctly by the NTP library in both 4-Sunday and
5-Sunday months — verified against the device for 2026–2028.

---

## Factory reset

Press **Factory reset** at the bottom of the admin panel. This clears all saved settings
and reboots the device into access point mode.

If the device is unreachable (for example you changed your WiFi password), you can also
erase it from the Arduino IDE: **Tools → Erase All Flash**, then re-upload.

---

## Building

The project is a standard Arduino sketch. The main file must remain `TideTime.ino` in a
folder named `TideTime/`, and all other files live in that same folder.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 "TideTime"
arduino-cli upload  --fqbn esp32:esp32:esp32 -p <PORT> "TideTime"
```

### Required libraries

- **FastLED** 3.10.3 (Library Manager)
- **ArduinoJson** 7.x (Library Manager)
- **NTP** by sstaub (Library Manager) — for NTP time sync with timezone/DST rules
- `WebServer`, `DNSServer` and `Preferences` ship with the ESP32 Arduino core

### Flash usage

The sketch uses **1,143,308 bytes (87%)** of the default 1.2 MB app partition. There is
roughly **167 KB of headroom left**.

If you need more room, switch the board's partition scheme in the Arduino IDE under
**Tools → Partition Scheme** — `Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)` gives
comfortable extra space. Alternatively `No FS 4MB (2MB APP x2)` gives 2 MB.

---

## Architecture

| File | Responsibility |
|---|---|
| `TideTime.ino` | `setup()`/`loop()`, tide fetch, LED rendering |
| `Config.h/.cpp` | Settings struct + NVS (`Preferences`) persistence |
| `Palette.h/.cpp` | Runtime gradient palette (`CRGBPalette256`) |
| `Provisioning.h/.cpp` | AP bring-up, captive-portal DNS, WiFi connect state machine, timezone rules |
| `TimeZones.h/.cpp` | Built-in table of common timezones (offset + DST rules) |
| `WebUi.h/.cpp` | HTTP routes, handlers, HTML/CSS/JS |

### Operating modes

```
BOOT ──no saved wifi──► SETUP_AP ──credentials──► CONNECT_STA ──connected──► RUNNING
  │                                                   │                        │
  └──saved wifi──► CONNECT_STA ──failed/timeout──► SETUP_AP                   │
```

- **SETUP_AP** — access point and captive-portal DNS are up; waiting for credentials.
- **CONNECT_STA** — access point is still up while the device joins your network.
- **RUNNING** — connected; the access point is shut down.

### Notes on the codebase

- The gradient palette is built at runtime into a `CRGBPalette256` (256 entries), so
  palette colours and stop positions are user-configurable.
- `parse8601()` deliberately avoids `std::chrono::parse` / iostreams — they cost ~237 KB
  of flash, which overflows the default app partition. It uses `sscanf` plus header-only
  `std::chrono` calendar arithmetic instead.
- The EasyTide request uses `WiFiClientSecure` with `setInsecure()`, because the original
  code called `http.begin("https://...")` without a CA certificate and therefore never
  completed a TLS handshake.

See `PLAN.md` for the full design rationale and the implementation record.

---

## Troubleshooting

**The phone doesn't show a captive portal popup.** Open `http://192.168.4.1` manually.
Some devices bypass the DNS redirect (notably Android with a hardcoded DNS resolver).

**The scan finds no networks.** Scanning briefly interrupts the radio. Wait a few seconds
and press **Rescan for networks**. If your network is still missing, choose
**"Other... enter network name manually"** and type the SSID yourself.

**"Network not found".** The SSID was not visible to the device. Check for typos, and note
that some routers hide the SSID band or the device may be unable to see 5 GHz-only
networks — the ESP32 is 2.4 GHz only.

**The tide data never arrives.** Check the serial output at 115200 baud for
`[Tide] GET failed`. The device needs working DNS and internet access. The EasyTide
service provides roughly 7 days of predictions.

**Settings didn't save.** The device must be able to write to NVS. If the admin page
returns an error, check the serial log.

**The device reboots with `assert failed: xQueueSemaphoreTake`.** This means a socket was
opened before the lwIP network stack was up. On a normal boot the log shows:

```
[Provisioning] lwIP network stack initialised
[Provisioning] AP up: TideTime-XXXX at 192.168.4.1
[SETUP] HTTP server listening on port 80
```

If the "lwIP network stack initialised" line is missing, the ordering is broken. The web
server and the NTP client both need that line to have appeared first.
