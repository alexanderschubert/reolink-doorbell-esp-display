#include "esp_lv_adapter_arduino.h"
#include "door.h"
#include "net.h"
#include "ui_common.h"

// Türbild: eigene Vollbild-Ebene auf lv_layer_sys, unter der Helligkeits-Schicht. Sie liegt über Bildschirmschoner und
// Nachtmodus. Beim Schließen bleibt darunter alles, wie es war (Seite, Schoner, Nachtanzeige); nur die Beleuchtung wird
// wieder so gesetzt, wie es der Nachtmodus verlangt.
static lv_obj_t *ov, *img, *chip, *msg;
static lv_img_dsc_t dsc;
static bool shown = false;
static uint32_t lastSeq = 0, lastSec = 0;

static void hide() {
  if (!shown) return;
  shown = false;
  lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
  ui_night_restore();
}

static void press_cb(lv_event_t *) {
  door_dismiss();
  hide();
}

// Vor ui_bright_apply() aufrufen, damit die Helligkeits-Schicht darüber liegt.
void ui_door_init() {
  ov = ui_plain(lv_layer_sys(), 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
  lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ov, press_cb, LV_EVENT_PRESSED, nullptr);
  img = lv_img_create(ov);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  msg = ui_label_w(ov, f20, 0xa9b4d0, "Bild wird geladen …", 0, 220, SCREEN_W, LV_TEXT_ALIGN_CENTER);
  chip = ui_label(ov, f16, 0xffffff, " ", 16, 14);
  lv_obj_set_style_bg_color(chip, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_60, 0);
  lv_obj_set_style_radius(chip, 14, 0);
  lv_obj_set_style_pad_hor(chip, 12, 0);
  lv_obj_set_style_pad_ver(chip, 6, 0);
  lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
}

// Aus loop() (ohne Sperre): nimmt die Sperre nur, wenn sich etwas ändert.
void ui_door_poll() {
  if (!ov) return;
  const bool want = door_wanted();
  const bool news = shown && door_seq() != lastSeq;
  const bool sec = shown && millis() - lastSec >= 1000;
  if (want == shown && !news && !sec) return;
  if (esp_lv_adapter_lock(100) != ESP_OK) return;
  if (want && !shown) {
    shown = true;
    lastSeq = 0;
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(msg, "Bild wird geladen …");
    lv_obj_clear_flag(msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_HIDDEN);
    display_backlight(true);
  } else if (!want && shown) {
    hide();
  }
  if (shown) {
    const uint16_t *px;
    int w, h;
    const uint32_t s = door_seq();
    if (s != lastSeq && door_frame(&px, &w, &h)) {
      lastSeq = s;
      dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
      dsc.header.w = w;
      dsc.header.h = h;
      dsc.data_size = (uint32_t)w * h * 2;
      dsc.data = (const uint8_t *)px;
      lv_img_set_src(img, &dsc);
      lv_obj_set_pos(img, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2);
      lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(msg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(img);
    }
    if (sec || s == 0) {
      lastSec = millis();
      char b[64];
      snprintf(b, sizeof b, "Türklingel · noch %d s · Berühren zum Schließen", door_remaining());
      lv_label_set_text(chip, b);
    }
  }
  esp_lv_adapter_unlock();
}
