#pragma once

// Alle Aufrufe nur unter esp_lv_adapter_lock() / esp_lv_adapter_unlock().
void ui_init();      // baut die Oberfläche auf
void ui_refresh();   // Startseite aktualisieren
void ui_tick();      // einmal pro Sekunde: Uhr, Statuszeile
