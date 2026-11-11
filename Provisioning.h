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

// Initialise the lwIP stack. Must be called before any socket is created
// (web server, UDP client). Idempotent.
void initNetwork();

void startAccessPoint();      // brings up softAP + DNS captive portal
void stopAccessPoint();       // tears down softAP + DNS
void beginWifiConnect();     // non-blocking connect using config.ssid/config.psk
void pollWifiConnect();      // called from loop(); advances the connect state machine
void applyTimezone();        // pushes config's timezone rules into the NTP object
String apSsid();             // "TideTime-XXXX"
String wifiStatusText();     // human-readable WiFi status

// NTP client lifecycle.
//
// The NTP library opens a UDP socket inside begin() and immediately attempts a
// sync. If the radio is not up yet, socket() succeeds but bind() fails, which
// leaves a dead socket behind; the subsequent ntpUpdate() then trips the
// FreeRTOS xQueueSemaphoreTake assert inside lwIP and reboots the device.
// So begin() must not be called until WiFi is actually connected.
//
// ntpStarted is false until that has happened. The main loop must not call
// ntp.update() while it is false, because NTP::update() retries the sync on
// every call until it succeeds - each retry on a dead socket crashes.
extern bool ntpStarted;

// Starts the NTP client. Safe to call only once WiFi is connected; it is a
// no-op if the client has already been started.
void startNtp();

// True once the NTP client has been started and has completed at least one
// successful sync. Use this to decide whether the clock is usable.
bool ntpHasSynced();
