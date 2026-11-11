#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <DNSServer.h>
#include "NTP.h"
#include "Config.h"

enum DeviceMode {
  MODE_SETUP_AP = 0,     // access point up, waiting for wifi credentials
  MODE_CONNECT_STA,      // access point up, connecting to home wifi
  MODE_RUNNING           // connected to home wifi, no access point
};

extern DeviceMode deviceMode;
extern String lastError;   // empty when there is no error

// Globals owned by TideTime.ino (Phase B keeps them there).
extern WiFiUDP wifiUdp;
extern NTP ntp;

// Captive-portal DNS server, defined in Provisioning.cpp.
extern DNSServer dnsServer;

void startAccessPoint();      // brings up softAP + DNS captive portal
void stopAccessPoint();       // tears down softAP + DNS
void beginWifiConnect();     // non-blocking connect using config.ssid/config.psk
void pollWifiConnect();      // called from loop(); advances the connect state machine
void applyTimezone();        // pushes config's timezone rules into the NTP object
String apSsid();             // "TideTime-XXXX"
String wifiStatusText();     // human-readable WiFi status
