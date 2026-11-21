#include "door.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <rom/tjpgd.h>

#include "cfg.h"
#include "httputil.h"
#include "config.h"
#include "net.h"

// Bewegung: Abfrage des Türspions per HTTP (GetEvents, ältere Firmware: GetMdState), einmal pro Sekunde.
// Bild: Snap des Türspions (Substream) oder eine eigene JPEG-Adresse; dekodiert mit dem JPEG-Dekoder im ROM des ESP32-S3.
static const size_t JPG_MAX = 320 * 1024;
static const int POOL = 8192;
static const uint32_t CAP_MS = 300000;      // so lange höchstens am Stück, auch bei Dauerbewegung
static const uint32_t NOFRAME_MS = 8000;    // kommt bis dahin kein Bild, wird abgebrochen

// Bildkette: Der Abruf-Task holt das nächste JPEG (jpg[fi]), während der Dekodier-Task das vorige dekodiert. So überlappen sich
// Netzwerk und Rechnen, und die Bildrate hängt vom langsameren der beiden ab, nicht von der Summe.
// Dekodiert wird in einen von bis zu drei Puffern, der weder gerade gezeigt (pubIdx) noch zuletzt gezeigt (prevIdx) wurde,
// damit der Zeichentask nie in ein Bild liest, das gerade überschrieben wird.
static uint8_t *jpg[2] = {nullptr, nullptr};
static int jpgLen[2] = {0, 0};
static volatile int job = -1;               // Index des JPEGs, das der Dekodier-Task als Nächstes nimmt
static volatile bool decBusy = false;       // true von der Übergabe bis das Bild fertig ist
static int prevIdx = -1;
static int nbuf = 0;                        // Anzahl Bildpuffer (2 oder 3)
static uint16_t *buf[3] = {nullptr, nullptr, nullptr};
static void *pool = nullptr;
static volatile int pubIdx = -1, pubW = 0, pubH = 0;
static volatile uint32_t seq = 0;
static volatile bool want = false;
static volatile bool dismissReq = false, testReq = false;
static volatile uint32_t endAt = 0;         // millis, an dem die Anzeige endet
static char status[96] = "aus";
static char rawText[150] = "";
static char outText[260];
static volatile time_t lastMotionT = 0;
static WiFiClient plain;
static WiFiClientSecure secure;              // der Türspion hat ein selbst signiertes Zertifikat: nicht geprüft (Heimnetz)
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

void door_last_text(char *out, size_t n) {
  const time_t m = lastMotionT;
  if (!m) { snprintf(out, n, "noch keine Bewegung erkannt"); return; }
  if (!net_time_ok()) { snprintf(out, n, "Uhrzeit wird geholt …"); return; }
  const time_t now = time(nullptr);
  struct tm a, b;
  localtime_r(&m, &a);
  localtime_r(&now, &b);
  char when[24];
  if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) snprintf(when, sizeof when, "heute");
  else if ((b.tm_yday - a.tm_yday == 1 && a.tm_year == b.tm_year) || (b.tm_yday == 0 && a.tm_mon == 11 && a.tm_mday == 31 && b.tm_year - a.tm_year == 1)) snprintf(when, sizeof when, "gestern");
  else snprintf(when, sizeof when, "%d.%d.", a.tm_mday, a.tm_mon + 1);
  const long d = (long)(now - m);
  char ago[24];
  if (d < 60) snprintf(ago, sizeof ago, "vor %ld s", d < 0 ? 0 : d);
  else if (d < 3600) snprintf(ago, sizeof ago, "vor %ld min", d / 60);
  else if (d < 86400) snprintf(ago, sizeof ago, "vor %ld h", d / 3600);
  else snprintf(ago, sizeof ago, "vor %ld Tagen", d / 86400);
  snprintf(out, n, "%s %02d:%02d Uhr (%s)", when, a.tm_hour, a.tm_min, ago);
}
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

// Der Türspion antwortet meist nur auf HTTPS (HTTP wird dorthin umgeleitet). Ohne Angabe gilt HTTPS; "http://" erzwingt HTTP.
static String base() {
  if (cfg.dhost.startsWith("http://") || cfg.dhost.startsWith("https://")) return cfg.dhost;
  return String("https://") + cfg.dhost;
}

