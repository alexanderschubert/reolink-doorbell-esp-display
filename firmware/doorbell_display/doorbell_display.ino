#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

#include <esp_lcd_panel_rgb.h>

#include "esp_lv_adapter_arduino.h"

#include "config.h"
#include "door.h"
#include "nuki.h"
#include "net.h"
#include "ui.h"
#include "ui_common.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static LCD *gLcd = nullptr;
static Board *gBoard = nullptr;

void display_backlight(bool on) {
  if (!gBoard || !gBoard->getBacklight()) return;
  if (on) gBoard->getBacklight()->on(); else gBoard->getBacklight()->off();
}

// Aufbau wie in Waveshares LVGL-Beispiel (Board, Display, Touch); die Oberfläche steht in ui.cpp.
void setup() {
  Serial.begin(115200);
  delay(1500);                                  // dem Mac Zeit geben, den USB-Anschluss zu öffnen
  Serial.println("Reolink Tuerspion Display");
  Serial.printf("Heap frei %u, PSRAM frei %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  Board *board = new Board();
  gBoard = board;
  if (board == nullptr || !board->init()) {
    Serial.println("Board init failed");
    while (true) delay(1000);
  }

  const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
  const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
  const uint8_t frame_buffer_count = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);

  LCD *lcd = board->getLCD();
  if (lcd == nullptr) {
    Serial.println("LCD device is not available");
    while (true) delay(1000);
  }
  gLcd = lcd;
  auto *lcd_bus = lcd->getBus();
  if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
    lcd->configFrameBufferNumber(frame_buffer_count);
    static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * BOUNCE_LINES);
  }
  assert(board->begin());

  esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
  adapter_config.task_stack_size = 32 * 1024;   // TinyTTF braucht beim Rendern großer Schriften viel Stapel
  adapter_config.task_priority = 2;
  adapter_config.task_core_id = 0;   // Zeichentask auf dem anderen Kern als loop(), sonst hungert er die Hauptschleife aus
  ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

  esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(lcd, lcd->getFrameWidth(), lcd->getFrameHeight(), rotation);
  disp_config.profile.use_psram = true;
  lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
  assert(disp != nullptr);

  if (board->getTouch() != nullptr) {
    esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, board->getTouch());
    lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
    assert(touch != nullptr);
  }
  ESP_ERROR_CHECK(esp_lv_adapter_start());

  ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
  ui_init();
  esp_lv_adapter_unlock();
  Serial.printf("Oberflaeche steht, Heap frei %u, PSRAM frei %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  img_begin();                                  // Bilderspeicher einhängen (beim ersten Start wird er formatiert)
  net_begin();
  door_begin();
  nuki_begin();
  if (net_setup()) {                            // noch nicht eingerichtet: Anleitung zeigen
    esp_lv_adapter_lock(-1);
    ui_setup_show();
    esp_lv_adapter_unlock();
  }
}

void loop() {
  static uint32_t lastTick = 0, lastBeat = 0;
  if (DEBUG_LOG && millis() - lastBeat > 10000) { lastBeat = millis(); Serial.printf("loop %u s, heap %u, psram %u\n", millis() / 1000, ESP.getFreeHeap(), ESP.getFreePsram()); }
  net_loop();
  web_loop();
  ui_door_poll();

  // Das Starten des WLANs stört die Bildübertragung einmal kurz, danach sitzt das Bild versetzt. Ein Neustart der
  // Übertragung, sobald das WLAN steht, richtet es wieder aus.
  static uint32_t restartAt = 0;
  static uint8_t restarts = 0;
  if (!restartAt && net_wifi_ok()) restartAt = millis() + 3000;
  if (restartAt && restarts < 2 && millis() > restartAt && gLcd) {
    esp_lcd_rgb_panel_restart(gLcd->getHandle());
    restarts++;
    restartAt = millis() + 8000;
  }

  if (imgNeedRestart && gLcd) {            // nach dem Schreiben von Bildern sitzt das Bild sonst versetzt
    delay(300);
    imgNeedRestart = false;
    esp_lcd_rgb_panel_restart(gLcd->getHandle());
  }

  if (prefsChanged && esp_lv_adapter_lock(200) == ESP_OK) {
    prefsChanged = false;
    ui_prefs_applied();
    esp_lv_adapter_unlock();
  }

  const bool tick = millis() - lastTick >= 1000;
  if (tick) {
    // Erst nach bekommener Sperre als erledigt merken, sonst geht eine Aktualisierung verloren, wenn das Zeichnen länger dauert.
    if (esp_lv_adapter_lock(200) == ESP_OK) {
      lastTick = millis();
      ui_tick();
      esp_lv_adapter_unlock();
    }
  }
  delay(20);
}
