#!/bin/bash
# Stellt die lv_conf.h von Waveshares LVGL 8.4 für diese Firmware ein (einmal nach dem Einrichten der Bibliotheken):
#   - TinyTTF an (die eingebettete Schrift mit Umlauten)
#   - LVGLs Speicher aus dem PSRAM statt dem knappen internen RAM (sonst bleiben mit allen Seiten nur ~25 KB frei)
#   tools/patch-lv-conf.sh <Pfad zu lv_conf.h>
set -euo pipefail
F="${1:?Pfad zur lv_conf.h angeben (im Arduino-Ordner libraries/)}"
python3 - "$F" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
s = s.replace('#define LV_USE_TINY_TTF 0', '#define LV_USE_TINY_TTF 1')
s = s.replace('#define LV_MEM_CUSTOM 0', '#define LV_MEM_CUSTOM 1')
s = re.sub(r'#define LV_MEM_CUSTOM_INCLUDE .*', '#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>', s)
s = re.sub(r'#define LV_MEM_CUSTOM_ALLOC .*', '#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)', s)
s = re.sub(r'#define LV_MEM_CUSTOM_FREE .*', '#define LV_MEM_CUSTOM_FREE heap_caps_free', s)
s = re.sub(r'#define LV_MEM_CUSTOM_REALLOC .*', '#define LV_MEM_CUSTOM_REALLOC(p, size) heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM)', s)
open(p, 'w', encoding='utf-8').write(s)
PY
grep -n "LV_USE_TINY_TTF 1\|LV_MEM_CUSTOM 1\|LV_MEM_CUSTOM_ALLOC\|LV_MEM_CUSTOM_INCLUDE" "$F"