static String token;                         // Anmeldemarke des Türspions (Login); leer: noch nicht angemeldet

// Antwort des Türspions holen. post: JSON-Körper (POST) oder nullptr (GET). Rückgabe: Länge oder -1.
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
  if (code != 200) { set_status("Türspion antwortet mit %d", code); http.end(); cl.stop(); return -1; }
  BufStream bs(dst, cap);
  http.writeToStream(&bs);
  http.end();                                // bei reuse bleibt die Verbindung offen
  if (bs.over || bs.n == 0) { cl.stop(); set_status(bs.over ? "Antwort zu groß" : "Antwort leer"); return -1; }
  return (int)bs.n;
}

// Kurzfassung eines Fehlers des Türspions für die Anzeige: Befehl, rspCode und Text
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
    {  // Fehlersuche: jeder alarm_state mit seinem Namen, z. B. "md=1 people=0"
      size_t k = 0;
      rawText[0] = 0;
      for (const char *q = strstr(b, "alarm_state"); q && k < sizeof rawText - 12; q = strstr(q + 11, "alarm_state")) {
        const char *o = q;
        while (o > b && *o != '{') o--;                       // öffnende Klammer des Blocks
        const char *e = o;
        while (e > b && (*e == '{' || *e == ' ' || *e == ':' || *e == '"' || *e == '\n' || *e == '\t' || *e == '\r')) e--;
        const char *st = e;
        while (st > b && *(st - 1) != '"') st--;
        const char *v = q + 11;
        while (*v && (*v == '"' || *v == ' ' || *v == ':')) v++;
        k += snprintf(rawText + k, sizeof rawText - k, "%.*s=%c ", (int)(e - st + 1), st, *v);
      }
      if (!k) { for (const char *q = b; *q && k < sizeof rawText - 1; q++) if (*q > ' ' && *q != '"') rawText[k++] = *q; rawText[k] = 0; }
    }
    // Antwort enthält je nach Modell "state" (GetMdState) oder "alarm_state" (GetEvents: md, ai.people, visitor ...) mit Wert 1
    return strstr(b, "\"state\" : 1") || strstr(b, "\"state\":1") || strstr(b, "alarm_state\" : 1") || strstr(b, "alarm_state\":1") ||
           strstr(b, "\"visitor\" : 1") || strstr(b, "\"visitor\":1") ? 1 : 0;
  }
  if (token.length()) set_status("Türspion versteht die Abfrage nicht");
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

