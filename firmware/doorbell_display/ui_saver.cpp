#include <time.h>

#include "cfg.h"
#include "door.h"
#include "net.h"
#include "ui_common.h"

// Bildschirmschoner: nach einiger Zeit ohne Berührung entweder eine wandernde Uhr auf schwarzem Grund oder eine
// Bilderschau mit Uhr. Eine Berührung beendet ihn.
static lv_obj_t *ss, *ssImg, *ssBox, *ssClock, *ssDate, *ssDim, *ssIcon, *ssStat;
static lv_img_dsc_t ssDsc;
static uint8_t *ssBuf = nullptr;
static bool shown = false;
static uint32_t nextSlide = 0;
static int slide = -1, lastMinute = -1, moves = 0;

static void wake_cb(lv_event_t *) {
  lv_disp_trig_activity(NULL);
  lv_obj_add_flag(ss, LV_OBJ_FLAG_HIDDEN);
  shown = false;
}

void ui_saver_init(lv_obj_t *) {
  ss = ui_plain(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(ss, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ss, LV_OPA_COVER, 0);
  lv_obj_add_flag(ss, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ss, wake_cb, LV_EVENT_PRESSED, nullptr);
  ssImg = lv_img_create(ss);
  lv_obj_set_pos(ssImg, 0, 0);
  lv_obj_clear_flag(ssImg, LV_OBJ_FLAG_CLICKABLE);
  ssBox = ui_plain(ss, 180, 90, 440, 260);
  lv_obj_clear_flag(ssBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(ssBox, 24, 0);
  ssClock = ui_label_w(ssBox, fClock, 0xffffff, "--:--", 0, 14, 440, LV_TEXT_ALIGN_CENTER);
  ssDate = ui_label_w(ssBox, f20, 0xd0d6e6, " ", 0, 108, 440, LV_TEXT_ALIGN_CENTER);
  // Kachel mit Meldungen und Updates
  lv_obj_t *tile = ui_plain(ssBox, 50, 168, 340, 56);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(tile, 28, 0);
  lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_10, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_border_color(tile, lv_color_white(), 0);
  lv_obj_set_style_border_opa(tile, LV_OPA_20, 0);
  ssIcon = ui_icon(tile, fi20, 0x22c55e, IC_BELL, 24, 16);
  ssStat = ui_label_w(tile, f20, 0xd0d6e6, " ", 60, 14, 270, LV_TEXT_ALIGN_LEFT);
  // Abdunklung über allem im Schoner, damit er dunkler ist als der normale Betrieb
  ssDim = ui_plain(ss, 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(ssDim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(ssDim, lv_color_black(), 0);
  lv_obj_add_flag(ss, LV_OBJ_FLAG_HIDDEN);
  shown = false;
  slide = -1;
  lastMinute = -1;
}

// Das nächste vorhandene Bild laden (unter der LVGL-Sperre; das Lesen dauert einen Moment)
static bool next_image() {
  if (!ssBuf) ssBuf = img_alloc();
  if (!ssBuf) return false;
  for (int k = 1; k <= IMG_SLOTS; k++) {
    const int s = (slide + k) % IMG_SLOTS;
    if (img_has(s) && img_read(s, ssBuf)) {
      slide = s;
      ssDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
      ssDsc.header.w = SCREEN_W;
      ssDsc.header.h = SCREEN_H;
      ssDsc.data_size = IMG_BYTES;
      ssDsc.data = ssBuf;
      lv_img_set_src(ssImg, &ssDsc);
      lv_obj_invalidate(ssImg);
      return true;
    }
  }
  return false;
}

void ui_saver_tick() {
  if (!ss) return;
  const bool busy = ui_night_shown();
  const bool want = prefs.saver != 0 && !busy && lv_disp_get_inactive_time(NULL) >= (uint32_t)prefs.saverMin * 60000u;
  if (!want) {
    if (shown) { lv_obj_add_flag(ss, LV_OBJ_FLAG_HIDDEN); shown = false; }
    return;
  }
  bool withImage = false;
  if (!shown) {
    shown = true;
    slide = -1;
    lastMinute = -1;
    lv_obj_clear_flag(ss, LV_OBJ_FLAG_HIDDEN);
    nextSlide = millis() + 30000;
    withImage = prefs.saver == 2 && next_image();
    if (!withImage) lv_obj_add_flag(ssImg, LV_OBJ_FLAG_HIDDEN);
  } else if (prefs.saver == 2 && (int32_t)(millis() - nextSlide) >= 0) {
    nextSlide = millis() + 30000;
    withImage = next_image();
  }
  if (withImage) lv_obj_clear_flag(ssImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(ssDim, (lv_opa_t)((100 - prefs.saverBright) * 255 / 100), 0);
  {
    char sb[64];
    const bool ok = cfg.door && !strcmp(door_status(), "verbunden");
    if (!cfg.door) snprintf(sb, sizeof sb, "Türspion aus");
    else if (ok) snprintf(sb, sizeof sb, "Türspion bereit");
    else snprintf(sb, sizeof sb, "Türspion: %s", door_status());
    lv_label_set_text(ssStat, sb);
    lv_label_set_text(ssIcon, IC_BELL);
    lv_obj_set_style_text_color(ssIcon, lv_color_hex(ok ? 0x22c55e : 0x7d8aa8), 0);
  }
  const bool imageMode = !lv_obj_has_flag(ssImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(ssBox, imageMode ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(ssBox, lv_color_black(), 0);
  lv_obj_set_style_text_color(ssClock, lv_color_hex(imageMode ? 0xffffff : 0xeef0f4), 0);
  lv_obj_set_style_text_color(ssDate, lv_color_hex(imageMode ? 0xd0d6e6 : 0xa9b4d0), 0);

  struct tm t;
  time_t now = time(nullptr);
  if (!net_time_ok()) return;
  localtime_r(&now, &t);
  char b[48];
  snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
  lv_label_set_text(ssClock, b);
  static const char *WD[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
  static const char *MO[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
  snprintf(b, sizeof b, "%s, %d. %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
  lv_label_set_text(ssDate, b);
}
