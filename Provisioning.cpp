#include "Provisioning.h"

DeviceMode deviceMode = MODE_SETUP_AP;
String lastError;

DNSServer dnsServer;

static const unsigned long CONNECT_TIMEOUT_MS = 20000;  // 20 s connect budget
static const unsigned long AP_GRACE_MS = 60000;         // keep AP up 60 s after connect

// State for the connect state machine (only meaningful while
// deviceMode == MODE_CONNECT_STA). millis()-subtraction is rollover-safe.
static unsigned long connectStart = 0;  // when the current connect attempt started
static unsigned long connectedAt = 0;   // 0 = not yet connected; else grace-period start
static bool runningLinkLost = false;    // set when the link drops while MODE_RUNNING

String apSsid() {
  // "TideTime-" + last 4 hex digits of the MAC, zero-padded manually
  // (do not rely on String(uint32_t, HEX) zero-padding behaviour).
  const uint64_t mac = ESP.getEfuseMac();
  const unsigned int suffix = (unsigned int)(mac & 0xFFFFull);
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", suffix);
  return String("TideTime-") + buf;
}

// Initialise lwIP.
//
// Both the web server (WebServer::begin -> lwip_socket) and the NTP client
// (NTP::begin -> udp->begin) need the lwIP TCP/IP thread to be running.
// That thread is started by esp_netif_init(), which the Arduino core only
// calls from NetworkManager::begin(). WiFiGenericClass::mode() calls it too,
// but only the first time it actually changes the radio mode - so relying on
// that alone is fragile.
//
// Calling Network.begin() directly is idempotent (NetworkManager::begin()
// guards with a static flag) and guarantees the stack is up before any
// socket is created.
void initNetwork() {
  if (!Network.begin()) {
    Serial.println("[Provisioning] ERROR: Network.begin() failed - lwIP not available");
  } else {
    Serial.println("[Provisioning] lwIP network stack initialised");
  }
}

void startAccessPoint() {
  // AP and STA together, so the STA side can still try to connect.
  WiFi.mode(WIFI_AP_STA);
  // Open AP, no password. Default AP IP is 192.168.4.1.
  WiFi.softAP(apSsid().c_str());

  // Answer every A query with the AP IP so the captive portal pops up.
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());

  deviceMode = MODE_SETUP_AP;
  Serial.printf("[Provisioning] AP up: %s at %s\n",
                apSsid().c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAccessPoint() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[Provisioning] AP torn down, STA-only mode");
}

void beginWifiConnect() {
  // Keep the AP up during the attempt.
  WiFi.mode(WIFI_AP_STA);
  // Core 3.3.11: WiFi.begin(ssid, psk) is ALREADY NON-BLOCKING (tryConnect
  // defaults to true). Never write a blocking connect loop - drive the
  // connect from pollWifiConnect().
  WiFi.begin(config.ssid.c_str(), config.psk.c_str());

  connectStart = millis();
  connectedAt = 0;
  deviceMode = MODE_CONNECT_STA;
  Serial.printf("[Provisioning] Connecting to \"%s\" (20 s budget)...\n", config.ssid.c_str());
}

// Reconnect after the link dropped while running. Unlike the initial
// provisioning attempt this does not need the access point or the 60 s
// grace period - the user is not waiting on a captive portal - so stay in
// STA-only mode.
static void reconnectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.psk.c_str());
  connectStart = millis();
  connectedAt = 0;
  deviceMode = MODE_CONNECT_STA;
  Serial.println("[Provisioning] Reconnecting to stored network...");
}

