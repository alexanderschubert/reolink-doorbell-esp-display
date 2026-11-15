#pragma once

#include <Arduino.h>

// WLAN und Türklingel. Die Werte kommen aus dem Speicher des Displays (eingetragen im Browser); ist dort nichts, gelten
// WLAN-Werte aus secrets.h, falls ausgefüllt. So geht eine fertige Firmware ohne eingebaute Zugangsdaten.
struct Cfg {
  String ssid, pass;
  bool door;      // Live-Bild bei Bewegung an
  int dsecs;      // Anzeigedauer in Sekunden (ab der letzten erkannten Bewegung)
  String dhost, duser, dpass;   // Adresse (IP, optional :Port) und Zugang der Klingel
  bool nuki;      // Nuki-Schloss auf der Startseite zeigen
  String ntoken;  // API-Token der Nuki-Web-API
  String durl;    // optional: eigene Adresse für das JPEG-Bild (z. B. go2rtc, Frigate), statt Snap der Klingel
};
extern Cfg cfg;

void cfg_load();
bool cfg_ok();                  // WLAN ist eingetragen
void cfg_save();
void cfg_save_door();           // nur die Türklingel-Werte (ändert das WLAN nicht)
void cfg_reset();               // beim nächsten Start Einrichtungsmodus
