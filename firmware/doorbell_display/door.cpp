#include "door.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <rom/tjpgd.h>

#include "cfg.h"
#include "httputil.h"
#include "config.h"
#include "net.h"

// Bewegung: Abfrage der Klingel per HTTP (GetEvents, ältere Firmware: GetMdState), einmal pro Sekunde.
// Bild: Snap der Klingel (Substream) oder eine eigene JPEG-Adresse; dekodiert mit dem JPEG-Dekoder im ROM des ESP32-S3.
static const size_t JPG_MAX = 512 * 1024;
static const int POOL = 8192;
static const uint32_t CAP_MS = 300000;      // so lange höchstens am Stück, auch bei Dauerbewegung
static const uint32_t NOFRAME_MS = 8000;    // kommt bis dahin kein Bild, wird abgebrochen

static uint8_t *jpg = nullptr;
static uint16_t *buf[2] = {nullptr, nullptr};
static void *pool = nullptr;
static volatile int pubIdx = -1, pubW = 0, pubH = 0;
static volatile uint32_t seq = 0;
static volatile bool want = false;
static volatile bool dismissReq = false, testReq = false;
static volatile uint32_t endAt = 0;         // millis, an dem die Anzeige endet
static char status[96] = "aus";
static volatile time_t lastMotionT = 0;
static WiFiClient plain;
static WiFiClientSecure secure;              // die Klingel hat ein selbst signiertes Zertifikat: nicht geprüft (Heimnetz)
static HTTPClient http;

static void set_status(const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vsnprintf(status, sizeof status, fmt, a);
  va_end(a);
  if (DEBUG_LOG) Serial.printf("Tuer: %s\n", status);
}

const char *door_status() { return status; }
time_t door_last_motion() { return lastMotionT; }
bool door_wanted() { return want; }
int door_remaining() { const int32_t r = (int32_t)(endAt - millis()); return want && r > 0 ? (r + 999) / 1000 : 0; }
uint32_t door_seq() { return seq; }
void door_dismiss() { dismissReq = true; }
void door_test() { testReq = true; }

bool door_frame(const uint16_t **px, int *w, int *h) {
  if (pubIdx < 0) return false;
  *px = buf[pubIdx]; *w = pubW; *h = pubH;
  return true;
}