void pollWifiConnect() {
  if (deviceMode == MODE_RUNNING) {
    // Watch the link while running. If it drops we must not keep calling
    // ntp.update(): the UDP socket is bound to an interface that has gone
    // away, and the retry path can trip the same lwIP assert that the
    // initial-start ordering bug did. Stop the NTP client and reconnect.
    if (WiFi.status() != WL_CONNECTED) {
      if (!runningLinkLost) {
        runningLinkLost = true;
        lastError = "Connection lost, reconnecting...";
        Serial.println("[Provisioning] Link lost - stopping NTP client");
        ntp.stop();
        ntpStarted = false;
        reconnectWifi();
      }
    } else if (runningLinkLost) {
      runningLinkLost = false;
      lastError = "";
    }
    return;
  }

  if (deviceMode != MODE_CONNECT_STA) {
    return;
  }

  const wl_status_t st = WiFi.status();
  const unsigned long now = millis();

  if (st == WL_CONNECTED) {
    if (connectedAt == 0) {
      connectedAt = now;
      lastError = "";
      Serial.printf("[Provisioning] Connected, IP: %s (AP stays up for 60 s so /status can be polled)\n",
                    WiFi.localIP().toString().c_str());
      // The NTP client can only be started now: its UDP socket needs a live
      // network interface (see startNtp()).
      startNtp();
    }
    if (now - connectedAt >= AP_GRACE_MS) {
      stopAccessPoint();
      deviceMode = MODE_RUNNING;
      connectedAt = 0;
      connectStart = 0;
    }
    return;
  }

  // Not connected. If we were in the post-connect grace period the link
  // dropped: resume a fresh connect budget instead of falling over.
  if (connectedAt != 0) {
    connectedAt = 0;
    connectStart = now;
  }
  if (connectStart == 0) {
    connectStart = now;  // safety net if beginWifiConnect() was never polled for time
  }

  if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
    lastError = wifiStatusText();
    deviceMode = MODE_SETUP_AP;  // the AP is still up, so the UI can show the error
    connectStart = 0;
    Serial.printf("[Provisioning] Connect failed: %s\n", lastError.c_str());
    return;
  }

  if (now - connectStart >= CONNECT_TIMEOUT_MS) {
    lastError = "Connection timed out";
    deviceMode = MODE_SETUP_AP;  // the AP is still up, so the UI can show the error
    connectStart = 0;
    Serial.printf("[Provisioning] %s\n", lastError.c_str());
  }
}

String wifiStatusText() {
  switch (WiFi.status()) {
    case WL_CONNECTED:       return "Connected";
    case WL_CONNECT_FAILED:  return "Connection failed";
    case WL_NO_SSID_AVAIL:   return "Network not found";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED:    return "Disconnected";
    case WL_IDLE_STATUS:     return "Idle";
    case WL_NO_SHIELD:       return "WiFi module not present";
    default:                 return "Connecting";
  }
}

void applyTimezone() {
  // Offsets are ABSOLUTE minutes from UTC. timeZone() needs signed
  // hours/minutes, so use signed division: -300 -> -5 hours, 0 minutes.
  if (!config.tzHasDst) {
    ntp.timeZone((int8_t)(config.tzStdOffset / 60), (int8_t)(config.tzStdOffset % 60));
    ntp.isDST(false);
  } else {
    // The day-of-week is fixed to Sunday (matches the original sketch and
    // the config schema, which has no dow field).
    ntp.ruleDST(config.tzDstName.c_str(), (int8_t)config.tzDstWeek, Sun,
                (int8_t)config.tzDstMonth, (int8_t)config.tzDstHour, config.tzDstOffset);
    ntp.ruleSTD(config.tzStdName.c_str(), (int8_t)config.tzStdWeek, Sun,
                (int8_t)config.tzStdMonth, (int8_t)config.tzStdHour, config.tzStdOffset);
  }
}

bool ntpStarted = false;

// Start the NTP client, but only once the network is actually up.
//
// NTP::begin() -> init() -> udp->begin(123) opens and binds a UDP socket and
// then immediately calls ntpUpdate(). If no network interface is active the
// bind() fails, NetworkUDP::stop() runs, and the follow-up sendto()/recvfrom()
// on the dead socket trips the FreeRTOS xQueueSemaphoreTake assert inside lwIP
// (pxQueue == NULL), which aborts and reboots the ESP32.
//
// So this must only be called once WiFi.status() == WL_CONNECTED.
void startNtp() {
  if (ntpStarted) {
    return;
  }
  // The timezone rules must be in place before the client starts.
  applyTimezone();
  Serial.println("[NTP] Starting NTP client (network is up)");
  ntp.begin();
  ntpStarted = true;
}

bool ntpHasSynced() {
  // NTP::epoch() is only meaningful once a sync has succeeded; before that it
  // returns 0.
  return ntpStarted && ntp.epoch() > 1000000000UL;
}
