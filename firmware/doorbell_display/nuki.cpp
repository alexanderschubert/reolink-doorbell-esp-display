#include "nuki.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "cfg.h"
#include "config.h"
#include "httputil.h"
#include "net.h"

// Das Zertifikat von api.nuki.io wird geprüft (Bündel der Wurzelzertifikate im Arduino-Kern), weil der Token mitgeschickt wird.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static const size_t BUF = 24 * 1024;
static NukiInfo info = {};
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool refreshReq = false;

NukiInfo nuki_get() {
  portENTER_CRITICAL(&mux);
  NukiInfo c = info;
  portEXIT_CRITICAL(&mux);
  return c;
}

void nuki_refresh() { refreshReq = true; }

const char *nuki_state_text(int s) {
  switch (s) {
    case 1: return "Zugeschlossen";
    case 2: return "Wird entriegelt";
    case 3: return "Entriegelt";
    case 4: return "Wird verriegelt";
    case 5: return "Tür geöffnet";
    case 6: return "Entriegelt (Lock'n'Go)";
    case 7: return "Öffnet";
    case 254: return "Blockiert";
    case 0: return "Nicht kalibriert";
    default: return "Unbekannt";
  }
}

static void set_err(const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  portENTER_CRITICAL(&mux);
  vsnprintf(info.err, sizeof info.err, fmt, a);
  portEXIT_CRITICAL(&mux);
  va_end(a);
}

static bool query(uint8_t *buf) {
  WiFiClientSecure cl;
  cl.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  if (!http.begin(cl, "https://api.nuki.io/smartlock")) { set_err("Verbindung nicht möglich"); return false; }
  http.addHeader("Authorization", String("Bearer ") + cfg.ntoken);
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code == 401) { set_err("Token ungültig (401)"); http.end(); return false; }
  if (code == 429) { set_err("Zu viele Anfragen (429)"); http.end(); return false; }
  if (code != 200) { set_err("Nuki antwortet mit %d", code); http.end(); return false; }
  BufStream bs(buf, BUF - 1);
  http.writeToStream(&bs);
  http.end();
  if (bs.over) { set_err("Antwort zu groß"); return false; }
  buf[bs.n] = 0;
  const char *b = (const char *)buf;
  if (b[0] != '[' || !strstr(b, "\"smartlockId\"")) { set_err("Kein Schloss im Konto gefunden"); return false; }
  const char *st = strstr(b, "\"state\":{");
  if (!st) st = strstr(b, "\"state\" : {");
  if (!st) { set_err("Zustand fehlt in der Antwort"); return false; }
  st = strchr(st, '{');
  NukiInfo n = {};
  n.valid = true;
  strlcpy(n.name, json_val(b, "name").c_str(), sizeof n.name);
  n.state = json_val(st, "state").toInt();
  const String d = json_val(st, "doorState");
  n.door = d.length() ? d.toInt() : 255;
  const String bc = json_val(st, "batteryCharge");
  n.battery = bc.length() ? bc.toInt() : -1;
  n.critical = json_val(st, "batteryCritical") == "true";
  n.at = millis();
  portENTER_CRITICAL(&mux);
  strlcpy(n.err, "", sizeof n.err);
  info = n;
  portEXIT_CRITICAL(&mux);
  return true;
}

static void task(void *) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM);
  uint32_t last = 0;
  bool first = true;
  for (;;) {
    if (!cfg.nuki || !cfg.ntoken.length() || !net_wifi_ok() || net_setup() || !net_time_ok() || !buf) {
      first = true;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (first || refreshReq || millis() - last >= 30000) {
      first = false;
      refreshReq = false;
      last = millis();
      if (!query(buf)) last = millis() - 20000;            // bei einem Fehler nach zehn Sekunden erneut
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void nuki_begin() { xTaskCreatePinnedToCore(task, "nuki", 10240, nullptr, 1, nullptr, 1); }
