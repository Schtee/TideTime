/**
 * BasicHTTPClient.ino
 *
 *  Created on: 24.05.2015
 *
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>

#include <HTTPClient.h>

#include <ArduinoJson.h>

#include "NTP.h"

#include <chrono>

#define TZ_INFO "Europe/London"

WiFiMulti wifiMulti;
WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

/*
const char* ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n" \
"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n" \
"DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n" \
"SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n" \
"GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n" \
"AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n" \
"q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n" \
"SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n" \
"Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n" \
"a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n" \
"/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n" \
"AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n" \
"CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n" \
"bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n" \
"c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n" \
"VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n" \
"ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n" \
"MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n" \
"Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n" \
"AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n" \
"uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n" \
"wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n" \
"X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n" \
"PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n" \
"KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n" \
"-----END CERTIFICATE-----\n";
*/

struct TideEvent {
  tide_time time;
  int type;
}

JsonDocument jsonDoc;
typedef std::chrono::sys_time<std::chrono::milliseconds> tide_time;
typedef std::optional<std::pair<TideEvent, TideEvent>> MaybeTideEventPair;

std::chrono::sys_time<std::chrono::milliseconds> parse8601(std::string str) {
  std::istringstream in{str};
  std::chrono::sys_time<std::chrono::milliseconds> tp;
  in >> std::chrono::parse("%FT%T", tp);
  return tp;
}


MaybeTideEventPair getPreviousAndNextEvent(JsonArray eventList) {
  TideEvent previous;
  int now = ntp.epoch() * 1000;
  for (auto x : eventList) {
    auto time = parse8601(x["dateTime"].as<const char*>());

    TideEvent e = { time, x["eventType"].as<int>() };

    if (time.time_since_epoch().count() > now) {
      return std::pair<TideEvent, TideEvent>(previous, e);
    }

    previous = e;
    Serial.println(time.time_since_epoch().count());
  }
  return std::nullopt;
}

std::optional<std::pair<tide_time, tide_time>> currentTideEvents;

std::pair<tide_time, tide_time> getPreviousAndNextEvent() {
  HTTPClient http;

  Serial.print("[HTTP] begin...\n");
  // configure traged server and url
  //http.begin("https://www.howsmyssl.com/a/check", ca); //HTTPS
  http.begin("https://easytide.admiralty.co.uk/Home/GetPredictionData?stationId=0546");  //HTTP

  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println(payload);
      deserializeJson(jsonDoc, payload);
      auto eventList = jsonDoc["tidalEventList"].as<JsonArray>();
      Serial.println(eventList[0]["eventType"].as<int>());
      currentTideEvents = getPreviousAndNextEvent(eventList);
    }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
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

  wifiMulti.addAP("TNCAPCE0FAA", "y4PYgmp3RppedRGf");

  while ((wifiMulti.run() != WL_CONNECTED)) {
    delay(100);
  }
  
  ntp.ruleDST("BST", Last, Sun, Mar, 1, 60);
  ntp.ruleSTD("GMT", Last, Sun, Oct, 2, 0);
  ntp.begin();

  
}

bool calculatedTideTime = false;
void loop() {
  if (ntp.update()) {
    if (!calculatedTideTime) {
      calculatedTideTime = true;

      Serial.println(ntp.epoch());
      Serial.println(ntp.formattedTime("%d. %B %Y")); // dd. Mmm yyyy
      Serial.println(ntp.formattedTime("%A %T")); // Www hh:mm:ss
    }
  } 
  else {
    Serial.println("No valid time data");
  }
  
  delay(10000);
}
