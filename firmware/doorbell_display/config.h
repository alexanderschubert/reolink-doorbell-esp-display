#pragma once

// Eigene Werte (WLAN) stehen optional in secrets.h, die nicht ins Repo kommt: secrets.h.example kopieren und ausfüllen.
// Ohne secrets.h richtet sich das Display im Browser ein.
#include "secrets.h"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"   // Mitteleuropa mit Sommerzeit
#define NTP_SERVER "pool.ntp.org"

#define NIGHT_FROM_DEFAULT "23:00"
#define NIGHT_TO_DEFAULT "06:00"

#ifndef BOUNCE_LINES
#define BOUNCE_LINES 20       // Zeilen im Zwischenpuffer der Bildübertragung (Waveshare: 10); mehr verträgt mehr Verzögerung
#endif

#ifndef DEBUG_LOG
#define DEBUG_LOG 0           // 1: alle zehn Sekunden Speicherstand über Serial (Build mit FQBN_EXTRA=CDCOnBoot=cdc)
#endif

#define SCREEN_W 800
#define SCREEN_H 480
