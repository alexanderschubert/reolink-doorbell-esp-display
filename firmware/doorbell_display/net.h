#pragma once

#include <Arduino.h>

// WLAN und Uhrzeit (NTP). net_loop() muss oft aufgerufen werden und blockiert nie lange.
void net_begin();
void net_loop();

bool net_wifi_ok();
bool net_setup();                         // noch nicht eingerichtet: Hotspot für die Einrichtung
bool net_time_ok();                       // die Uhr ist per NTP gestellt
