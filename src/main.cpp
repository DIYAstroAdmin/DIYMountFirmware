// MyMount - equatorial mount controller for the FYSETC E4.
//
// The USB serial port speaks LX200 only, so nothing else may print to Serial.
//
// Core use: everything network related (WiFi, mDNS, web page, Alpaca, NTP) runs in the
// "comms" task on core 0, next to the WiFi driver. Core 1 is left to mount control: the
// mount task, the step generator, and LX200 on the USB serial port (loop()).
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

// Network task, pinned to core 0.
static void commsTask(void *) {
  startWiFi();
  WebUI::begin();
  for (;;) {
    WebUI::poll();
    pollNtp();
    vTaskDelay(1);   // also lets the core 0 idle task run, which the task watchdog checks
  }
}

void setup() {
#if PIN_STATUS_LED >= 0
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, !STATUS_LED_ON);   // off until the mount is running (see Mount::updateLed)
#endif
  Serial.begin(LX200_BAUD);
  LOGI(FIRMWARE_NAME " " FIRMWARE_VERSION " starting");
  settings.load();
  Log::setLevel((Log::Level)settings.logLevel);
  Log::setSourceEnabled(Log::Source::Lx200, settings.logLx200);
  Log::setSourceEnabled(Log::Source::Alpaca, settings.logAlpaca);
  mount.begin();
  // Same stack as the Arduino loop task: the web handlers build JSON responses in Strings.
  xTaskCreatePinnedToCore(commsTask, "comms", 8192, nullptr, 1, nullptr, 0);
}

// Runs on core 1 (the Arduino default), below the mount task's priority.
void loop() {
  lx200.poll();
  delay(1);
}