static String enc(const String &s) {
  String o;
  static const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

// Die Klingel antwortet meist nur auf HTTPS (HTTP wird dorthin umgeleitet). Ohne Angabe gilt HTTPS; "http://" erzwingt HTTP.
static String base() {
  if (cfg.dhost.startsWith("http://") || cfg.dhost.startsWith("https://")) return cfg.dhost;
  return String("https://") + cfg.dhost;
}

static String token;                         // Anmeldemarke der Klingel (Login); leer: noch nicht angemeldet

// Antwort der Klingel holen. post: JSON-Körper (POST) oder nullptr (GET). Rückgabe: Länge oder -1.
static int fetch(const String &url, uint8_t *dst, size_t cap, const char *post = nullptr) {
  const bool tls = url.startsWith("https");
  if (tls) secure.setInsecure();
  WiFiClient &cl = tls ? (WiFiClient &)secure : plain;
  http.setReuse(true);
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  if (!http.begin(cl, url)) return -1;
  int code;
  if (post) { http.addHeader("Content-Type", "application/json"); code = http.POST((uint8_t *)post, strlen(post)); }
  else code = http.GET();
  if (code != 200) { set_status("Klingel antwortet mit %d", code); http.end(); cl.stop(); return -1; }
  BufStream bs(dst, cap);
  http.writeToStream(&bs);
  http.end();                                // bei reuse bleibt die Verbindung offen
  if (bs.over || bs.n == 0) { cl.stop(); set_status(bs.over ? "Antwort zu groß" : "Antwort leer"); return -1; }
  return (int)bs.n;
}

// Kurzfassung eines Fehlers der Klingel für die Anzeige: Befehl, rspCode und Text
static void set_error(const char *what, const char *b) {
  String d = json_val(b, "detail"), c = json_val(b, "rspCode");
  set_status("%s: %s (%s)", what, d.length() ? d.c_str() : "?", c.length() ? c.c_str() : "?");
}

// Anmelden und die Marke holen (so wie die Reolink-App und Home Assistant es tun)
static bool login() {
  static uint8_t body[1024];
  // Anführungszeichen und Backslash im Kennwort maskieren
  String esc;
  for (size_t i = 0; i < cfg.dpass.length(); i++) { const char c = cfg.dpass[i]; if (c == '"' || c == '\\') esc += '\\'; esc += c; }
  String esu, j;
  for (size_t i = 0; i < cfg.duser.length(); i++) { const char c = cfg.duser[i]; if (c == '"' || c == '\\') esu += '\\'; esu += c; }
  j = String("[{\"cmd\":\"Login\",\"param\":{\"User\":{\"Version\":\"0\",\"userName\":\"") + esu + "\",\"password\":\"" + esc + "\"}}}]";
  const int n = fetch(base() + "/api.cgi?cmd=Login", body, sizeof body - 1, j.c_str());
  if (n < 0) return false;
  body[n] = 0;
  const char *b = (const char *)body;
  const char *t = strstr(b, "\"Token\"");
  String name = t ? json_val(b, "name", t) : "";
  if (!name.length()) { set_error("Anmeldung abgelehnt", b); return false; }
  token = name;
  return true;
}

static String api(const char *cmd, const char *extra) {
  return base() + "/api.cgi?cmd=" + cmd + "&channel=0" + extra + "&token=" + enc(token);
}

// 1 Bewegung, 0 ruhig, -1 Fehler
static int poll_motion() {
  static bool useMd = false;
  static uint8_t body[1536];
  bool swapped = false;
  for (int tries = 0; tries < 4; tries++) {
    if (!token.length() && !login()) return -1;
    // Bewegung und Ereignisse fragt man per POST mit JSON-Körper ab (GET kennt die Parameter nicht: "check err -3")
    const String req = String("[{\"cmd\":\"") + (useMd ? "GetMdState" : "GetEvents") + "\",\"action\":0,\"param\":{\"channel\":0}}]";
    const int n = fetch(base() + "/api.cgi?token=" + enc(token), body, sizeof body - 1, req.c_str());
    if (n < 0) return -1;
    body[n] = 0;
    const char *b = (const char *)body;
    if (!strstr(b, "\"code\" : 0") && !strstr(b, "\"code\":0")) {
      const String rc = json_val(b, "rspCode");
      if (rc == "-6" || rc == "-5" || strstr(b, "login")) { token = ""; continue; }   // Marke abgelaufen: neu anmelden
      set_error(useMd ? "GetMdState" : "GetEvents", b);
      if (!swapped) { swapped = true; useMd = !useMd; continue; }   // Befehl unbekannt (z. B. -9, -104): den anderen versuchen
      return -1;
    }
    set_status("verbunden");
    if (useMd) return strstr(b, "\"state\" : 1") || strstr(b, "\"state\":1") ? 1 : 0;
    // GetEvents: Bewegung (md) oder erkannte Person bzw. Besucher (Klingeltaste)
    return strstr(b, "\"state\" : 1") || strstr(b, "\"state\":1") || strstr(b, "\"visitor\" : 1") || strstr(b, "\"visitor\":1") ||
           strstr(b, "\"people\" : 1") || strstr(b, "\"people\":1") ? 1 : 0;
  }
  if (token.length()) set_status("Klingel versteht die Abfrage nicht");
  return -1;
}

struct Ctx { const uint8_t *p; size_t n, pos; uint16_t *out; int ow; };

static UINT in_cb(JDEC *jd, BYTE *b, UINT n) {
  Ctx *c = (Ctx *)jd->device;
  const size_t r = min((size_t)n, c->n - c->pos);
  if (b) memcpy(b, c->p + c->pos, r);
  c->pos += r;
  return r;
}

static UINT out_cb(JDEC *jd, void *bitmap, JRECT *rc) {
  Ctx *c = (Ctx *)jd->device;
  const uint8_t *s = (const uint8_t *)bitmap;
  for (int y = rc->top; y <= rc->bottom; y++) {
    uint16_t *d = c->out + (size_t)y * c->ow + rc->left;
    for (int x = rc->left; x <= rc->right; x++, s += 3, d++) *d = ((s[0] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[2] >> 3);
  }
  return 1;
}

static bool decode(size_t n, uint16_t *out, int *w, int *h) {
  Ctx c = {jpg, n, 0, out, 0};
  JDEC jd;
  if (jd_prepare(&jd, in_cb, pool, POOL, &c) != JDR_OK) { set_status("Bild ist kein lesbares JPEG"); return false; }
  int sc = 0;
  while (sc < 3 && ((int)(jd.width >> sc) > SCREEN_W || (int)(jd.height >> sc) > SCREEN_H)) sc++;
  c.ow = jd.width >> sc;
  *w = c.ow;
  *h = jd.height >> sc;
  if (*w > SCREEN_W || *h > SCREEN_H) { set_status("Bild zu groß (%ux%u)", (unsigned)jd.width, (unsigned)jd.height); return false; }
  if (jd_decomp(&jd, out_cb, sc) != JDR_OK) { set_status("JPEG nicht dekodierbar"); return false; }
  return true;
}

static volatile uint32_t firstReq = 0;                    // millis der Anforderung, für "erstes Bild nach"
static volatile uint32_t stFetch = 0, stDec = 0, stBytes = 0, stFirst = 0;
static volatile int stW = 0, stH = 0;
static uint32_t stFrames = 0, stSince = 0;
static char stText[96] = "";

// Kennzahlen des letzten Bildes für die Einstellungen: Größe, Abruf- und Dekodierzeit, Bilder pro Sekunde
const char *door_stats() {
  if (!stBytes) return "noch kein Bild geholt";
  snprintf(stText, sizeof stText, "%dx%d, %u KB, Abruf %u ms, Dekodieren %u ms, erstes Bild nach %u ms", stW, stH, (unsigned)(stBytes / 1024), (unsigned)stFetch, (unsigned)stDec, (unsigned)stFirst);
  return stText;
}

static bool grab_frame(int wr) {
  String url = cfg.durl.length() ? cfg.durl : (token.length() || login()) ? api("Snap", (String("&snapType=sub&width=640&height=480&rs=") + String((uint32_t)esp_random(), HEX)).c_str()) : String();
  if (!url.length()) return false;
  const uint32_t t0 = millis();
  const int n = fetch(url, jpg, JPG_MAX);
  const uint32_t t1 = millis();
  if (n < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
    if (n >= 0) {
      if (n < JPG_MAX) jpg[n] = 0;
      const String rc = json_val((const char *)jpg, "rspCode");
      if (rc == "-6" || rc == "-5") token = "";
      set_error("Bild", (const char *)jpg);
    }
    return false;
  }
  int w, h;
  if (!decode(n, buf[wr], &w, &h)) return false;
  stFetch = t1 - t0; stDec = millis() - t1; stBytes = n; stW = w; stH = h;
  if (seq == 0) stFirst = millis() - firstReq;
  pubW = w; pubH = h; pubIdx = wr;
  seq = seq + 1;
  return true;
}

static bool alloc_buffers() {
  if (jpg && buf[0] && buf[1] && pool) return true;
  if (!jpg) jpg = (uint8_t *)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
  for (int i = 0; i < 2; i++) if (!buf[i]) buf[i] = (uint16_t *)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (!pool) pool = heap_caps_malloc(POOL, MALLOC_CAP_INTERNAL);
  const bool ok = jpg && buf[0] && buf[1] && pool;
  if (!ok) set_status("zu wenig Speicher");
  return ok;
}

static void task(void *) {
  uint32_t lastPoll = 0, start = 0, lastMotion = 0, noFrameSince = 0;
  bool armed = true;                          // erst wieder auslösen, wenn die Bewegung einmal aufgehört hat
  int wr = 0;
  for (;;) {
    if (!cfg.door || !cfg.dhost.length() || net_setup() || !net_wifi_ok()) {
      want = false;
      if (!cfg.door) set_status("aus");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    const uint32_t now = millis();
    if (dismissReq) { dismissReq = false; want = false; armed = false; }
    if (testReq) {
      testReq = false;
      if (alloc_buffers()) { firstReq = now; want = true; start = lastMotion = now; noFrameSince = now; seq = 0; pubIdx = -1; endAt = now + cfg.dsecs * 1000u; }
    }
    if (now - lastPoll >= (want ? 3000u : 1000u)) {
      lastPoll = now;
      const int m = poll_motion();
      if (m == 0) armed = true;
      if (m == 1) {
        lastMotion = millis();
        if (net_time_ok()) lastMotionT = time(nullptr);
        if (!want && armed && alloc_buffers()) {
          firstReq = millis(); want = true; start = lastMotion; noFrameSince = lastMotion; seq = 0; pubIdx = -1;
        }
        if (want) endAt = lastMotion + cfg.dsecs * 1000u;
      }
    }
    if (want) {
      const uint32_t t = millis();
      const bool timeUp = (int32_t)(t - endAt) >= 0, capped = t - start > CAP_MS, noFrame = pubIdx < 0 && t - noFrameSince > NOFRAME_MS;
      if (timeUp || capped || noFrame) {
        want = false;
        if (capped || noFrame) armed = false;
      } else if (grab_frame(wr)) { wr ^= 1; noFrameSince = millis(); }
      else vTaskDelay(pdMS_TO_TICKS(300));
    } else vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void door_begin() {
  xTaskCreatePinnedToCore(task, "door", 8192, nullptr, 1, nullptr, 1);   // Kern 1 wie loop(); das Zeichnen läuft auf Kern 0
}
