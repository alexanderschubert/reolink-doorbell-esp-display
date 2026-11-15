#include "door.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <rom/tjpgd.h>

#include "cfg.h"
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
static WiFiClient client;
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

static String api(const char *cmd, const char *extra) {
  return String("http://") + cfg.dhost + "/api.cgi?cmd=" + cmd + "&channel=0" + extra + "&user=" + enc(cfg.duser) + "&password=" + enc(cfg.dpass);
}

// Antwort der Klingel holen; Kopf auswerten. Rückgabe: Länge oder -1.
static int fetch(const String &url, uint8_t *dst, size_t cap) {
  http.setReuse(true);
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(client, url)) return -1;
  const int code = http.GET();
  if (code != 200) { set_status("Klingel antwortet mit %d", code); http.end(); client.stop(); return -1; }
  const int len = http.getSize();
  if (len <= 0 || (size_t)len > cap) { set_status("Antwort unbrauchbar (%d Byte)", len); http.end(); client.stop(); return -1; }
  WiFiClient *s = http.getStreamPtr();
  int got = 0;
  const uint32_t t0 = millis();
  while (got < len && millis() - t0 < 3000) {
    const int n = s->readBytes(dst + got, len - got);
    if (n > 0) got += n; else if (!s->connected()) break; else delay(2);
  }
  http.end();                                // bei reuse bleibt die Verbindung offen
  if (got != len) { client.stop(); set_status("Antwort unvollständig"); return -1; }
  return got;
}

// 1 Bewegung, 0 ruhig, -1 Fehler
static int poll_motion() {
  static bool useMd = false;
  static uint8_t body[768];
  for (int tries = 0; tries < 2; tries++) {
    const int n = fetch(api(useMd ? "GetMdState" : "GetEvents", ""), body, sizeof body - 1);
    if (n < 0) return -1;
    body[n] = 0;
    const char *b = (const char *)body;
    if (!strstr(b, "\"code\" : 0") && !strstr(b, "\"code\":0")) {          // Befehl unbekannt oder Anmeldung abgelehnt
      if (strstr(b, "rspCode\" : -6") || strstr(b, "rspCode\":-6") || strstr(b, "login failed") || strstr(b, "please login")) { set_status("Anmeldung an der Klingel abgelehnt"); return -1; }
      useMd = !useMd;
      continue;
    }
    set_status("verbunden");
    if (useMd) return strstr(b, "\"state\" : 1") || strstr(b, "\"state\":1") ? 1 : 0;
    // GetEvents: Bewegung (md) oder erkannte Person bzw. Besucher (Klingeltaste)
    return strstr(b, "\"state\" : 1") || strstr(b, "\"state\":1") || strstr(b, "\"visitor\" : 1") || strstr(b, "\"visitor\":1") ||
           strstr(b, "\"people\" : 1") || strstr(b, "\"people\":1") ? 1 : 0;
  }
  set_status("Klingel versteht die Abfrage nicht");
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

static bool grab_frame(int wr) {
  String url = cfg.durl.length() ? cfg.durl : api("Snap", (String("&snapType=sub&rs=") + String((uint32_t)esp_random(), HEX)).c_str());
  const int n = fetch(url, jpg, JPG_MAX);
  if (n < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) { if (n >= 0) set_status("Antwort ist kein JPEG"); return false; }
  int w, h;
  if (!decode(n, buf[wr], &w, &h)) return false;
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
      if (alloc_buffers()) { want = true; start = lastMotion = now; noFrameSince = now; seq = 0; pubIdx = -1; endAt = now + cfg.dsecs * 1000u; }
    }
    if (now - lastPoll >= (want ? 1500u : 1000u)) {
      lastPoll = now;
      const int m = poll_motion();
      if (m == 0) armed = true;
      if (m == 1) {
        lastMotion = millis();
        if (net_time_ok()) lastMotionT = time(nullptr);
        if (!want && armed && alloc_buffers()) {
          want = true; start = lastMotion; noFrameSince = lastMotion; seq = 0; pubIdx = -1;
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
