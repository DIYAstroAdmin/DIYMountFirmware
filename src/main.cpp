// MyMount - equatorial mount controller for the FYSETC E4.
//
// The USB serial port speaks LX200 only, so nothing else may print to Serial.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>
#include "Astro.h"
#include "Config.h"
#include "LX200.h"
#include "Log.h"
#include "Mount.h"
#include "Settings.h"
#include "WebUI.h"

static LX200 lx200(Serial);
static bool ntpStarted = false;

// Joins the configured network, or starts an access point if that is not possible.
static void startWiFi() {
  WiFi.persistent(false);
  WiFi.setHostname(settings.hostname.c_str());
  bool joined = false;
  if (settings.ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(100);
    joined = WiFi.status() == WL_CONNECTED;
  }
  if (joined) {
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    configTime(0, 0, NTP_SERVER);
    ntpStarted = true;
    LOGI("WiFi: joined '%s', address %s", settings.ssid.c_str(), WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(settings.hostname.c_str(), AP_PASSWORD);
    if (settings.ssid.length()) LOGW("WiFi: could not join '%s'", settings.ssid.c_str());
    LOGI("WiFi: access point '%s', address %s", settings.hostname.c_str(), WiFi.softAPIP().toString().c_str());
  }
  MDNS.begin(settings.hostname.c_str());
  MDNS.addService("http", "tcp", 80);
}

// Takes the time from NTP once, unless a client has already set it.
static void pollNtp() {
  static uint32_t last = 0;
  if (!ntpStarted || Astro::timeIsSet() || millis() - last < 1000) return;
  last = millis();
  time_t now = time(nullptr);
  if (now > 1700000000) {
    Astro::setUtc((double)now);
    LOGI("Time set from NTP");
  }
}

void setup() {
  Serial.begin(LX200_BAUD);
  LOGI(FIRMWARE_NAME " " FIRMWARE_VERSION " starting");
  settings.load();
  Log::setLevel((Log::Level)settings.logLevel);
  mount.begin();
  startWiFi();
  WebUI::begin();
}

void loop() {
  lx200.poll();
  WebUI::poll();
  pollNtp();
  delay(1);
}
