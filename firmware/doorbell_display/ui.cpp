#include "ui.h"

#include <time.h>

#include "font_icons.h"
#include "font_lato_bold.h"
#include "font_lato_regular.h"
#include "cfg.h"
#include "door.h"
#include "net.h"
#include "nuki.h"
#include "ui_common.h"

const lv_font_t *f12, *f14, *f16, *f20, *f28, *f36b, *fClock, *fHome;
const lv_font_t *fi14, *fi16, *fi20, *fi28;

static const char *PAGE_NAME[2] = {"Start", "Einstellungen"};
static const char *PAGE_ICON[2] = {IC_HOUSE, IC_GEAR};

static lv_obj_t *page[2];
static lv_obj_t *navIcon[2], *navText[2];

// Startseite: große Uhr, zwei Status-Chips (Schloss, Türspion) und drei runde Tasten
static lv_obj_t *clockL, *dateL, *pillN, *pillD, *doorIcon, *doorL, *nukiIcon, *nukiState, *nukiNote;
static lv_obj_t *btnLock, *btnLive, *btnUnlock, *modal, *modalTitle;

static int curPage = 0;
static void refresh_page(int i);

static void show_page(int i) {
  curPage = i;
  for (int p = 0; p < 2; p++) {
    if (p == i) lv_obj_clear_flag(page[p], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page[p], LV_OBJ_FLAG_HIDDEN);
    const uint32_t c = p == i ? C_ACC : C_DM;
    lv_obj_set_style_text_color(navIcon[p], lv_color_hex(c), 0);
    lv_obj_set_style_text_color(navText[p], lv_color_hex(c), 0);
  }
  refresh_page(i);
}

static void nav_cb(lv_event_t *e) { show_page((int)(intptr_t)lv_event_get_user_data(e)); }
static void show_cb(lv_event_t *) { door_test(); }

static uint32_t modalSince = 0;
static void noop_cb(lv_event_t *) {}
static void lock_cb(lv_event_t *) { nuki_action(false); lv_label_set_text(nukiNote, "Zuschließen wird gesendet …"); }
static void unlock_ask_cb(lv_event_t *) { modalSince = millis(); lv_obj_clear_flag(modal, LV_OBJ_FLAG_HIDDEN); }
static void cancel_cb(lv_event_t *) { lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN); }
static void unlock_go_cb(lv_event_t *) { lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN); nuki_action(true); lv_label_set_text(nukiNote, "Aufschließen wird gesendet …"); }

