#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "cfg.h"
#include "door.h"
#include "nuki.h"
#include "ui.h"
#include "net.h"
#include "ui_common.h"

// Einstellungen (im Speicher des Boards abgelegt) und Nachtmodus: nachts nur die gedimmte Uhr oder das Display ganz aus,
// eine Berührung weckt es für 30 Sekunden, ein Alarm sofort.
Prefs prefs = {true, 23 * 60, 6 * 60, false, 0, -1, 60, 0, 5, 40, 100};

void prefs_load() {
  Preferences p;
  p.begin("doorbell", true);
  prefs.nightOn = p.getBool("nOn", true);
  prefs.nightFrom = p.getInt("nFrom", 23 * 60);
  prefs.nightTo = p.getInt("nTo", 6 * 60);
  prefs.nightOff = p.getBool("nOff", false);
  prefs.theme = p.getInt("theme", 0);
  prefs.bg = p.getInt("bg", -1);
  prefs.dim = p.getInt("dim", 60);
  prefs.saver = p.getInt("saver", 0);
  prefs.saverMin = p.getInt("sMin", 5);
  prefs.bright = p.getInt("bri", 100);
  prefs.saverBright = p.getInt("sBri", 40);
  p.end();
}

void prefs_save() {
  Preferences p;
  p.begin("doorbell", false);
  p.putBool("nOn", prefs.nightOn);
  p.putInt("nFrom", prefs.nightFrom);
  p.putInt("nTo", prefs.nightTo);
  p.putBool("nOff", prefs.nightOff);
  p.putInt("theme", prefs.theme);
  p.putInt("bg", prefs.bg);
  p.putInt("dim", prefs.dim);
  p.putInt("saver", prefs.saver);
  p.putInt("sMin", prefs.saverMin);
  p.putInt("bri", prefs.bright);
  p.putInt("sBri", prefs.saverBright);
  p.end();
}

static lv_obj_t *swDoor, *vDoorSecs, *infoDoor, *infoNuki;
static lv_obj_t *swNight, *swOff, *fromL, *toL, *infoWifi, *infoIp, *infoFw, *infoMem, *infoSub, *infoWeb;
static lv_obj_t *vTheme, *vBg, *vDim, *vSaver, *vMin, *vBright, *vSBright, *infoPin;
static lv_obj_t *overlay, *ovClock, *ovDate;
static uint32_t wakeUntil = 0;
static bool overlayShown = false;

static void hhmm(char *b, size_t n, int m) { m = (m % 1440 + 1440) % 1440; snprintf(b, n, "%02d:%02d", m / 60, m % 60); }

static void sw_cb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  if (id == 3) { cfg.nuki = on; cfg_save_door(); nuki_refresh(); ui_refresh(); return; }
  if (id == 2) { cfg.door = on; cfg_save_door(); page_settings_refresh(); return; }
  if (id == 0) prefs.nightOn = on; else prefs.nightOff = on;
  prefs_save();
}

static const int MINUTES[] = {1, 2, 5, 10, 15, 30, 60};
static const int N_MIN = 7;
static const int DOOR_SECS[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300};
static const int N_DOOR = 11;

// Plätze mit Bild, davor "Aus": 0 = Aus, 1.. = Platz
static int bg_index() {
  if (prefs.bg < 0) return 0;
  int k = 0;
  for (int i = 0; i < IMG_SLOTS; i++) if (img_has(i)) { k++; if (i == prefs.bg) return k; }
  return 0;
}

