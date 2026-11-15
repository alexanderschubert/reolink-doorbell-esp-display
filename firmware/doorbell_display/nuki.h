#pragma once

#include <Arduino.h>

// Nuki-Schloss über die Web-API (api.nuki.io, HTTPS, API-Token). Ein eigener Task fragt alle 30 Sekunden ab.
struct NukiInfo {
  bool valid;            // mindestens eine Antwort erhalten
  char name[40];
  int state;             // Nuki-Zustand: 1 zu, 3 entriegelt, 5 Falle offen, 254 blockiert ...
  int door;              // Türsensor: 2 zu, 3 offen, sonst unbekannt oder nicht vorhanden
  int battery;           // Prozent, -1 unbekannt
  bool critical;         // Batterie kritisch
  uint32_t at;           // millis der letzten Antwort
  char err[72];          // Fehlertext, leer wenn alles gut
};
void nuki_begin();
NukiInfo nuki_get();
void nuki_refresh();                 // sofort neu abfragen
const char *nuki_state_text(int state);
