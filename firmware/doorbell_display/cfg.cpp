#include "cfg.h"

#include <Preferences.h>

#include "config.h"

Cfg cfg;

void cfg_load() {
  Preferences p;
  p.begin("doorbell", true);
  const bool again = p.getBool("setup", false);          // "Neu einrichten" wurde gewählt
  if (p.isKey("ssid") || again) {
    cfg.ssid = again ? "" : p.getString("ssid", "");
    cfg.pass = p.getString("wpass", "");
  } else {
#if defined(WIFI_SSID)
    if (strcmp(WIFI_SSID, "Name-des-WLAN") != 0) {         // secrets.h ist ausgefüllt
      cfg.ssid = WIFI_SSID;
      cfg.pass = WIFI_PASS;
    }
#endif
  }
  cfg.door = p.getBool("dOn", false);
  cfg.dsecs = constrain(p.getInt("dSecs", 20), 3, 300);
  cfg.dhost = p.getString("dHost", "");
  cfg.duser = p.getString("dUser", "");
  cfg.dpass = p.getString("dPass", "");
  cfg.durl = p.getString("dUrl", "");
  cfg.nuki = p.getBool("kOn", false);
  cfg.nctl = p.getBool("kCtl", false);
  cfg.ntoken = p.getString("kTok", "");
  p.end();
}

bool cfg_ok() { return cfg.ssid.length() > 0; }

void cfg_save() {
  Preferences p;
  p.begin("doorbell", false);
  p.putString("ssid", cfg.ssid);
  p.putString("wpass", cfg.pass);
  p.putBool("setup", false);
  p.end();
  cfg_save_door();
}

void cfg_save_door() {
  Preferences p;
  p.begin("doorbell", false);
  p.putBool("dOn", cfg.door);
  p.putInt("dSecs", cfg.dsecs);
  p.putString("dHost", cfg.dhost);
  p.putString("dUser", cfg.duser);
  p.putString("dPass", cfg.dpass);
  p.putString("dUrl", cfg.durl);
  p.putBool("kOn", cfg.nuki);
  p.putBool("kCtl", cfg.nctl);
  p.putString("kTok", cfg.ntoken);
  p.end();
}

void cfg_reset() {
  Preferences p;
  p.begin("doorbell", false);
  p.putBool("setup", true);
  p.end();
}
