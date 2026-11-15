#include "net.h"

#include <WiFi.h>
#include <time.h>

#include "cfg.h"
#include "config.h"

static uint32_t lastWifiTry = 0;
static bool timeSet = false;
static bool setupMode = false;
String web_pin();

void net_begin() {
  cfg_load();
  if (!cfg_ok()) {                          // noch nicht eingerichtet: eigenes WLAN aufspannen, Einrichtung im Browser
    setupMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Doorbell-Display-Setup", ("tuer" + web_pin()).c_str());
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  configTzTime(TZ_INFO, NTP_SERVER);
}

void net_loop() {
  if (setupMode) return;
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiTry > 15000) { lastWifiTry = now; WiFi.disconnect(); WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str()); }
    return;
  }
  if (!timeSet) {
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_year > 120) timeSet = true;
  }
}

bool net_wifi_ok() { return WiFi.status() == WL_CONNECTED; }
bool net_setup() { return setupMode; }
bool net_time_ok() { return timeSet; }