// Runde Taste: Symbol oben, darunter die Beschriftung
static lv_obj_t *round_btn(lv_obj_t *p, int x, int y, const char *glyph, const char *text, uint32_t color, lv_event_cb_t cb) {
  const int d = 92;
  lv_obj_t *b = ui_button(p, x, y, d, d, "", color, cb, nullptr);
  lv_obj_clear_flag(lv_obj_get_child(b, 0), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *i = ui_icon_w(b, fi28, C_TEXT, glyph, 0, 16, d - 4);
  lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *t = ui_label_w(b, f14, C_TEXT, text, 0, 58, d - 4, LV_TEXT_ALIGN_CENTER);
  lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
  return b;
}

// Status-Chip: Symbol und eine Zeile Text
static lv_obj_t *pill(lv_obj_t *p, int x, lv_obj_t **icon, lv_obj_t **text) {
  lv_obj_t *c = ui_glass(p, x, 186, 370, 48, 24);
  *icon = ui_icon(c, fi20, C_DM, IC_BELL, 18, 12);
  *text = ui_label_w(c, f16, C_TEXT, " ", 54, 11, 300, LV_TEXT_ALIGN_LEFT);
  lv_label_set_long_mode(*text, LV_LABEL_LONG_DOT);
  lv_obj_set_height(*text, 24);
  return c;
}

static void build_start(lv_obj_t *p) {
  clockL = ui_label_w(p, fHome, C_TEXT, "--:--", 0, -4, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  dateL = ui_label_w(p, f28, C_MU, " ", 0, 128, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  pillN = pill(p, 20, &nukiIcon, &nukiState);
  pillD = pill(p, 410, &doorIcon, &doorL);
  btnLock = round_btn(p, 204, 256, IC_LOCK, "Zu", C_OK, lock_cb);
  btnLive = round_btn(p, 354, 256, IC_VIDEO, "Live", C_ACC, show_cb);
  btnUnlock = round_btn(p, 504, 256, IC_LOCK_OPEN, "Auf", C_WARM, unlock_ask_cb);
  nukiNote = ui_label_w(p, f14, C_DM, " ", 0, 366, SCREEN_W, LV_TEXT_ALIGN_CENTER);

  // Rückfrage zum Aufschließen: gedrückt halten, damit nichts aus Versehen aufgeht
  modal = ui_plain(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *mc = ui_plain(modal, 140, 110, 520, 250);
  lv_obj_set_style_radius(mc, 20, 0);
  lv_obj_set_style_bg_color(mc, lv_color_hex(0x1b2238), 0);
  lv_obj_set_style_bg_opa(mc, LV_OPA_COVER, 0);
  modalTitle = ui_label_w(mc, f28, 0xeef0f4, "Tür aufschließen?", 0, 28, 520, LV_TEXT_ALIGN_CENTER);
  ui_label_w(mc, f16, 0xa9b4d0, "Zum Bestätigen die Taste gedrückt halten", 0, 84, 520, LV_TEXT_ALIGN_CENTER);
  lv_obj_t *go = ui_button_i(mc, 30, 150, 270, 60, IC_LOCK_OPEN, "Gedrückt halten", C_WARM, noop_cb, nullptr);
  lv_obj_add_event_cb(go, unlock_go_cb, LV_EVENT_LONG_PRESSED, nullptr);
  ui_button(mc, 320, 150, 170, 60, "Abbrechen", C_ACC, cancel_cb, nullptr);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
}

// TinyTTF legt den Cache für (Cachegröße / Schriftgröße²) Zeichen an: zu klein gewählt sind es null Einträge und das
// Programm teilt durch null. Darum bekommt jede Schrift Platz für eine feste Zahl Zeichen.
static const lv_font_t *ttf(const unsigned char *data, size_t len, int size) {
  const int glyphs = size <= 20 ? 160 : size <= 40 ? 80 : 16;
  return lv_tiny_ttf_create_data_ex(data, len, size, (size_t)size * size * glyphs);
}

static lv_obj_t *bgImg, *bgDim;
static lv_img_dsc_t bgDsc;
static uint8_t *bgBuf = nullptr;
static int bgLoaded = -2;
static bool bgDirty = true, rebuildPending = false;
static uint32_t bgRev = 0;

// Hintergrundbild und Abdunklung nach den Einstellungen setzen (unter der LVGL-Sperre)
static void bg_apply() {
  bgDirty = false;
  int slot = prefs.bg;
  if (slot >= 0 && !img_has(slot)) slot = -1;
  if (slot >= 0 && !bgBuf) bgBuf = img_alloc();
  if (slot >= 0 && !bgBuf) slot = -1;
  if (slot >= 0 && slot != bgLoaded) {
    if (img_read(slot, bgBuf)) {
      bgDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
      bgDsc.header.w = SCREEN_W;
      bgDsc.header.h = SCREEN_H;
      bgDsc.data_size = IMG_BYTES;
      bgDsc.data = bgBuf;
      bgLoaded = slot;
      lv_img_set_src(bgImg, &bgDsc);
      lv_obj_invalidate(bgImg);
    } else slot = -1;
  }
  if (slot < 0) bgLoaded = -2;
  if (slot >= 0) { lv_obj_clear_flag(bgImg, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(bgDim, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(bgImg, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(bgDim, LV_OBJ_FLAG_HIDDEN); }
  lv_obj_set_style_bg_color(bgDim, lv_color_hex(th.bg), 0);
  lv_obj_set_style_bg_opa(bgDim, (lv_opa_t)(prefs.dim * 255 / 100), 0);
}

void ui_bg_changed() { bgDirty = true; }

bool ui_theme_light_wanted() {
  if (prefs.theme == 1) return true;
  if (prefs.theme == 0) return false;
  if (!net_time_ok()) return th.light;
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_hour >= 7 && tm.tm_hour < 19;
}

static void build_all() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  bgImg = lv_img_create(scr);
  lv_obj_set_pos(bgImg, 0, 0);
  lv_obj_clear_flag(bgImg, LV_OBJ_FLAG_CLICKABLE);
  bgDim = ui_plain(scr, 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(bgDim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(bgDim, LV_OPA_COVER, 0);
  bgLoaded = -2;
  bg_apply();

  for (int i = 0; i < 2; i++) page[i] = ui_plain(scr, 0, 0, SCREEN_W, PAGE_H);
  build_start(page[0]);
  page_settings_build(page[1]);

  lv_obj_t *nav = ui_plain(scr, 0, SCREEN_H - 56, SCREEN_W, 56);
  lv_obj_set_style_bg_color(nav, lv_color_hex(th.nav), 0);
  lv_obj_set_style_bg_opa(nav, th.navOpa, 0);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = ui_plain(nav, i * 400, 0, 400, 56);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    navIcon[i] = ui_icon_w(b, fi20, C_DM, PAGE_ICON[i], 0, 6, 400);
    navText[i] = ui_label_w(b, f12, C_DM, PAGE_NAME[i], 0, 33, 400, LV_TEXT_ALIGN_CENTER);
  }
  ui_saver_init(scr);
  ui_night_init(scr);
  ui_bright_apply();
}

static void rebuild_cb(void *) {
  rebuildPending = false;
  const int cur = curPage;
  ui_clock_reset();
  lv_obj_clean(lv_layer_top());
  lv_obj_clean(lv_scr_act());
  build_all();
  show_page(cur);
  ui_refresh();
  ui_tick();
}

void ui_theme_changed() {
  if (rebuildPending) return;
  ui_theme_apply(ui_theme_light_wanted());
  rebuildPending = true;
  lv_async_call(rebuild_cb, nullptr);       // nicht aus dem Ereignis heraus: das auslösende Objekt wird dabei gelöscht
}

void ui_init() {
  f12 = ttf(font_lato_regular, font_lato_regular_len, 12);
  f14 = ttf(font_lato_regular, font_lato_regular_len, 14);
  f16 = ttf(font_lato_regular, font_lato_regular_len, 16);
  f20 = ttf(font_lato_regular, font_lato_regular_len, 20);
  f28 = ttf(font_lato_regular, font_lato_regular_len, 28);
  f36b = ttf(font_lato_bold, font_lato_bold_len, 36);
  fClock = ttf(font_lato_regular, font_lato_regular_len, 72);
  fHome = ttf(font_lato_regular, font_lato_regular_len, 112);
  fi14 = ttf(font_icons, font_icons_len, 14);
  fi16 = ttf(font_icons, font_icons_len, 16);
  fi20 = ttf(font_icons, font_icons_len, 20);
  fi28 = ttf(font_icons, font_icons_len, 28);
  for (lv_indev_t *d = lv_indev_get_next(nullptr); d; d = lv_indev_get_next(d)) d->driver->long_press_time = 900;   // Gedrückthalten zum Bestätigen
  prefs_load();
  ui_theme_apply(prefs.theme == 1);
  ui_door_init();                           // vor build_all(): die Helligkeits-Schicht muss darüber liegen
  build_all();
  show_page(0);
  ui_refresh();
  ui_tick();
}

static void refresh_start();
static void refresh_nuki();

void ui_tick() {
  if (!clockL) return;
  static const char *WD[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
  static const char *MO[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
  char buf[48];
  if (net_time_ok()) {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(clockL, buf);
    ui_set_clock_text(buf);
    snprintf(buf, sizeof buf, "%s, %d. %s", WD[tm.tm_wday], tm.tm_mday, MO[tm.tm_mon]);
    lv_label_set_text(dateL, buf);
  } else {
    lv_label_set_text(clockL, "--:--");
    ui_set_clock_text("--:--");
    lv_label_set_text(dateL, net_wifi_ok() ? "Uhrzeit wird geholt …" : "Suche das WLAN …");
  }
  if (!rebuildPending && ui_theme_light_wanted() != th.light) ui_theme_changed();   // Automatik: Wechsel zwischen hell und dunkel
  if (bgDirty || imgRevision != bgRev) { bgRev = imgRevision; bg_apply(); }
  ui_saver_tick();
  ui_night_tick();
  page_tick();
  if (curPage == 0) { refresh_start(); refresh_nuki(); }
}

static void refresh_start() {
  char b[96];
  const char *st = door_status();
  const bool ok = cfg.door && !strcmp(st, "verbunden");
  lv_obj_set_style_text_color(doorIcon, lv_color_hex(ok ? C_OK : C_DM), 0);
  if (!net_wifi_ok()) lv_label_set_text(doorL, "Kein WLAN");
  else if (!cfg.door) lv_label_set_text(doorL, "Türspion aus (Einstellungen)");
  else if (!cfg.dhost.length()) lv_label_set_text(doorL, "Adresse fehlt (Webseite des Displays)");
  else if (!ok) { snprintf(b, sizeof b, "Türspion: %s", st); lv_label_set_text(doorL, b); }
  else if (!door_last_motion()) lv_label_set_text(doorL, "Noch keine Bewegung erkannt");
  else {
    char lt[64];
    door_last_text(lt, sizeof lt);
    snprintf(b, sizeof b, "Bewegung %s", lt);
    lv_label_set_text(doorL, b);
  }
}

static void refresh_nuki() {
  char b[96];
  const bool on = cfg.nuki;
  const NukiInfo n = nuki_get();
  const bool ctl = on && cfg.nctl && cfg.ntoken.length() && n.valid;
  // Anordnung: ohne Schloss steht der Türspion-Chip in der Mitte, ohne Bedienung bleibt nur "Live"
  if (on) lv_obj_clear_flag(pillN, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(pillN, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_x(pillD, on ? 410 : 215);
  if (ctl) { lv_obj_clear_flag(btnLock, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(btnUnlock, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(btnLock, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(btnUnlock, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN); }
  if (!lv_obj_has_flag(modal, LV_OBJ_FLAG_HIDDEN) && millis() - modalSince > 15000) lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);   // Rückfrage nach 15 s von selbst weg
  if (!on) { lv_label_set_text(nukiNote, " "); return; }
  lv_obj_set_style_text_color(nukiState, lv_color_hex(C_TEXT), 0);
  if (!cfg.ntoken.length()) {
    lv_label_set_text(nukiState, "Schloss: Token fehlt");
    lv_label_set_text(nukiNote, "Token auf der Webseite des Displays eintragen");
    lv_obj_set_style_text_color(nukiIcon, lv_color_hex(C_DM), 0);
    return;
  }
  if (!n.valid) {
    lv_label_set_text(nukiState, n.err[0] ? "Schloss: keine Daten" : "Schloss wird abgefragt …");
    lv_label_set_text(nukiNote, n.err);
    lv_obj_set_style_text_color(nukiIcon, lv_color_hex(C_DM), 0);
    return;
  }
  const bool locked = n.state == 1, bad = n.state == 254 || n.state == 0;
  snprintf(b, sizeof b, "%s · %s", n.name[0] ? n.name : "Schloss", nuki_state_text(n.state));
  lv_label_set_text(nukiState, b);
  lv_label_set_text(nukiIcon, locked ? IC_LOCK : IC_LOCK_OPEN);
  lv_obj_set_style_text_color(nukiIcon, lv_color_hex(bad ? C_HOT : locked ? C_OK : C_WARM), 0);
  lv_obj_set_style_text_color(nukiState, lv_color_hex(bad ? C_HOT : C_TEXT), 0);
  // Unterzeile: Rückmeldung eines Befehls, sonst Tür, Batterie und Alter der Daten
  const uint32_t age = (millis() - n.at) / 1000;
  if (n.msg[0] && millis() - n.msgAt < 12000) snprintf(b, sizeof b, "%s", n.msg);
  else if (n.err[0]) snprintf(b, sizeof b, "%s", n.err);
  else {
    const char *door = n.door == 2 ? "Tür geschlossen" : n.door == 3 ? "Tür offen" : "kein Türsensor";
    if (n.battery >= 0) snprintf(b, sizeof b, "%s · Batterie %d %%%s · Stand vor %u s", door, n.battery, n.critical ? " (schwach!)" : "", (unsigned)age);
    else snprintf(b, sizeof b, "%s · Stand vor %u s", door, (unsigned)age);
  }
  lv_label_set_text(nukiNote, b);
}

static void refresh_page(int i) {
  if (i == 0) { refresh_start(); refresh_nuki(); } else page_settings_refresh();
}

void ui_refresh() {
  if (!clockL) return;
  refresh_page(curPage);
}