static void step_cb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  const int d = id % 2 ? 1 : -1;
  if (id >= 20) {                              // Anzeigedauer des Türbilds
    int k = 0;
    for (int i = 0; i < N_DOOR; i++) if (DOOR_SECS[i] <= cfg.dsecs) k = i;
    cfg.dsecs = DOOR_SECS[constrain(k + d, 0, N_DOOR - 1)];
    cfg_save_door();
    page_settings_refresh();
    return;
  }
  if (id < 4) {
    int &v = id < 2 ? prefs.nightFrom : prefs.nightTo;
    v = ((v + d * 30) % 1440 + 1440) % 1440;
  } else if (id < 6) {
    prefs.theme = (prefs.theme + d + 3) % 3;
    prefs_save();
    ui_theme_changed();
    return;
  } else if (id < 8) {
    const int n = img_count();
    int k = (bg_index() + d + n + 1) % (n + 1);
    prefs.bg = -1;
    for (int i = 0, c = 0; k > 0 && i < IMG_SLOTS; i++) if (img_has(i) && ++c == k) { prefs.bg = i; break; }
    ui_bg_changed();
  } else if (id < 10) {
    prefs.dim = prefs.dim + d * 10 < 0 ? 0 : prefs.dim + d * 10 > 90 ? 90 : prefs.dim + d * 10;
    ui_bg_changed();
  } else if (id < 12 && id >= 10) {
    prefs.saver = (prefs.saver + d + 3) % 3;
  } else if (id >= 16) {
    prefs.saverBright = prefs.saverBright + d * 10 < 10 ? 10 : prefs.saverBright + d * 10 > 100 ? 100 : prefs.saverBright + d * 10;
  } else if (id >= 14) {
    prefs.bright = prefs.bright + d * 10 < 10 ? 10 : prefs.bright + d * 10 > 100 ? 100 : prefs.bright + d * 10;
    ui_bright_apply();
  } else {
    int k = 0;
    for (int i = 0; i < N_MIN; i++) if (MINUTES[i] == prefs.saverMin) k = i;
    prefs.saverMin = MINUTES[(k + d + N_MIN) % N_MIN];
  }
  prefs_save();
  page_settings_refresh();
}