static bool decode(const uint8_t *data, size_t n, uint16_t *out, int *w, int *h) {
  Ctx c = {data, n, 0, out, 0};
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
static uint32_t stFrames = 0, stSince = 0, stFps10 = 0;
static char stText[112] = "";

// Kennzahlen des letzten Bildes für die Einstellungen: Größe, Abruf- und Dekodierzeit, Bilder pro Sekunde
const char *door_stats() {
  if (!stBytes) strcpy(stText, "noch kein Bild geholt");
  else snprintf(stText, sizeof stText, "%dx%d, %u KB, Abruf %u ms, Dekodieren %u ms, %u.%u fps, erstes Bild nach %u ms", stW, stH, (unsigned)(stBytes / 1024), (unsigned)stFetch, (unsigned)stDec, (unsigned)(stFps10 / 10), (unsigned)(stFps10 % 10), (unsigned)stFirst);
  snprintf(outText, sizeof outText, "%s\nAntwort: %s", stText, rawText);
  return outText;
}

// Holt ein JPEG in jpg[fi] und übergibt es dem Dekodier-Task (wartet, falls der noch am vorigen Bild arbeitet).
static bool grab_frame(int fi) {
  String url = cfg.durl.length() ? cfg.durl : (token.length() || login()) ? api("Snap", (String("&snapType=sub&width=640&height=480&rs=") + String((uint32_t)esp_random(), HEX)).c_str()) : String();
  if (!url.length()) return false;
  const uint32_t t0 = millis();
  const int n = fetch(url, jpg[fi], JPG_MAX);
  const uint32_t t1 = millis();
  if (n < 4 || jpg[fi][0] != 0xFF || jpg[fi][1] != 0xD8) {
    if (n >= 0) {
      if (n < (int)JPG_MAX) jpg[fi][n] = 0;
      const String rc = json_val((const char *)jpg[fi], "rspCode");
      if (rc == "-6" || rc == "-5") token = "";
      set_error("Bild", (const char *)jpg[fi]);
    }
    return false;
  }
  stFetch = t1 - t0; stBytes = n;
  while (decBusy && want) vTaskDelay(1);
  jpgLen[fi] = n;
  job = fi;
  __sync_synchronize();
  decBusy = true;
  return true;
}

// Läuft im eigenen Task: nimmt übergebene JPEGs, dekodiert sie und veröffentlicht das Bild.
static void decode_task(void *) {
  for (;;) {
    if (!decBusy) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
    __sync_synchronize();
    const int fi = job;
    const uint32_t t1 = millis();
    int wr = -1;
    for (int i = 0; i < nbuf; i++) if (i != pubIdx && (nbuf < 3 || i != prevIdx)) { wr = i; break; }
    int w = 0, h = 0;
    if (wr >= 0 && decode(jpg[fi], jpgLen[fi], buf[wr], &w, &h)) {
      stDec = millis() - t1; stW = w; stH = h;
      if (seq == 0) stFirst = millis() - firstReq;
      prevIdx = pubIdx;
      pubW = w; pubH = h; pubIdx = wr;
      seq = seq + 1;
      stFrames++;
      const uint32_t now = millis();
      if (now - stSince >= 3000) { stFps10 = stFrames * 10000u / (now - stSince); stFrames = 0; stSince = now; }
    }
    decBusy = false;
  }
}

static bool alloc_buffers() {
  if (nbuf >= 2 && jpg[0] && jpg[1] && pool) return true;
  for (int i = 0; i < 2; i++) if (!jpg[i]) jpg[i] = (uint8_t *)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
  for (int i = 0; i < 3; i++) if (!buf[i]) buf[i] = (uint16_t *)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);   // der dritte ist optional
  if (!pool) pool = heap_caps_malloc(POOL, MALLOC_CAP_INTERNAL);
  nbuf = buf[0] && buf[1] ? (buf[2] ? 3 : 2) : 0;
  const bool ok = jpg[0] && jpg[1] && nbuf >= 2 && pool;
  if (!ok) set_status("zu wenig Speicher");
  return ok;
}

// Neue Anzeige beginnen: wartet auf den Dekodier-Task und setzt die Zähler zurück.
static void start_session(uint32_t now) {
  while (decBusy) vTaskDelay(1);
  firstReq = now; want = true; seq = 0; pubIdx = -1; prevIdx = -1; stFrames = 0; stSince = now; stFps10 = 0;
}

static void task(void *) {
  uint32_t lastPoll = 0, start = 0, lastMotion = 0, noFrameSince = 0;
  bool armed = true;                          // erst wieder auslösen, wenn die Bewegung einmal aufgehört hat
  int fi = 0;
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
      if (alloc_buffers()) { start_session(now); start = lastMotion = now; noFrameSince = now; endAt = now + cfg.dsecs * 1000u; }
    }
    if (now - lastPoll >= (want ? 3000u : 1000u)) {
      lastPoll = now;
      const int m = poll_motion();
      if (m == 0) armed = true;
      if (m == 1) {
        lastMotion = millis();
        if (net_time_ok()) {
          const time_t t = time(nullptr);
          if (t - lastMotionT > 60) {                // höchstens einmal pro Minute in den Speicher schreiben
            Preferences p;
            p.begin("doorbell", false);
            p.putULong("dLast", (uint32_t)t);
            p.end();
          }
          lastMotionT = t;
        }
        if (!want && armed && alloc_buffers()) {
          start_session(millis()); start = lastMotion; noFrameSince = lastMotion;
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
      } else if (grab_frame(fi)) { fi ^= 1; noFrameSince = millis(); }
      else vTaskDelay(pdMS_TO_TICKS(300));
    } else vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void door_begin() {
  Preferences p;
  p.begin("doorbell", true);
  lastMotionT = (time_t)p.getULong("dLast", 0);       // die letzte Bewegung übersteht einen Neustart
  p.end();
  xTaskCreatePinnedToCore(task, "door", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(decode_task, "doordec", 8192, nullptr, 1, nullptr, 1);   // Kern 1 wie loop(); das Zeichnen läuft auf Kern 0
}
