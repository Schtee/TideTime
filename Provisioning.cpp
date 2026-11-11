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

String apSsid() {
  // "TideTime-" + last 4 hex digits of the MAC, zero-padded manually
  // (do not rely on String(uint32_t, HEX) zero-padding behaviour).
  const uint64_t mac = ESP.getEfuseMac();
  const unsigned int suffix = (unsigned int)(mac & 0xFFFFull);
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", suffix);
  return String("TideTime-") + buf;
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

void pollWifiConnect() {
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
