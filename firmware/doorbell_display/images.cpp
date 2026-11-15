#include <FFat.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "cfg.h"
#include "door.h"
#include "nuki.h"
#include "net.h"
#include "ui_common.h"

// Hintergrundbilder und Bildschirmschoner-Bilder: acht Plätze im Flash (FAT), je 800x480 in RGB565 (768 000 Byte).
// Hochgeladen wird über die Webseite des Displays (http://<Adresse des Displays>/); der Browser rechnet das Bild um.
volatile uint32_t imgRevision = 0;
volatile bool prefsChanged = false;
volatile bool imgNeedRestart = false;     // nach dem Schreiben in den Flash die Bildübertragung neu starten
static bool mounted = false;
static WebServer server(80);
static bool serverUp = false;
static uint8_t *upBuf = nullptr;
static size_t upLen = 0;
static bool upOk = false;

// PIN der Webseite: beim ersten Start zufällig erzeugt, steht am Display unter Einstellungen > Dieses Display.
static String pin;
String web_pin() {
  if (!pin.length()) {
    Preferences pr;
    pr.begin("doorbell", false);
    pin = pr.getString("webPin", "");
    if (pin.length() != 6) { char b[8]; snprintf(b, sizeof b, "%06u", (unsigned)(esp_random() % 1000000)); pin = b; pr.putString("webPin", pin); }
    pr.end();
  }
  return pin;
}
void web_pin_new() {
  Preferences pr;
  pr.begin("doorbell", false);
  char b[8];
  snprintf(b, sizeof b, "%06u", (unsigned)(esp_random() % 1000000));
  pin = b;
  pr.putString("webPin", pin);
  pr.end();
}
static bool authed() {
  if (net_setup()) return true;           // Einrichtung: das WLAN des Displays ist selbst durch ein Passwort geschützt
  if (server.authenticate("display", web_pin().c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Türklingel-Display", "Benutzer: display, Passwort: die PIN aus den Einstellungen des Displays");
  return false;
}

static String path_of(int slot) { return String("/bg") + slot + ".bin"; }

void img_begin() {
  mounted = FFat.begin(true);
  imgRevision++;
}

uint8_t *img_alloc() { return (uint8_t *)heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM); }

bool img_has(int slot) {
  if (!mounted || slot < 0 || slot >= IMG_SLOTS) return false;
  File f = FFat.open(path_of(slot), "r");
  if (!f) return false;
  const bool ok = f.size() == IMG_BYTES;
  f.close();
  return ok;
}

int img_count() {
  int n = 0;
  for (int i = 0; i < IMG_SLOTS; i++) if (img_has(i)) n++;
  return n;
}

bool img_read(int slot, uint8_t *buf) {
  if (!mounted || !buf) return false;
  File f = FFat.open(path_of(slot), "r");
  if (!f || f.size() != IMG_BYTES) return false;
  const size_t n = f.read(buf, IMG_BYTES);
  f.close();
  return n == IMG_BYTES;
}

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Türklingel-Display · Bilder</title>
<style>
body{font:16px system-ui,sans-serif;background:#0b1226;color:#eef0f4;margin:0;padding:20px;max-width:900px;margin:auto}
h1{font-size:24px;margin:0 0 4px}p{color:#a9b4d0;margin:6px 0 16px}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:14px}
.c{background:#ffffff1a;border:1px solid #ffffff33;border-radius:14px;padding:10px}
canvas{width:100%;aspect-ratio:5/3;background:#0006;border-radius:8px;display:block}
.b{display:flex;justify-content:space-between;align-items:center;margin-top:8px;color:#a9b4d0;font-size:14px}
button,label.up{background:#6cb8ff33;border:1px solid #6cb8ffb3;color:#eef0f4;border-radius:20px;padding:8px 16px;font:inherit;cursor:pointer}
button.d{background:#ef444433;border-color:#ef4444b3;padding:4px 12px;font-size:14px}
h2{font-size:18px;margin:22px 0 8px}
.r{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid #ffffff22}
.r:last-child{border:0}.r span{color:#a9b4d0}select,input[type=time]{background:#0b1226;color:#eef0f4;border:1px solid #ffffff44;border-radius:8px;padding:6px 8px;font:inherit}
input[type=range]{width:180px}
input[type=password]{background:#0b1226;color:#eef0f4;border:1px solid #ffffff44;border-radius:8px;padding:6px 8px;font:inherit;width:150px}
#st{margin:12px 0;color:#a9b4d0;min-height:22px}
</style>
<h1>Türklingel-Display</h1><p><a href="/setup" style="color:#6cb8ff">Verbindung ändern (WLAN, Türklingel, Nuki)</a></p>
<p>Bis zu acht Bilder. Sie werden auf 800 × 480 zugeschnitten und im Display gespeichert. Welches Bild der Hintergrund ist und ob der Bildschirmschoner sie zeigt, stellst du am Display unter Einstellungen ein.</p>
<h2>Einstellungen</h2><div id="s" class="c"></div>
<h2>Bilder</h2><label class="up">Bilder auswählen<input id="f" type="file" accept="image/*" multiple hidden></label>
<div id="st"></div><div class="g" id="g"></div>
<script>
const W=800,H=480,N=8,$=s=>document.querySelector(s);
const st=t=>$('#st').textContent=t;
const MINS=[1,2,5,10,15,30,60],DS=[5,10,15,20,30,45,60,90,120,180,300],hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0'),mm=t=>{const[a,b]=t.split(':');return a*60+ +b};
async function save(o){await fetch('/api/prefs',{method:'POST',body:new URLSearchParams(o)})}
async function prefs(){const p=await (await fetch('/api/prefs')).json(),s=$('#s');
 const sel=(k,opts,v)=>'<select data-k="'+k+'">'+opts.map(([a,b])=>'<option value="'+a+'"'+(a==v?' selected':'')+'>'+b+'</option>').join('')+'</select>';
 const rng=(k,mn,mx,v)=>'<input type="range" data-k="'+k+'" min="'+mn+'" max="'+mx+'" step="10" value="'+v+'">';
 const chk=(k,v)=>'<input type="checkbox" data-k="'+k+'"'+(v?' checked':'')+'>';
 const tm=(k,v)=>'<input type="time" data-k="'+k+'" value="'+hm(v)+'">';
 const bg=[[-1,'Aus']];p.slots.forEach((h,i)=>h&&bg.push([i,'Bild '+(i+1)]));
 const row=(a,b)=>'<div class="r"><span>'+a+'</span>'+b+'</div>';
 s.innerHTML=row('Darstellung',sel('theme',[[0,'Dunkel'],[1,'Hell'],[2,'Automatisch']],p.theme))+row('Helligkeit',rng('bright',10,100,p.bright))+
  row('Hintergrundbild',sel('bg',bg,p.bg))+row('Abdunklung des Bildes',rng('dim',0,90,p.dim))+
  row('Bildschirmschoner',sel('saver',[[0,'Aus'],[1,'Uhr'],[2,'Bilder']],p.saver))+row('Starten nach',sel('saverMin',MINS.map(m=>[m,m+' min']),p.saverMin))+row('Helligkeit im Schoner',rng('saverBright',10,100,p.saverBright))+
  row('Nachtmodus',chk('nightOn',p.nightOn))+row('Nachts von',tm('nightFrom',p.nightFrom))+row('bis',tm('nightTo',p.nightTo))+row('Display nachts ausschalten',chk('nightOff',p.nightOff))+
  row('Türklingel: Live-Bild bei Bewegung',chk('doorOn',p.doorOn))+row('Anzeigedauer',sel('doorSecs',DS.map(m=>[m,m+' s']),p.doorSecs))+row('Türbild ausprobieren','<button id="dt">Jetzt zeigen</button>')+
  row('Nuki-Schloss auf der Startseite',chk('nukiOn',p.nukiOn))+row('Schloss am Display bedienen (auf-/zuschließen)',chk('nukiCtl',p.nukiCtl))+
  row('Nuki API-Token'+(p.nukiTok?' (gespeichert)':''),'<input type="password" id="nt" placeholder="neuer Token" autocomplete="new-password"><button id="ntb">Speichern</button>');
 $('#ntb').onclick=async()=>{const v=$('#nt').value.trim();if(v){await save({ntoken:v});prefs()}};
 $('#dt').onclick=()=>save({doorTest:1});
 s.querySelectorAll('[data-k]').forEach(e=>e.onchange=()=>{const k=e.dataset.k;save({[k]:e.type=='checkbox'?(e.checked?1:0):e.type=='time'?mm(e.value):e.value})})}
async function list(){prefs();const r=await (await fetch('/api/list')).json();const g=$('#g');g.innerHTML='';
 r.slots.forEach((has,i)=>{const c=document.createElement('div');c.className='c';
  c.innerHTML='<canvas width="400" height="240"></canvas><div class="b"><span>Bild '+(i+1)+'</span>'+(has?'<button class="d">Löschen</button>':'<span>frei</span>')+'</div>';
  g.appendChild(c);
  if(has){draw(i,c.querySelector('canvas'));c.querySelector('button').onclick=async()=>{await fetch('/delete?slot='+i,{method:'POST'});list()}}
 });st('Frei im Speicher: '+Math.round(r.free/1048576*10)/10+' MB');return r.slots}
async function draw(i,cv){const b=new Uint8Array(await (await fetch('/img?slot='+i)).arrayBuffer());
 const x=cv.getContext('2d'),d=x.createImageData(W,H);
 for(let p=0,q=0;p<W*H;p++,q+=4){const v=b[p*2]|(b[p*2+1]<<8);d.data[q]=(v>>11)*255/31;d.data[q+1]=((v>>5)&63)*255/63;d.data[q+2]=(v&31)*255/31;d.data[q+3]=255}
 const t=document.createElement('canvas');t.width=W;t.height=H;t.getContext('2d').putImageData(d,0,0);x.drawImage(t,0,0,400,240)}
async function conv(file){const bm=await createImageBitmap(file);const cv=document.createElement('canvas');cv.width=W;cv.height=H;const x=cv.getContext('2d');
 const s=Math.max(W/bm.width,H/bm.height),w=bm.width*s,h=bm.height*s;x.drawImage(bm,(W-w)/2,(H-h)/2,w,h);
 const d=x.getImageData(0,0,W,H).data,o=new Uint8Array(W*H*2);
 for(let p=0,q=0;p<W*H;p++,q+=4){const v=((d[q]&0xF8)<<8)|((d[q+1]&0xFC)<<3)|(d[q+2]>>3);o[p*2]=v&255;o[p*2+1]=v>>8}
 return o}
$('#f').onchange=async e=>{let slots=await list();
 for(const f of e.target.files){const i=slots.indexOf(false);if(i<0){st('Alle acht Plätze sind belegt. Lösche erst ein Bild.');break}
  st('Lade '+f.name+' hoch …');const fd=new FormData();fd.append('img',new Blob([await conv(f)]),'img.bin');
  const r=await fetch('/upload?slot='+i,{method:'POST',body:fd});if(!r.ok){st('Fehler beim Speichern von '+f.name);break}
  slots[i]=true}
 e.target.value='';list()};
list();
</script></html>)HTML";

static String esc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

// Einrichtungsseite: WLAN und Türklingel. Leere Kennwortfelder behalten das gespeicherte Kennwort.
static void handle_setup() {
  if (!authed()) return;
  String h = F("<!doctype html><html lang=de><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Türklingel-Display · Verbindung</title>"
               "<style>body{font:16px system-ui,sans-serif;background:#0b1226;color:#eef0f4;max-width:520px;margin:auto;padding:20px}label{display:block;margin:14px 0 4px;color:#a9b4d0}"
               "input[type=text],input[type=password],input[type=number]{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #ffffff44;background:#ffffff14;color:#eef0f4;font:inherit}"
               "button{margin-top:22px;padding:12px 22px;border-radius:22px;border:1px solid #6cb8ffb3;background:#6cb8ff33;color:#eef0f4;font:inherit;cursor:pointer}p{color:#a9b4d0}</style>"
               "<h1>Verbindung</h1><p>Trage das WLAN und die Zugangsdaten der Klingel ein. Nach dem Speichern startet das Display neu.</p>"
               "<form method=post action=/save>");
  h += "<label>WLAN (Name)</label><input type=text name=ssid value=\"" + esc(cfg.ssid) + "\" required>";
  h += "<label>WLAN-Kennwort" + String(cfg.pass.length() ? " (leer lassen = unverändert)" : "") + "</label><input type=password name=pass autocomplete=new-password>";
  h += "<h2>Türklingel (Reolink)</h2><p>Zeigt bei Bewegung das Live-Bild. Empfohlen: ein eigener Benutzer auf der Klingel. Ein-/Ausschalten und die Anzeigedauer stehen auf der Startseite.</p>";
  h += "<label>Adresse der Klingel (IP; die Klingel antwortet meist nur auf HTTPS, das nutzt das Display selbst)</label><input type=text name=dhost value=\"" + esc(cfg.dhost) + "\" placeholder=\"192.168.1.20\">";
  h += "<label>Benutzer</label><input type=text name=duser value=\"" + esc(cfg.duser) + "\" autocomplete=off>";
  h += "<label>Kennwort" + String(cfg.dpass.length() ? " (leer lassen = unverändert)" : "") + "</label><input type=password name=dpass autocomplete=new-password>";
  h += "<label>Eigene Bildadresse (optional, JPEG per http, z. B. von go2rtc; sonst holt das Display das Bild bei der Klingel)</label><input type=text name=durl value=\"" + esc(cfg.durl) + "\">";
  h += "<h2>Nuki-Schloss</h2><p>Erzeuge auf web.nuki.io unter „API &amp; Integrationen“ einen API-Token (zum Anzeigen genügt das Recht zum Lesen, zum Bedienen braucht er auch das Recht zum Ausführen von Aktionen) und trage ihn hier ein. Ein-/Ausschalten geht am Display oder auf der Startseite der Webseite.</p>";
  h += "<label>API-Token" + String(cfg.ntoken.length() ? " (leer lassen = unverändert)" : "") + "</label><input type=password name=ntoken autocomplete=new-password>";
  h += "<button>Speichern und neu starten</button></form></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

static void handle_save() {
  if (!authed()) return;
  if (server.hasArg("ssid") && server.arg("ssid").length()) {
    cfg.ssid = server.arg("ssid");
    if (server.arg("pass").length() || !cfg.pass.length()) cfg.pass = server.arg("pass");
    cfg.dhost = server.arg("dhost");
    cfg.dhost.trim();
    cfg.duser = server.arg("duser");
    if (server.arg("dpass").length() || !cfg.duser.length()) cfg.dpass = server.arg("dpass");
    cfg.durl = server.arg("durl");
    cfg.durl.trim();
    if (server.arg("ntoken").length()) { cfg.ntoken = server.arg("ntoken"); cfg.ntoken.trim(); }
    cfg_save();
    server.send(200, "text/html; charset=utf-8", F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><body style='font:18px system-ui;background:#0b1226;color:#eef0f4;padding:30px'>Gespeichert. Das Display startet neu und verbindet sich mit dem WLAN; die neue Adresse steht dann am Display unter Einstellungen.</body>"));
    delay(800);
    ESP.restart();
  } else server.send(400, "text/plain", "WLAN fehlt");
}

static void handle_list() {
  if (!authed()) return;
  String j = "{\"slots\":[";
  for (int i = 0; i < IMG_SLOTS; i++) j += String(i ? "," : "") + (img_has(i) ? "true" : "false");
  j += String("],\"free\":") + (unsigned)(mounted ? FFat.freeBytes() : 0) + "}";
  server.send(200, "application/json", j);
}

static void handle_door() {
  if (!authed()) return;
  server.send(200, "text/plain; charset=utf-8", String(door_status()) + "\n" + door_stats() + "\n");
}

static void handle_prefs() {
  if (!authed()) return;
  if (server.method() == HTTP_POST) {
    auto num = [&](const char *k, int lo, int hi, int &v) { if (server.hasArg(k)) v = constrain(server.arg(k).toInt(), lo, hi); };
    int nOn = prefs.nightOn, nOff = prefs.nightOff;
    num("theme", 0, 2, prefs.theme);
    num("bright", 10, 100, prefs.bright);
    num("bg", -1, IMG_SLOTS - 1, prefs.bg);
    num("dim", 0, 90, prefs.dim);
    num("saver", 0, 2, prefs.saver);
    num("saverMin", 1, 60, prefs.saverMin);
    num("saverBright", 10, 100, prefs.saverBright);
    num("nightOn", 0, 1, nOn);
    num("nightOff", 0, 1, nOff);
    num("nightFrom", 0, 1439, prefs.nightFrom);
    num("nightTo", 0, 1439, prefs.nightTo);
    prefs.nightOn = nOn; prefs.nightOff = nOff;
    int dOn = cfg.door, dSecs = cfg.dsecs;
    num("doorOn", 0, 1, dOn);
    num("doorSecs", 3, 300, dSecs);
    if (server.hasArg("ntoken") && server.arg("ntoken").length()) { cfg.ntoken = server.arg("ntoken"); cfg.ntoken.trim(); cfg_save_door(); nuki_refresh(); }
    int kOn = cfg.nuki, kCtl = cfg.nctl;
    num("nukiOn", 0, 1, kOn);
    num("nukiCtl", 0, 1, kCtl);
    if (kCtl != cfg.nctl) { cfg.nctl = kCtl; cfg_save_door(); }
    if (kOn != cfg.nuki) { cfg.nuki = kOn; cfg_save_door(); nuki_refresh(); }
    if (dOn != cfg.door || dSecs != cfg.dsecs) { cfg.door = dOn; cfg.dsecs = dSecs; cfg_save_door(); }
    if (server.hasArg("doorTest")) door_test();
    prefs_save();
    prefsChanged = true;
  }
  String j = String("{\"theme\":") + prefs.theme + ",\"bright\":" + prefs.bright + ",\"bg\":" + prefs.bg + ",\"dim\":" + prefs.dim +
             ",\"saver\":" + prefs.saver + ",\"saverMin\":" + prefs.saverMin + ",\"saverBright\":" + prefs.saverBright + ",\"nightOn\":" + (prefs.nightOn ? "true" : "false") +
             ",\"nightOff\":" + (prefs.nightOff ? "true" : "false") + ",\"nightFrom\":" + prefs.nightFrom + ",\"nightTo\":" + prefs.nightTo + ",\"doorOn\":" + (cfg.door ? "true" : "false") + ",\"doorSecs\":" + cfg.dsecs + ",\"nukiOn\":" + (cfg.nuki ? "true" : "false") + ",\"nukiCtl\":" + (cfg.nctl ? "true" : "false") + ",\"nukiTok\":" + (cfg.ntoken.length() ? "true" : "false") + ",\"slots\":[";
  for (int i = 0; i < IMG_SLOTS; i++) j += String(i ? "," : "") + (img_has(i) ? "true" : "false");
  j += "]}";
  server.send(200, "application/json", j);
}

static void handle_img() {
  if (!authed()) return;
  const int slot = server.arg("slot").toInt();
  if (!img_has(slot)) { server.send(404, "text/plain", "kein Bild"); return; }
  File f = FFat.open(path_of(slot), "r");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handle_delete() {
  if (!authed()) return;
  const int slot = server.arg("slot").toInt();
  if (mounted && slot >= 0 && slot < IMG_SLOTS) { FFat.remove(path_of(slot)); imgNeedRestart = true; }
  imgRevision++;
  server.send(200, "text/plain", "ok");
}

// Der Upload kommt in kleinen Stücken; alles im PSRAM sammeln und am Ende in einem Zug schreiben (viel schneller).
static void handle_upload_data() {
  HTTPUpload &u = server.upload();
  if (!server.authenticate("display", web_pin().c_str())) { upOk = false; upLen = 0; return; }   // ohne PIN nichts annehmen
  if (u.status == UPLOAD_FILE_START) {
    upLen = 0;
    upOk = false;
    if (!upBuf) upBuf = img_alloc();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upBuf && upLen + u.currentSize <= IMG_BYTES) { memcpy(upBuf + upLen, u.buf, u.currentSize); upLen += u.currentSize; }
    else upLen = IMG_BYTES + 1;
  } else if (u.status == UPLOAD_FILE_END) {
    const int slot = server.arg("slot").toInt();
    if (mounted && upBuf && upLen == IMG_BYTES && slot >= 0 && slot < IMG_SLOTS) {
      File f = FFat.open(path_of(slot), "w");
      if (f) {
        // In kleinen Stücken: jeder Schreibvorgang sperrt kurz den Speicherzugriff, den die Bildübertragung braucht
        size_t done = 0;
        while (done < IMG_BYTES) {
          const size_t n = min((size_t)4096, IMG_BYTES - done);
          if (f.write(upBuf + done, n) != n) break;
          done += n;
          delay(1);
        }
        upOk = done == IMG_BYTES;
        f.close();
        if (!upOk) FFat.remove(path_of(slot));
        imgNeedRestart = true;
      }
    }
    if (upBuf) { heap_caps_free(upBuf); upBuf = nullptr; }
    if (upOk) imgRevision++;
  }
}

static void handle_upload_done() { if (!authed()) return; server.send(upOk ? 200 : 500, "text/plain", upOk ? "ok" : "Fehler"); }

void web_loop() {
  if ((!net_setup() && WiFi.status() != WL_CONNECTED)) return;
  if (!serverUp) {
    server.on("/", HTTP_GET, []() {
      if (!authed()) return;
      if (net_setup()) { handle_setup(); return; }
      server.send_P(200, "text/html; charset=utf-8", PAGE);
    });
    server.on("/setup", HTTP_GET, handle_setup);
    server.on("/save", HTTP_POST, handle_save);
    server.on("/api/list", HTTP_GET, handle_list);
    server.on("/img", HTTP_GET, handle_img);
    server.on("/api/prefs", HTTP_ANY, handle_prefs);
    server.on("/api/door", HTTP_GET, handle_door);
    server.on("/delete", HTTP_POST, handle_delete);
    server.on("/upload", HTTP_POST, handle_upload_done, handle_upload_data);
    server.begin();
    serverUp = true;
  }
  server.handleClient();
}
