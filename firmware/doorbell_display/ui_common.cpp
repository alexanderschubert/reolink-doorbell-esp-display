#include "ui_common.h"

Theme th = {false, 0x0b1226, 0xeef0f4, 0xa9b4d0, 0x7d8aa8, 0x6cb8ff, 0x22c55e, 0xf59e0b, 0xef4444, 0xc9a45a,
            0xffffff, 0xffffff, 0xffffff, 0x05070f, 26, 51, 51, 26, 204};

void ui_theme_apply(bool light) {
  if (light) th = {true, 0xe9eff9, 0x1a2238, 0x4b5878, 0x6b7794, 0x1d6fd6, 0x16a34a, 0xd97706, 0xdc2626, 0x8a6d2a,
                   0x141e3c, 0xffffff, 0x141e3c, 0xffffff, 184, 36, 33, 20, 217};
  else th = {false, 0x0b1226, 0xeef0f4, 0xa9b4d0, 0x7d8aa8, 0x6cb8ff, 0x22c55e, 0xf59e0b, 0xef4444, 0xc9a45a,
             0xffffff, 0xffffff, 0xffffff, 0x05070f, 26, 51, 51, 26, 204};
}

lv_obj_t *ui_plain(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *ui_glass(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
  lv_obj_t *o = ui_plain(parent, x, y, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(th.glassBg), 0);
  lv_obj_set_style_bg_opa(o, th.glassOpa, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(th.glassBorder), 0);
  lv_obj_set_style_border_opa(o, th.borderOpa, 0);
  return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t *ui_label_w(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x, int y, int w, lv_text_align_t align) {
  lv_obj_t *l = ui_label(parent, font, color, text, x, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, align, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);     // zu lange Texte enden mit …, statt umzubrechen
  return l;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *glyph, int x, int y) {
  return ui_label(parent, font, color, glyph, x, y);
}

lv_obj_t *ui_icon_w(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *glyph, int x, int y, int w) {
  lv_obj_t *l = ui_label(parent, font, color, glyph, x, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

lv_obj_t *ui_icon_text(lv_obj_t *parent, const char *glyph, uint32_t iconColor, const char *text, const lv_font_t *tfont, uint32_t tcolor, int x, int y) {
  const int iw = tfont == f20 || tfont == f28 ? 28 : 22;
  ui_icon(parent, tfont == f20 ? fi16 : tfont == f28 ? fi20 : fi14, iconColor, glyph, x, y + (tfont == f14 ? 1 : tfont == f16 ? 2 : 3));
  return ui_label(parent, tfont, tcolor, text, x + iw, y);
}

lv_obj_t *ui_dot(lv_obj_t *parent, int x, int y, int d, uint32_t color) {
  lv_obj_t *o = ui_plain(parent, x, y, d, d);
  lv_obj_set_style_radius(o, d / 2, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

lv_obj_t *ui_arc(lv_obj_t *parent, int x, int y, int size, int width, uint32_t color) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_obj_set_pos(a, x, y);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(th.tint), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, th.trackOpa, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
  return a;
}

lv_obj_t *ui_bar(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *b = lv_bar_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_bar_set_range(b, 0, 100);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, lv_color_hex(th.tint), 0);
  lv_obj_set_style_bg_opa(b, th.trackOpa, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_ACC), LV_PART_INDICATOR);
  return b;
}

void ui_bar_set(lv_obj_t *bar, int pct, uint32_t color) {
  lv_bar_set_value(bar, pct < 0 ? 0 : pct > 100 ? 100 : pct, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
}

lv_obj_t *ui_button_i(lv_obj_t *parent, int x, int y, int w, int h, const char *glyph, const char *text, uint32_t color, lv_event_cb_t cb, void *user) {
  lv_obj_t *b = ui_button(parent, x, y, w, h, text, color, cb, user);
  lv_obj_t *t = lv_obj_get_child(b, 0);
  lv_obj_set_width(t, w - 40);
  lv_obj_set_x(t, 36);
  lv_obj_t *i = ui_icon(b, fi16, C_TEXT, glyph, 16, (h - 20) / 2);
  lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
  return b;
}

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, uint32_t color, lv_event_cb_t cb, void *user) {
  lv_obj_t *b = ui_plain(parent, x, y, w, h);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(b, LV_OPA_70, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  lv_obj_t *l = ui_label_w(b, f16, C_TEXT, text, 0, (h - 22) / 2 - 1, w - 4, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(l, 0, (h - 22) / 2 - 1);
  return b;
}

// Seitenkopf: Titel links; die Uhr rechts oben zeichnet ui_tick() (Label mit Namen "clk")
static lv_obj_t *clockLabels[8];
static int nClockLabels = 0;

void ui_page_title(lv_obj_t *page, const char *title, lv_obj_t **sub) {
  ui_label(page, f36b, C_TEXT, title, 32, 14);
  lv_obj_t *s = ui_label(page, f16, C_MU, " ", 34, 62);
  if (sub) *sub = s;
  if (nClockLabels < 8) {
    clockLabels[nClockLabels] = ui_label_w(page, f28, C_MU, "--:--", SCREEN_W - 32 - 140, 18, 140, LV_TEXT_ALIGN_RIGHT);
    nClockLabels++;
  }
}

void ui_clock_reset() { nClockLabels = 0; }

void ui_set_clock_text(const char *t) {
  for (int i = 0; i < nClockLabels; i++) lv_label_set_text(clockLabels[i], t);
}

