#pragma once

#include <Arduino.h>
#include <Stream.h>

// Kleine Hilfen für HTTP-Antworten (Türspion und Nuki)

// Antwort in einen festen Puffer sammeln (auch bei "chunked"-Übertragung)
struct BufStream : public Stream {
  uint8_t *d; size_t cap, n; bool over;
  BufStream(uint8_t *b, size_t c) : d(b), cap(c), n(0), over(false) {}
  size_t write(uint8_t c) override { if (n < cap) { d[n++] = c; return 1; } over = true; return 0; }
  size_t write(const uint8_t *b, size_t l) override { if (n + l > cap) { over = true; return 0; } memcpy(d + n, b, l); n += l; return l; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Wert zu "key" : "wert" bzw. "key" : zahl aus einer JSON-Antwort (ohne JSON-Bibliothek). from: ab dort suchen.
static inline String json_val(const char *b, const char *key, const char *from = nullptr) {
  String k = String("\"") + key + "\"";
  const char *p = strstr(from ? from : b, k.c_str());
  if (!p) return "";
  p = strchr(p + k.length(), ':');
  if (!p) return "";
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n') p++;
  String v;
  if (*p == '"') { p++; while (*p && *p != '"' && v.length() < 60) v += *p++; }
  else while (*p && *p != ',' && *p != '}' && *p != '\n' && *p != ' ' && v.length() < 20) v += *p++;
  return v;
}