// Deckende Karte statt Glas: Transparenz und Rand kosten beim Wischen viel Zeichenarbeit
static lv_obj_t *solid_card(lv_obj_t *p, int y, int h) {
  lv_obj_t *c = ui_glass(p, 32, y, 736, h, 16);
  lv_obj_set_style_bg_color(c, lv_color_mix(lv_color_hex(th.glassBg), lv_color_hex(th.bg), th.glassOpa), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  return c;
}

static lv_obj_t *row_card(lv_obj_t *p, int y, const char *glyph, const char *title, const char *sub) {
  lv_obj_t *c = solid_card(p, y, 62);
  ui_icon_w(c, fi28, C_ACC, glyph, 14, 14, 40);
  ui_label(c, f20, C_TEXT, title, 66, 8);
  ui_label(c, f14, C_DM, sub, 66, 36);
  return c;
}

static lv_obj_t *toggle(lv_obj_t *c, int id, bool on) {
  lv_obj_t *s = lv_switch_create(c);
  lv_obj_set_pos(s, 736 - 22 - 60, 15);
  lv_obj_set_size(s, 60, 32);
  if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s, sw_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
  return s;
}

static lv_obj_t *step_btn(lv_obj_t *c, int x, const char *t, int id) {
  return ui_button(c, x, 12, 40, 38, t, C_ACC, step_cb, (void *)(intptr_t)id);
}

// Zeile mit Wert in der Mitte und Pfeilen links und rechts (id = erster der beiden Knöpfe)
static lv_obj_t *stepper(lv_obj_t *p, int y, const char *glyph, const char *title, const char *sub, int id) {
  lv_obj_t *c = row_card(p, y, glyph, title, sub);
  step_btn(c, 462, "-", id);
  lv_obj_t *v = ui_label_w(c, f20, C_TEXT, " ", 506, 18, 176, LV_TEXT_ALIGN_CENTER);
  step_btn(c, 686 - 8, "+", id + 1);
  return v;
}

// Kacheln auf der Startansicht, dahinter je ein Untermenü ohne Scrollen
static lv_obj_t *home, *grp[6], *backBtn, *tileSub[6];
static int curGrp = -1;       // bleibt beim Neuaufbau (Themawechsel) erhalten
static const char *GRP_NAME[6] = {"Darstellung", "Bildschirmschoner", "Nachtmodus", "Dieses Display", "Türklingel", "Nuki-Schloss"};

static void show_grp(int g) {
  curGrp = g;
  if (home) { if (g < 0) lv_obj_clear_flag(home, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN); }
  for (int i = 0; i < 6; i++) { if (i == g) lv_obj_clear_flag(grp[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp[i], LV_OBJ_FLAG_HIDDEN); }
  if (g < 0) lv_obj_add_flag(backBtn, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(infoSub, g < 0 ? "Werden auf diesem Display gespeichert" : GRP_NAME[g]);
}

static void tile_cb(lv_event_t *e) { show_grp((int)(intptr_t)lv_event_get_user_data(e)); page_settings_refresh(); }
static void back_cb(lv_event_t *) { show_grp(-1); }

static lv_obj_t *tile_btn(lv_obj_t *p, int x, int y, const char *glyph, const char *title, int g) {
  lv_obj_t *c = solid_card(p, y, 104);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_width(c, 358);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)g);
  ui_icon(c, fi28, C_ACC, glyph, 24, 14);
  ui_label(c, f28, C_TEXT, title, 70, 10);
  tileSub[g] = ui_label_w(c, f14, C_DM, " ", 24, 66, 310, LV_TEXT_ALIGN_LEFT);
  return c;
}

static void info_tick();
static uint32_t resetAsk = 0;
static void reset_cb(lv_event_t *e) {
  lv_obj_t *l = lv_obj_get_child(lv_event_get_target(e), 0);
  if (resetAsk && millis() - resetAsk < 4000) { cfg_reset(); ESP.restart(); }
  resetAsk = millis();
  lv_label_set_text(l, "Wirklich?");
}
static void newpin_cb(lv_event_t *) { web_pin_new(); info_tick(); }

void page_settings_build(lv_obj_t *p) {
  ui_page_title(p, "Einstellungen", &infoSub);
  for (int i = 0; i < 6; i++) grp[i] = ui_plain(p, 0, 0, SCREEN_W, PAGE_H);
  const int dy = 68;

  // Darstellung: Thema, Helligkeit, Hintergrundbild, Abdunklung
  int y = 88;
  vTheme = stepper(grp[0], y, IC_PALETTE, "Darstellung", "Dunkel, hell oder automatisch (hell von 7 bis 19 Uhr)", 4); y += dy;
  vBright = stepper(grp[0], y, IC_SUN, "Helligkeit", "Dunkelt das Bild ab; die Beleuchtung selbst lässt sich nicht regeln", 14); y += dy;
  vBg = stepper(grp[0], y, IC_IMAGE, "Hintergrundbild", "Bilder lädst du über die Webseite des Displays hoch", 6); y += dy;
  vDim = stepper(grp[0], y, IC_CIRCLE_HALF, "Abdunklung des Bildes", "Je stärker, desto besser lesbar", 8);

  // Bildschirmschoner
  y = 88;
  vSaver = stepper(grp[1], y, IC_TV, "Bildschirmschoner", "Uhr oder Bilderschau, eine Berührung beendet ihn", 10); y += dy;
  vMin = stepper(grp[1], y, IC_CLOCK, "Starten nach", "Minuten ohne Berührung", 12); y += dy;
  vSBright = stepper(grp[1], y, IC_SUN, "Helligkeit im Schoner", "Dunkler als sonst, schont Display und Augen", 16); y += dy;
  lv_obj_t *up = solid_card(grp[1], y, 70);
  infoWeb = ui_icon_text(up, IC_IMAGE, C_ACC, " ", f14, C_TEXT, 22, 12);
  ui_label(up, f14, C_DM, "Die Bilder für Hintergrund und Bilderschau stehen im Display (bis zu acht)", 22, 38);

  // Nachtmodus
  y = 88;
  lv_obj_t *c1 = row_card(grp[2], y, IC_MOON, "Nachtmodus", "Nachts nur die Uhr, Berührung weckt das Display");
  swNight = toggle(c1, 0, prefs.nightOn); y += dy;
  lv_obj_t *c2 = row_card(grp[2], y, IC_CLOCK, "Zeitraum", "von / bis in 30-Minuten-Schritten");
  step_btn(c2, 330, "-", 0);
  fromL = ui_label_w(c2, f20, C_TEXT, "23:00", 374, 18, 70, LV_TEXT_ALIGN_CENTER);
  step_btn(c2, 448, "+", 1);
  ui_label(c2, f16, C_MU, "bis", 506, 20);
  step_btn(c2, 542, "-", 2);
  toL = ui_label_w(c2, f20, C_TEXT, "06:00", 586, 18, 70, LV_TEXT_ALIGN_CENTER);
  step_btn(c2, 660, "+", 3); y += dy;
  lv_obj_t *c3 = row_card(grp[2], y, IC_TV, "Display ausschalten", "Nachts die Beleuchtung ganz abschalten, statt die Uhr zu zeigen");
  swOff = toggle(c3, 1, prefs.nightOff);

  // Türklingel
  y = 88;
  lv_obj_t *cd = row_card(grp[4], y, IC_BELL, "Live-Bild bei Bewegung", "Zeigt das Bild der Klingel, Berührung schließt es");
  swDoor = toggle(cd, 2, cfg.door); y += dy;
  vDoorSecs = stepper(grp[4], y, IC_HOURGLASS, "Anzeigedauer", "Ab der letzten erkannten Bewegung", 20); y += dy;
  lv_obj_t *cs = solid_card(grp[4], y, 96);
  infoDoor = ui_icon_text(cs, IC_BELL, C_ACC, " ", f14, C_TEXT, 22, 12);
  ui_label(cs, f14, C_DM, "Adresse und Zugang der Klingel trägst du auf der Webseite des Displays ein", 22, 38);
  ui_button(cs, 22, 58, 170, 32, "Jetzt testen", C_ACC, [](lv_event_t *) { door_test(); }, nullptr);

  // Nuki-Schloss
  y = 88;
  lv_obj_t *ck = row_card(grp[5], y, IC_SHIELD, "Schloss auf der Startseite", "Zeigt Zustand, Tür und Batterie deines Nuki-Schlosses");
  toggle(ck, 3, cfg.nuki); y += dy;
  lv_obj_t *cn = solid_card(grp[5], y, 96);
  infoNuki = ui_icon_text(cn, IC_SHIELD, C_ACC, " ", f14, C_TEXT, 22, 12);
  ui_label(cn, f14, C_DM, "Den API-Token (web.nuki.io, API & Integrationen) trägst du auf der Webseite des Displays ein", 22, 38);
  ui_button(cn, 22, 58, 170, 32, "Jetzt abfragen", C_ACC, [](lv_event_t *) { nuki_refresh(); }, nullptr);

  // Dieses Display
  lv_obj_t *info = solid_card(grp[3], 96, 148);
  ui_icon_text(info, IC_TV, C_ACC, "Dieses Display", f14, C_MU, 22, 10);
  infoWifi = ui_icon_text(info, IC_WIFI, C_ACC, " ", f14, C_TEXT, 22, 34);
  infoIp = ui_icon_text(info, IC_NETWORK_WIRED, C_ACC, " ", f14, C_TEXT, 22, 58);
  infoFw = ui_icon_text(info, IC_MICROCHIP, C_ACC, " ", f14, C_TEXT, 22, 82);
  infoMem = ui_icon_text(info, IC_MEMORY, C_ACC, " ", f14, C_TEXT, 400, 34);
  infoPin = ui_icon_text(info, IC_LINK, C_ACC, " ", f14, C_TEXT, 400, 82);
  ui_button(info, 400, 106, 130, 32, "Neue PIN", C_ACC, newpin_cb, nullptr);
  ui_button(info, 544, 106, 170, 32, "Neu einrichten", C_WARM, reset_cb, nullptr);

  // Startansicht mit vier Kacheln
  home = ui_plain(p, 0, 0, SCREEN_W, PAGE_H);
  tile_btn(home, 32, 84, IC_PALETTE, GRP_NAME[0], 0);
  tile_btn(home, 410, 84, IC_TV, GRP_NAME[1], 1);
  tile_btn(home, 32, 204, IC_MOON, GRP_NAME[2], 2);
  tile_btn(home, 410, 204, IC_MICROCHIP, GRP_NAME[3], 3);
  tile_btn(home, 32, 324, IC_BELL, GRP_NAME[4], 4);
  tile_btn(home, 410, 324, IC_SHIELD, GRP_NAME[5], 5);

  backBtn = ui_button(p, 440, 16, 140, 40, "Zurück", C_ACC, back_cb, nullptr);
  show_grp(curGrp);
}

void page_settings_refresh() {
  if (!fromL) return;
  char b[48];
  hhmm(b, sizeof b, prefs.nightFrom);
  lv_label_set_text(fromL, b);
  hhmm(b, sizeof b, prefs.nightTo);
  lv_label_set_text(toL, b);
  lv_label_set_text(vTheme, prefs.theme == 0 ? "Dunkel" : prefs.theme == 1 ? "Hell" : "Automatisch");
  const int k = bg_index();
  if (img_count() == 0) lv_label_set_text(vBg, "Keine Bilder");
  else if (k == 0) lv_label_set_text(vBg, "Aus");
  else { snprintf(b, sizeof b, "Bild %d", prefs.bg + 1); lv_label_set_text(vBg, b); }
  snprintf(b, sizeof b, "%d %%", prefs.bright);
  lv_label_set_text(vBright, b);
  snprintf(b, sizeof b, "%d %%", prefs.saverBright);
  lv_label_set_text(vSBright, b);
  snprintf(b, sizeof b, "%d %%", prefs.dim);
  lv_label_set_text(vDim, b);
  lv_label_set_text(vSaver, prefs.saver == 0 ? "Aus" : prefs.saver == 1 ? "Uhr" : "Bilder");
  snprintf(b, sizeof b, "%d min", prefs.saverMin);
  lv_label_set_text(vMin, b);
  snprintf(b, sizeof b, "%d s", cfg.dsecs);
  lv_label_set_text(vDoorSecs, b);
  if (cfg.door) lv_obj_add_state(swDoor, LV_STATE_CHECKED); else lv_obj_clear_state(swDoor, LV_STATE_CHECKED);
  if (tileSub[0]) {
    char t[64];
    snprintf(t, sizeof t, "%s · Helligkeit %d %%", prefs.theme == 0 ? "Dunkel" : prefs.theme == 1 ? "Hell" : "Automatisch", prefs.bright);
    lv_label_set_text(tileSub[0], t);
    snprintf(t, sizeof t, prefs.saver == 0 ? "Aus" : "%s nach %d min", prefs.saver == 1 ? "Uhr" : "Bilder", prefs.saverMin);
    lv_label_set_text(tileSub[1], t);
    if (!prefs.nightOn) lv_label_set_text(tileSub[2], "Aus");
    else { char f[8], u[8]; hhmm(f, sizeof f, prefs.nightFrom); hhmm(u, sizeof u, prefs.nightTo); snprintf(t, sizeof t, "%s bis %s%s", f, u, prefs.nightOff ? " · Display aus" : ""); lv_label_set_text(tileSub[2], t); }
    lv_label_set_text(tileSub[3], "WLAN, Adresse, Firmware, Speicher");
    snprintf(t, sizeof t, cfg.door ? "An · %d s" : "Aus", cfg.dsecs);
    lv_label_set_text(tileSub[4], t);
    lv_label_set_text(tileSub[5], cfg.nuki ? (cfg.ntoken.length() ? "An" : "An · Token fehlt") : "Aus");
  }
}

static void info_tick() {
  if (!infoWifi) return;
  char b[96];
  if (WiFi.status() == WL_CONNECTED) snprintf(b, sizeof b, "WLAN: %s (%d dBm)", WiFi.SSID().c_str(), WiFi.RSSI());
  else snprintf(b, sizeof b, "WLAN: nicht verbunden");
  lv_label_set_text(infoWifi, b);
  snprintf(b, sizeof b, "Adresse: %s", WiFi.localIP().toString().c_str());
  lv_label_set_text(infoIp, b);
  snprintf(b, sizeof b, "Firmware: %s %s", __DATE__, __TIME__);
  lv_label_set_text(infoFw, b);
  snprintf(b, sizeof b, "Freier Speicher: %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  lv_label_set_text(infoMem, b);
  snprintf(b, sizeof b, "Web-PIN: %s (Benutzer: display)", web_pin().c_str());
  lv_label_set_text(infoPin, b);
  if (WiFi.status() != WL_CONNECTED) snprintf(b, sizeof b, "Bilder hochladen: erst mit WLAN möglich");
  else snprintf(b, sizeof b, "Bilder hochladen: http://%s/  (%d von %d Plätzen belegt)", WiFi.localIP().toString().c_str(), img_count(), IMG_SLOTS);
  lv_label_set_text(infoWeb, b);
  snprintf(b, sizeof b, "Klingel: %s · %s", cfg.dhost.length() ? cfg.dhost.c_str() : "keine Adresse", door_status());
  lv_label_set_text(infoDoor, b);
  const NukiInfo n = nuki_get();
  if (!cfg.nuki) snprintf(b, sizeof b, "Nuki: aus");
  else if (!cfg.ntoken.length()) snprintf(b, sizeof b, "Nuki: Token fehlt");
  else if (n.err[0]) snprintf(b, sizeof b, "Nuki: %s", n.err);
  else if (n.valid) snprintf(b, sizeof b, "Nuki: %s · %s", n.name, nuki_state_text(n.state));
  else snprintf(b, sizeof b, "Nuki: wird abgefragt …");
  lv_label_set_text(infoNuki, b);
}

void page_tick() { info_tick(); }

static void wake_cb(lv_event_t *) { wakeUntil = millis() + 30000; }

void ui_night_init(lv_obj_t *) {
  overlay = ui_plain(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(overlay, wake_cb, LV_EVENT_PRESSED, nullptr);
  ovClock = ui_label_w(overlay, fClock, 0x2b3250, "--:--", 0, 150, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  ovDate = ui_label_w(overlay, f20, 0x232944, " ", 0, 250, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  overlayShown = false;
  display_backlight(true);
}

bool ui_night_shown() { return overlayShown; }
void ui_night_restore() { display_backlight(!(overlayShown && prefs.nightOff)); }

static bool is_night(const struct tm &t) {
  const int a = prefs.nightFrom, b = prefs.nightTo, n = t.tm_hour * 60 + t.tm_min;
  if (a == b) return false;
  return a < b ? (n >= a && n < b) : (n >= a || n < b);
}

void ui_night_tick() {
  struct tm t;
  bool night = false;
  if (net_time_ok()) {
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    night = prefs.nightOn && is_night(t);
    if (overlayShown) {
      char b[40];
      snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
      lv_label_set_text(ovClock, b);
      static const char *WD[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
      static const char *MO[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
      snprintf(b, sizeof b, "%s, %d. %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
      lv_label_set_text(ovDate, b);
    }
  }
  const bool show = night && (int32_t)(millis() - wakeUntil) > 0;
  if (show != overlayShown) {
    overlayShown = show;
    if (show) lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    display_backlight(!(show && prefs.nightOff));
  }
}

void ui_prefs_applied() {
  ui_bright_apply();
  ui_bg_changed();
  if (ui_theme_light_wanted() != th.light) ui_theme_changed();
  page_settings_refresh();
}

// Software-Helligkeit: eine schwarze Fläche über allem; die Beleuchtung des Boards kennt nur an und aus.
void ui_bright_apply() {
  static lv_obj_t *veil = nullptr;
  if (!veil) {
    veil = ui_plain(lv_layer_sys(), 0, 0, SCREEN_W, SCREEN_H);
    lv_obj_clear_flag(veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(veil, lv_color_black(), 0);
  }
  lv_obj_set_style_bg_opa(veil, (lv_opa_t)((100 - prefs.bright) * 255 / 100), 0);
}

// Einrichtungsanzeige: deckt alles ab, solange das Display noch kein WLAN kennt
void ui_setup_show() {
  lv_obj_t *o = ui_plain(lv_layer_sys(), 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(o, lv_color_hex(0x0b1226), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  ui_icon_w(o, fi28, 0x6cb8ff, IC_WIFI, 0, 40, SCREEN_W);
  ui_label_w(o, f36b, 0xeef0f4, "Einrichtung", 0, 84, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  char b[96];
  ui_label_w(o, f20, 0xa9b4d0, "1. Mit diesem WLAN verbinden (Handy oder Rechner):", 0, 160, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  ui_label_w(o, f28, 0xeef0f4, "Doorbell-Display-Setup", 0, 192, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  snprintf(b, sizeof b, "Kennwort: tuer%s", web_pin().c_str());
  ui_label_w(o, f28, 0x6cb8ff, b, 0, 232, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  ui_label_w(o, f20, 0xa9b4d0, "2. Im Browser öffnen:", 0, 296, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  ui_label_w(o, f28, 0xeef0f4, "http://192.168.4.1", 0, 328, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  ui_label_w(o, f16, 0x7d8aa8, "3. WLAN eintragen (Klingel später) und speichern", 0, 396, SCREEN_W, LV_TEXT_ALIGN_CENTER);
}
