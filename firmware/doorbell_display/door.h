#pragma once

#include <Arduino.h>
#include <time.h>

// Türklingel (Reolink): erkennt Bewegung und holt dann JPEG-Einzelbilder in schneller Folge. Das Netzwerk und das
// Dekodieren laufen in einem eigenen Task; die Oberfläche (ui_door.cpp) zeigt nur das fertige Bild.
void door_begin();
bool door_wanted();                         // das Bild soll gerade angezeigt werden
int door_remaining();                       // Sekunden bis zum Ende der Anzeige
uint32_t door_seq();                        // zählt mit jedem neuen Bild hoch (0: noch keines)
// Neuestes Bild (RGB565, w x h, höchstens 800 x 480); der Zeiger bleibt bis zum übernächsten Bild gültig.
bool door_frame(const uint16_t **px, int *w, int *h);
void door_dismiss();                        // Berührung: Anzeige beenden, bis die Bewegung einmal aufgehört hat
void door_test();                           // Anzeige zum Ausprobieren starten
time_t door_last_motion();                 // Zeitpunkt der letzten erkannten Bewegung (0: noch keine seit dem Start)
const char *door_status();                  // letzter Zustand als Text (für die Einstellungen)
