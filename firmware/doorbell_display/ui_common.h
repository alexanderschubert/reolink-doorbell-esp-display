#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "icons.h"

// Farben wie auf der Kiosk-Seite; dunkel oder hell, umschaltbar (ui_theme_apply)
struct Theme {
  bool light;
  uint32_t bg, text, mu, dm, acc, ok, warm, hot, gold;
  uint32_t tint;            // Farbe für Linien, Bahnen und Chips: weiß auf dunkel, dunkles Blau auf hell
  uint32_t glassBg, glassBorder, nav;
  uint8_t glassOpa, borderOpa, trackOpa, chipOpa, navOpa;
};
extern Theme th;
void ui_theme_apply(bool light);
#define C_BG th.bg
#define C_TEXT th.text
#define C_MU th.mu
#define C_DM th.dm
#define C_ACC th.acc
#define C_OK th.ok
#define C_WARM th.warm
#define C_HOT th.hot
#define C_GOLD th.gold
// Feste helle Farben für Flächen, die in beiden Themen dunkel bleiben (Gehäusefront, Befehlskarten)
#define K_TEXT 0xeef0f4
#define K_MU 0xa9b4d0
#define K_DM 0x7d8aa8

#define PAGE_H (SCREEN_H - 56)          // Höhe einer Seite, darunter liegt die Navigationsleiste

extern const lv_font_t *f12, *f14, *f16, *f20, *f28, *f36b, *fClock;
extern const lv_font_t *fi14, *fi16, *fi20, *fi28;     // Symbolschrift (icons.h)

// Bausteine
lv_obj_t *ui_plain(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_glass(lv_obj_t *parent, int x, int y, int w, int h, int radius = 16);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x, int y);
lv_obj_t *ui_label_w(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x, int y, int w, lv_text_align_t align);
lv_obj_t *ui_icon(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *glyph, int x, int y);
lv_obj_t *ui_icon_w(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *glyph, int x, int y, int w);   // mittig in w
lv_obj_t *ui_icon_text(lv_obj_t *parent, const char *glyph, uint32_t iconColor, const char *text, const lv_font_t *tfont, uint32_t tcolor, int x, int y);   // Symbol, danach der Text
lv_obj_t *ui_button_i(lv_obj_t *parent, int x, int y, int w, int h, const char *glyph, const char *text, uint32_t color, lv_event_cb_t cb, void *user);
lv_obj_t *ui_dot(lv_obj_t *parent, int x, int y, int d, uint32_t color);
lv_obj_t *ui_arc(lv_obj_t *parent, int x, int y, int size, int width, uint32_t color);
lv_obj_t *ui_bar(lv_obj_t *parent, int x, int y, int w, int h);
void ui_bar_set(lv_obj_t *bar, int pct, uint32_t color);
lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, uint32_t color, lv_event_cb_t cb, void *user);
void ui_page_title(lv_obj_t *page, const char *title, lv_obj_t **sub);
void ui_clock_reset();                                // vor dem Neuaufbau der Oberfläche
void ui_set_clock_text(const char *t);                // setzt die Uhr in allen Seitenköpfen

void page_settings_build(lv_obj_t *p);   void page_settings_refresh();
void page_tick();                         // jede Sekunde (Anzeige in den Einstellungen)

// Nachtmodus und Einstellungen (ui_settings.cpp)
void ui_night_init(lv_obj_t *screen);
void ui_night_tick();
void ui_night_restore();                    // Beleuchtung so setzen, wie es der Nachtmodus verlangt (nach dem Türbild)
void ui_door_init();                        // Türbild (ui_door.cpp)
void ui_door_poll();                        // aus loop(), ohne LVGL-Sperre
bool ui_night_shown();
void ui_saver_init(lv_obj_t *screen);       // Bildschirmschoner (ui_saver.cpp)
void ui_saver_tick();
void ui_bright_apply();
void ui_setup_show();                       // Einrichtungsanzeige (Hotspot-Daten), solange nicht eingerichtet                     // Helligkeit setzen (Schicht über allem)
void ui_theme_changed();                    // Theme oder Auto-Wahl hat sich geändert: Oberfläche neu aufbauen
void ui_bg_changed();                       // Hintergrundbild oder Abdunklung geändert
bool ui_theme_light_wanted();

// Bilder im Flash (images.cpp): 800x480, RGB565, bis zu acht Plätze; Hochladen über die Webseite des Displays
#define IMG_SLOTS 8
#define IMG_BYTES (SCREEN_W * SCREEN_H * 2)
void img_begin();
bool img_has(int slot);
bool img_read(int slot, uint8_t *buf);      // liest IMG_BYTES nach buf
int img_count();
extern volatile bool imgNeedRestart;
extern volatile bool prefsChanged;          // die Webseite hat Einstellungen geändert
void ui_prefs_applied();                    // unter der LVGL-Sperre aufrufen
extern volatile uint32_t imgRevision;       // zählt hoch, wenn Bilder hochgeladen oder gelöscht wurden
String web_pin();
void web_pin_new();
void web_loop();                            // Webseite zum Hochladen (nur mit WLAN)
uint8_t *img_alloc();                       // IMG_BYTES im PSRAM
struct Prefs {
  bool nightOn; int nightFrom; int nightTo; bool nightOff;   // Minuten seit Mitternacht
  int theme;      // 0 dunkel, 1 hell, 2 automatisch (hell von 7 bis 19 Uhr)
  int bg;         // Platz des Hintergrundbilds (0..7) oder -1 ohne
  int dim;        // Abdunklung des Hintergrundbilds in Prozent
  int saver;      // Bildschirmschoner: 0 aus, 1 Uhr, 2 Bilder
  int saverMin;   // nach so vielen Minuten ohne Berührung
  int saverBright; // Helligkeit des Bildschirmschoners in Prozent
  int bright;     // Helligkeit in Prozent (Abdunklung per Software, die Beleuchtung selbst lässt sich nicht regeln)
};
extern Prefs prefs;
void prefs_load();
void prefs_save();

// Anzeige (in doorbell_display.ino)
void display_backlight(bool on);
