// WebUi.cpp — captive-portal setup + admin panel served by the core WebServer.
//
// Pages are kept in PROGMEM; SETUP_HTML is streamed with send_P(), ADMIN_HTML
// is a template whose %TOKENS% are substituted with the current config values
// before sending (so /admin GET is always pre-filled).
//
// Route list (registered exactly once, from setupRoutes()):
//   /            GET  -> 302 /setup (or /admin when running)
//   /setup       GET  -> provisioning form
//   /setup       POST -> save ssid/psk, beginWifiConnect()
//   /scan        GET  -> JSON list of visible networks (max 20)
//   /status      GET  -> JSON {mode,ssid,ip,rssi,connecting,error}
//   /admin       GET  -> settings form, pre-filled
//   /admin       POST -> parse/clamp/sort/save + apply live, no reboot
//   /factory-reset POST -> clear NVS namespace + reboot
//   /favicon.ico GET  -> 204
//   captive-portal probe URLs -> 302 /setup

#include "WebUi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <FastLED.h>

#include "Config.h"
#include "Palette.h"
#include "Provisioning.h"

// Defined in TideTime.ino.
extern CRGB leds[MAX_LEDS];
extern volatile bool tideRefetchRequested;

WebServer server(80);

// ---------------------------------------------------------------------------
// Escaping helpers
// ---------------------------------------------------------------------------

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); ++i) {
    const char c = s[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  char buf[8];
  for (unsigned int i = 0; i < s.length(); ++i) {
    const unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      default:
        if (c < 0x20) {
          snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
          out += buf;
        } else {
          out += (char)c;
        }
        break;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Small parse/clamp/sanitise helpers
// ---------------------------------------------------------------------------

static long parseLongArg(const String& v) {
  return strtol(v.c_str(), nullptr, 10);
}

static long clampLong(long v, long lo, long hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Trim, allow only [A-Za-z0-9-], max 32 chars. Empty result means "invalid".
static String sanitizeStationIdArg(const String& raw) {
  String s = raw;
  s.trim();
  if (s.length() == 0 || s.length() > 32) {
    return String();
  }
  for (unsigned int i = 0; i < s.length(); ++i) {
    const char c = s[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || (c == '-');
    if (!ok) {
      return String();
    }
  }
  return s;
}

static void truncateTo5(String& s) {
  if (s.length() > 5) {
    s = s.substring(0, 5);
  }
}

// ---------------------------------------------------------------------------
// PROGMEM pages
// ---------------------------------------------------------------------------

static const char SETUP_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — WiFi setup</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
label{display:block;margin:.7rem 0 .25rem;font-size:.9rem}
input{width:100%;box-sizing:border-box;padding:.6rem;font-size:1rem;border:1px solid #34576e;border-radius:8px;background:#12314a;color:#e8f4f8}
button{margin-top:.9rem;width:100%;padding:.7rem;font-size:1rem;border:0;border-radius:8px;background:#31b3e8;color:#04263a;font-weight:600}
button.secondary{background:#1d4257;color:#cfe9f5}
#status{margin-top:.8rem;font-size:.9rem;min-height:1.2em;color:#ffd479}
</style></head><body><main>
<h1>TideTime</h1>
<p>Connect the device to your WiFi network. You are on its setup access point.</p>
<div id="status"></div>
<form method="POST" action="/setup">
 <label for="ssid">Network name (SSID)</label>
 <input id="ssid" name="ssid" maxlength="32" list="ssidList" autocomplete="off" required>
 <datalist id="ssidList"></datalist>
 <label for="psk">Password (leave empty for open networks)</label>
 <input id="psk" name="psk" type="password" maxlength="63" autocomplete="off">
 <button type="submit">Save and connect</button>
</form>
<button type="button" class="secondary" id="scanBtn">Scan for networks</button>
<script>
var st=document.getElementById('status');
document.getElementById('scanBtn').addEventListener('click',function(){
 st.textContent='Scanning...';
 fetch('/scan').then(function(r){return r.json();}).then(function(list){
  var dl=document.getElementById('ssidList'); dl.innerHTML='';
  for(var i=0;i<list.length;i++){
   var o=document.createElement('option');
   o.value=list[i].ssid;
   o.label=list[i].ssid+' ('+list[i].rssi+' dBm'+(list[i].secure?', secured':'')+')';
   dl.appendChild(o);
  }
  st.textContent=list.length?('Found '+list.length+' networks - start typing to pick one'):('No networks found');
 }).catch(function(){st.textContent='Scan failed - try again';});
});
fetch('/status').then(function(r){return r.json();}).then(function(s){
 if(s.error){st.textContent='Error: '+s.error;}
});
</script>)rawliteral";

static const char CONNECT_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — Connecting</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
p{font-size:.95rem;line-height:1.45}
a{color:#7fd0f2}
#msg{color:#ffd479}
</style></head><body><main>
<h1>TideTime</h1>
<p>Connecting to &#8220;%SSID%&#8221;&#8230;</p>
<p id="msg">Please wait&#8230;</p>
<p><a href="/setup">Back to setup</a></p>
<script>
var waited=0;
var msg=document.getElementById('msg');
function success(ip){
 msg.innerHTML='Connected! Device address: <a href="http://'+ip+'/">http://'+ip+'/</a><br>If this page stops responding, the device has left setup mode - open the address above on your network.';
}
function poll(){
 fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){
  waited++;
  if(s.mode==='RUNNING'){success(s.ip);}
  else if(s.connecting && s.ip && s.ip!=='192.168.4.1'){success(s.ip);}
  else if(!s.connecting && s.error){msg.textContent='Error: '+s.error+' - go back and try again';}
  else{msg.textContent='Connecting... ('+waited+' s)';}
  setTimeout(poll,1000);
 }).catch(function(){
  msg.textContent='Lost contact with the device. If it connected, the setup access point has shut down - check your router for its new address.';
  setTimeout(poll,1000);
 });
}
poll();
</script>)rawliteral";

static const char ADMIN_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — Settings</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:640px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
h2{font-size:1.05rem;margin:1.2rem 0 .4rem;color:#9fd6ef}
label{display:block;margin:.55rem 0 .2rem;font-size:.9rem}
input,select{box-sizing:border-box;padding:.55rem;font-size:1rem;border:1px solid #34576e;border-radius:8px;background:#12314a;color:#e8f4f8;max-width:100%}
input[type=number]{width:5.5rem}
input[type=range]{width:100%;padding:0;background:transparent}
input[type=color]{width:3rem;height:2.2rem;padding:0;border-radius:8px}
button{margin-top:1rem;width:100%;padding:.75rem;font-size:1rem;border:0;border-radius:8px;background:#31b3e8;color:#04263a;font-weight:600}
button.danger{background:#c9463d;color:#fff}
fieldset{border:0;margin:0;padding:0}
.note{font-size:.8rem;color:#ffd479;margin:.3rem 0;line-height:1.4}
.stop{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;margin:.45rem 0;padding:.45rem;border:1px solid #1d4257;border-radius:8px}
.stop label{margin:0;font-size:.8rem}
.stop input[type=number]{width:4.3rem}
.stop input[type=range]{flex:1 1 8rem;min-width:6rem}
.preview{height:2.4rem;border:1px solid #34576e;border-radius:8px;margin-bottom:.5rem;background:#000}
.row{display:flex;flex-wrap:wrap;gap:.6rem;align-items:flex-end}
.row label{margin:0}
.inline{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;margin:.35rem 0;font-size:.9rem}
.inline select{width:auto}
.posout{font-size:.8rem;min-width:2.4rem;text-align:right}
</style></head><body><main>
<h1>TideTime settings</h1>
<form method="POST" action="/admin">
<h2>Palette</h2>
<div id="preview" class="preview"></div>
<div class="stop"><span>Stop 0</span>
 <input type="color" id="c0" value="%COLOR0%">
 <label>R<input type="number" id="pal0r" name="pal0r" min="0" max="255" value="%PAL0R%"></label>
 <label>G<input type="number" id="pal0g" name="pal0g" min="0" max="255" value="%PAL0G%"></label>
 <label>B<input type="number" id="pal0b" name="pal0b" min="0" max="255" value="%PAL0B%"></label>
 <input type="range" id="palPos0" name="palPos0" min="0" max="255" value="%PALPOS0%">
 <span class="posout" id="posOut0">0</span></div>
<div class="stop"><span>Stop 1</span>
 <input type="color" id="c1" value="%COLOR1%">
 <label>R<input type="number" id="pal1r" name="pal1r" min="0" max="255" value="%PAL1R%"></label>
 <label>G<input type="number" id="pal1g" name="pal1g" min="0" max="255" value="%PAL1G%"></label>
 <label>B<input type="number" id="pal1b" name="pal1b" min="0" max="255" value="%PAL1B%"></label>
 <input type="range" id="palPos1" name="palPos1" min="0" max="255" value="%PALPOS1%">
 <span class="posout" id="posOut1">0</span></div>
<div class="stop"><span>Stop 2</span>
 <input type="color" id="c2" value="%COLOR2%">
 <label>R<input type="number" id="pal2r" name="pal2r" min="0" max="255" value="%PAL2R%"></label>
 <label>G<input type="number" id="pal2g" name="pal2g" min="0" max="255" value="%PAL2G%"></label>
 <label>B<input type="number" id="pal2b" name="pal2b" min="0" max="255" value="%PAL2B%"></label>
 <input type="range" id="palPos2" name="palPos2" min="0" max="255" value="%PALPOS2%">
 <span class="posout" id="posOut2">0</span></div>
<div class="stop"><span>Stop 3</span>
 <input type="color" id="c3" value="%COLOR3%">
 <label>R<input type="number" id="pal3r" name="pal3r" min="0" max="255" value="%PAL3R%"></label>
 <label>G<input type="number" id="pal3g" name="pal3g" min="0" max="255" value="%PAL3G%"></label>
 <label>B<input type="number" id="pal3b" name="pal3b" min="0" max="255" value="%PAL3B%"></label>
 <input type="range" id="palPos3" name="palPos3" min="0" max="255" value="%PALPOS3%">
 <span class="posout" id="posOut3">0</span></div>
<p class="note">Gradient runs from position 0 (sea / start of the strip) to 255 (sand / end of the strip). Stops are applied in position order.</p>

<h2>LEDs</h2>
<label>LED count (1-300)<input type="number" name="numLeds" min="1" max="300" value="%NUMLEDS%"></label>
<label for="brightness">Brightness: <output id="bOut" for="brightness">%BRIGHTNESS%</output></label>
<input type="range" id="brightness" name="brightness" min="0" max="255" value="%BRIGHTNESS%">

<h2>Timezone</h2>
<label><input type="checkbox" name="tzHasDst" id="tzHasDst" %TZHASDST_CHECKED%> Use daylight saving time (DST)</label>
<p class="note">Offsets are <b>minutes from UTC</b> (absolute, not a difference): 0 = GMT/UTC, 60 = UTC+1 (BST), -300 = UTC-5 (EST).</p>
<div class="row">
 <label>Standard name<input type="text" name="tzStdName" maxlength="5" size="7" value="%TZSTDNAME%"></label>
 <label>Daylight name<input type="text" name="tzDstName" maxlength="5" size="7" value="%TZDSTNAME%"></label>
</div>
<div class="row">
 <label>Standard offset (min from UTC)<input type="number" name="tzStdOffset" min="-720" max="840" value="%TZSTDOFFSET%"></label>
 <label>Daylight offset (min from UTC)<input type="number" name="tzDstOffset" min="-720" max="840" value="%TZDSTOFFSET%"></label>
</div>
<div class="inline">DST starts:
 <select name="tzDstMonth" id="tzDstMonth">
  <option value="0">January</option><option value="1">February</option><option value="2">March</option>
  <option value="3">April</option><option value="4">May</option><option value="5">June</option>
  <option value="6">July</option><option value="7">August</option><option value="8">September</option>
  <option value="9">October</option><option value="10">November</option><option value="11">December</option>
 </select>
 <select name="tzDstWeek" id="tzDstWeek">
  <option value="0">Last</option><option value="1">First</option><option value="2">Second</option>
  <option value="3">Third</option><option value="4">Fourth</option>
 </select>
 Sunday at hour <input type="number" name="tzDstHour" min="0" max="23" value="%DH%"></div>
<div class="inline">Standard time (DST ends) starts:
 <select name="tzStdMonth" id="tzStdMonth">
  <option value="0">January</option><option value="1">February</option><option value="2">March</option>
  <option value="3">April</option><option value="4">May</option><option value="5">June</option>
  <option value="6">July</option><option value="7">August</option><option value="8">September</option>
  <option value="9">October</option><option value="10">November</option><option value="11">December</option>
 </select>
 <select name="tzStdWeek" id="tzStdWeek">
  <option value="0">Last</option><option value="1">First</option><option value="2">Second</option>
  <option value="3">Third</option><option value="4">Fourth</option>
 </select>
 Sunday at hour <input type="number" name="tzStdHour" min="0" max="23" value="%SH%"></div>
<p class="note">Rules switch on the given Sunday of the month (Last = 0, First = 1, ...). Both rules use Sunday, matching the clock's behaviour.</p>

<h2>Tide station</h2>
<label>EasyTide station ID<input type="text" name="stationId" maxlength="32" value="%STATION_ID%"></label>
<p class="note">Letters, digits and dashes only (e.g. 0546). Tide data is re-fetched when this changes, and every 6 hours.</p>

<button type="submit">Save settings</button>
</form>
<form method="POST" action="/factory-reset" onsubmit="return confirm('Erase ALL settings (including WiFi credentials) and reboot?');">
<button type="submit" class="danger">Factory reset</button>
</form>
<script>
var sel={dm:%DM%,dw:%DW%,dh:%DH%,sm:%SM%,sw:%SW%,sh:%SH%};
document.getElementById('tzDstMonth').value=sel.dm;
document.getElementById('tzDstWeek').value=sel.dw;
document.getElementById('tzDstHour').value=sel.dh;
document.getElementById('tzStdMonth').value=sel.sm;
document.getElementById('tzStdWeek').value=sel.sw;
document.getElementById('tzStdHour').value=sel.sh;
var preview=document.getElementById('preview');
function $(id){return document.getElementById(id);}
function hex2(n){n=Number(n)&255;return (n<16?'0':'')+n.toString(16);}
function rgbToHex(r,g,b){return '#'+hex2(r)+hex2(g)+hex2(b);}
function hexToRgb(h){h=String(h||'').replace('#','');if(h.length<6)return [0,0,0];return [parseInt(h.substr(0,2),16)||0,parseInt(h.substr(2,2),16)||0,parseInt(h.substr(4,2),16)||0];}
function readStops(){var a=[];for(var i=0;i<4;i++){a.push({r:Number($('pal'+i+'r').value)||0,g:Number($('pal'+i+'g').value)||0,b:Number($('pal'+i+'b').value)||0,pos:Number($('palPos'+i).value)||0});}return a;}
function updatePreview(){
 var s=readStops().sort(function(a,b){return a.pos-b.pos;});
 var parts=[];for(var i=0;i<s.length;i++){parts.push('rgb('+s[i].r+','+s[i].g+','+s[i].b+') '+Math.round(s[i].pos/255*100)+'%');}
 preview.style.background='linear-gradient(to right,'+parts.join(',')+')';
}
for(var i=0;i<4;i++){(function(i){
 $('c'+i).addEventListener('input',function(){
  var v=hexToRgb(this.value);
  $('pal'+i+'r').value=v[0];$('pal'+i+'g').value=v[1];$('pal'+i+'b').value=v[2];
  updatePreview();
 });
 ['r','g','b'].forEach(function(ch){
  $('pal'+i+ch).addEventListener('input',function(){
   $('c'+i).value=rgbToHex($('pal'+i+'r').value,$('pal'+i+'g').value,$('pal'+i+'b').value);
   updatePreview();
  });
 });
 $('palPos'+i).addEventListener('input',function(){$('posOut'+i).textContent=this.value;updatePreview();});
 $('posOut'+i).textContent=$('palPos'+i).value;
})(i);}
document.getElementById('brightness').addEventListener('input',function(){document.getElementById('bOut').textContent=this.value;});
document.getElementById('bOut').textContent=document.getElementById('brightness').value;
updatePreview();
</script>)rawliteral";

static const char ADMIN_SAVED_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — Saved</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
p{font-size:.95rem;line-height:1.45}
a{color:#7fd0f2}
.warn{color:#ffd479}
</style></head><body><main>
<h1>Settings saved</h1>
<p>All settings were applied immediately - no reboot needed.</p>
%NOTE%
<p><a href="/admin">Back to settings</a></p>)rawliteral";

static const char RESET_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — Factory reset</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
p{font-size:.95rem;line-height:1.45}
</style></head><body><main>
<h1>Factory reset</h1>
<p>All settings erased. The device is rebooting and will come back up as the setup access point <b>TideTime-XXXX</b>. Connect to it to configure WiFi again.</p>)rawliteral";

static const char ERROR_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TideTime — Error</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:1rem;background:#0b1e2d;color:#e8f4f8}
main{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:.2rem 0 .8rem}
p{font-size:.95rem;line-height:1.45}
a{color:#7fd0f2}
.warn{color:#ffd479}
</style></head><body><main>
<h1>Problem</h1>
<p class="warn">%MSG%</p>
<p><a href="/setup">Back to setup</a></p>)rawliteral";

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

static void sendErrorPage(int code, const String& msg) {
  String html = ERROR_HTML;
  html.replace("%MSG%", htmlEscape(msg));
  server.send(code, "text/html", html);
}

static void redirect(const char* location) {
  server.sendHeader("Location", location, true);
  server.send(302, "text/plain", "");
}

static const char* modeName() {
  switch (deviceMode) {
    case MODE_SETUP_AP:   return "SETUP_AP";
    case MODE_CONNECT_STA: return "CONNECT_STA";
    default:               return "RUNNING";
  }
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

static void handleRoot() {
  if (deviceMode != MODE_RUNNING) {
    redirect("/setup");
  } else {
    redirect("/admin");
  }
}

static void handleSetupGet() {
  server.send_P(200, "text/html", SETUP_HTML);
}

void handleSetupSave() {
  String ssid = server.arg("ssid");
  ssid.trim();
  // Do not trim the PSK: passwords may legally contain leading/trailing spaces.
  String psk = server.arg("psk");

  if (ssid.length() == 0) {
    sendErrorPage(400, "Network name (SSID) must not be empty.");
    return;
  }
  if (ssid.length() > 32) {
    sendErrorPage(400, "Network name (SSID) must be 32 characters or fewer.");
    return;
  }
  if (psk.length() > 63 || (psk.length() > 0 && psk.length() < 8)) {
    sendErrorPage(400, "Password must be empty (open network) or 8-63 characters.");
    return;
  }
  if (ssid == apSsid()) {
    sendErrorPage(400, "That is this device's own setup network - pick your home WiFi instead.");
    return;
  }

  config.ssid = ssid;
  config.psk = psk;
  config.save();       // saved BEFORE connecting so a power cut cannot lose it
  beginWifiConnect();  // non-blocking; pollWifiConnect() drives it from loop()

  String page = CONNECT_HTML;
  page.replace("%SSID%", htmlEscape(ssid));
  server.send(200, "text/html", page);
}

static void handleScan() {
  const int n = WiFi.scanNetworks(false, true);
  String json = "[";
  int count = 0;
  if (n > 0) {
    for (int i = 0; i < n && count < 20; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) {
        continue;  // skip hidden networks with no broadcast SSID
      }
      if (count > 0) {
        json += ",";
      }
      json += "{\"ssid\":\"";
      json += jsonEscape(ssid);
      json += "\",\"rssi\":";
      json += String((int)WiFi.RSSI(i));
      json += ",\"secure\":";
      json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
      json += "}";
      ++count;
    }
  }
  json += "]";
  WiFi.scanDelete();  // always free the scan results
  server.send(200, "application/json", json);
}

static void handleStatus() {
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  const String ssid = wifiConnected ? WiFi.SSID() : config.ssid;
  const String ip = wifiConnected ? WiFi.localIP().toString()
                                  : WiFi.softAPIP().toString();
  const int rssi = wifiConnected ? (int)WiFi.RSSI() : 0;
  const bool connecting = (deviceMode == MODE_CONNECT_STA);

  String json = "{\"mode\":\"";
  json += modeName();
  json += "\",\"ssid\":\"";
  json += jsonEscape(ssid);
  json += "\",\"ip\":\"";
  json += ip;
  json += "\",\"rssi\":";
  json += String(rssi);
  json += ",\"connecting\":";
  json += connecting ? "true" : "false";
  json += ",\"error\":\"";
  json += jsonEscape(lastError);
  json += "\"}";
  server.send(200, "application/json", json);
}

static void handleAdminGet() {
  String html = ADMIN_HTML;  // ESP32: PROGMEM is memory-mapped, direct read is safe

  char hex[8];
  for (int i = 0; i < PALETTE_STOPS; ++i) {
    snprintf(hex, sizeof(hex), "#%02X%02X%02X",
             (unsigned)config.palette[i].r, (unsigned)config.palette[i].g,
             (unsigned)config.palette[i].b);
    html.replace(String("%COLOR") + i + "%", String(hex));
    html.replace(String("%PAL") + i + "R%", String((unsigned)config.palette[i].r));
    html.replace(String("%PAL") + i + "G%", String((unsigned)config.palette[i].g));
    html.replace(String("%PAL") + i + "B%", String((unsigned)config.palette[i].b));
    html.replace(String("%PALPOS") + i + "%", String((unsigned)config.palette[i].pos));
  }

  html.replace("%STATION_ID%", htmlEscape(config.stationId));
  html.replace("%NUMLEDS%", String((unsigned)config.numLeds));
  html.replace("%BRIGHTNESS%", String((unsigned)config.brightness));
  html.replace("%TZHASDST_CHECKED%", config.tzHasDst ? "checked" : "");
  html.replace("%TZSTDNAME%", htmlEscape(config.tzStdName));
  html.replace("%TZDSTNAME%", htmlEscape(config.tzDstName));
  html.replace("%TZSTDOFFSET%", String((int)config.tzStdOffset));
  html.replace("%TZDSTOFFSET%", String((int)config.tzDstOffset));
  html.replace("%DM%", String((unsigned)config.tzDstMonth));
  html.replace("%DW%", String((unsigned)config.tzDstWeek));
  html.replace("%DH%", String((unsigned)config.tzDstHour));
  html.replace("%SM%", String((unsigned)config.tzStdMonth));
  html.replace("%SW%", String((unsigned)config.tzStdWeek));
  html.replace("%SH%", String((unsigned)config.tzStdHour));

  server.send(200, "text/html", html);
}

void handleAdminSave() {
  // ---- Parse + clamp everything first ----

  const String station = sanitizeStationIdArg(server.arg("stationId"));
  const bool stationValid = (station.length() > 0);
  const bool stationChanged = stationValid && (station != config.stationId);

  const uint16_t newNumLeds =
      (uint16_t)clampLong(parseLongArg(server.arg("numLeds")), 1, MAX_LEDS);
  const uint8_t newBrightness =
      (uint8_t)clampLong(parseLongArg(server.arg("brightness")), 0, 255);

  PaletteStop stops[PALETTE_STOPS];
  for (int i = 0; i < PALETTE_STOPS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "pal%dr", i);
    stops[i].r = (uint8_t)clampLong(parseLongArg(server.arg(key)), 0, 255);
    snprintf(key, sizeof(key), "pal%dg", i);
    stops[i].g = (uint8_t)clampLong(parseLongArg(server.arg(key)), 0, 255);
    snprintf(key, sizeof(key), "pal%db", i);
    stops[i].b = (uint8_t)clampLong(parseLongArg(server.arg(key)), 0, 255);
    snprintf(key, sizeof(key), "palPos%d", i);
    stops[i].pos = (uint8_t)clampLong(parseLongArg(server.arg(key)), 0, 255);
  }

  // Sort the four stops ascending by position (insertion sort, n=4).
  for (int i = 1; i < PALETTE_STOPS; ++i) {
    const PaletteStop key = stops[i];
    int j = i - 1;
    while (j >= 0 && stops[j].pos > key.pos) {
      stops[j + 1] = stops[j];
      --j;
    }
    stops[j + 1] = key;
  }

  const bool newHasDst = server.hasArg("tzHasDst");  // unchecked checkbox sends nothing

  String stdName = server.arg("tzStdName");
  String dstName = server.arg("tzDstName");
  truncateTo5(stdName);
  truncateTo5(dstName);

  const int16_t newStdOffset =
      (int16_t)clampLong(parseLongArg(server.arg("tzStdOffset")), -720, 840);
  const int16_t newDstOffset =
      (int16_t)clampLong(parseLongArg(server.arg("tzDstOffset")), -720, 840);
  const uint8_t newDstMonth = (uint8_t)clampLong(parseLongArg(server.arg("tzDstMonth")), 0, 11);
  const uint8_t newDstWeek  = (uint8_t)clampLong(parseLongArg(server.arg("tzDstWeek")), 0, 4);
  const uint8_t newDstHour  = (uint8_t)clampLong(parseLongArg(server.arg("tzDstHour")), 0, 23);
  const uint8_t newStdMonth = (uint8_t)clampLong(parseLongArg(server.arg("tzStdMonth")), 0, 11);
  const uint8_t newStdWeek  = (uint8_t)clampLong(parseLongArg(server.arg("tzStdWeek")), 0, 4);
  const uint8_t newStdHour  = (uint8_t)clampLong(parseLongArg(server.arg("tzStdHour")), 0, 23);

  // ---- Commit to config + NVS ----

  const bool numLedsChanged = (newNumLeds != config.numLeds);

  if (stationValid) {
    config.stationId = station;
  } else {
    Serial.printf("[WebUi] invalid stationId \"%s\" - keeping \"%s\"\n",
                  server.arg("stationId").c_str(), config.stationId.c_str());
  }
  config.numLeds    = newNumLeds;
  config.brightness = newBrightness;
  for (int i = 0; i < PALETTE_STOPS; ++i) {
    config.palette[i] = stops[i];
  }
  config.tzHasDst    = newHasDst;
  config.tzStdName   = stdName;
  config.tzDstName   = dstName;
  config.tzStdOffset = newStdOffset;
  config.tzDstOffset = newDstOffset;
  config.tzDstMonth  = newDstMonth;
  config.tzDstWeek   = newDstWeek;
  config.tzDstHour   = newDstHour;
  config.tzStdMonth  = newStdMonth;
  config.tzStdWeek   = newStdWeek;
  config.tzStdHour   = newStdHour;
  config.save();

  // ---- Apply live, without a reboot ----

  applyPalette(config.palette);
  FastLED.setBrightness(config.brightness);
  FastLED.show();
  if (numLedsChanged) {
    // Blank the whole old range first so shrinking the count cannot leave
    // stale lit pixels beyond the new end of the strip.
    FastLED.clear();
    FastLED.show();
    FastLED.addLeds<WS2812, 2, GRB>(leds, config.numLeds);
    FastLED.show();
  }
  applyTimezone();
  ntp.update();
  if (stationChanged) {
    // The HTTPS fetch can block for seconds, so the main loop performs it;
    // the flag survives until WiFi + a valid clock allow the fetch.
    tideRefetchRequested = true;
  }

  String page = ADMIN_SAVED_HTML;
  page.replace("%NOTE%", stationValid ? ""
                                      : "<p class=\"warn\">The station ID was not valid, so the previous one was kept.</p>");
  server.send(200, "text/html", page);
}

static void handleFactoryReset() {
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, false)) {
    // Same namespace may be open elsewhere - close and retry once.
    p.end();
    p.begin(NVS_NAMESPACE, false);
  }
  p.clear();
  p.end();  // flushes the erase to flash
  Serial.println("[WebUi] Factory reset: NVS cleared, rebooting");
  server.send(200, "text/html", RESET_HTML);
  delay(500);  // let the response drain before the radio dies
  ESP.restart();
}

static void handleFavicon() {
  server.send(204, "text/plain", "");
}

static void handleCaptiveProbe() {
  redirect("/setup");
}

// ---------------------------------------------------------------------------
// Route registration (called exactly once, from setup())
// ---------------------------------------------------------------------------

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetupGet);
  server.on("/setup", HTTP_POST, handleSetupSave);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/admin", HTTP_GET, handleAdminGet);
  server.on("/admin", HTTP_POST, handleAdminSave);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);

  // Captive-portal detection probes (Android / iOS / macOS / Windows / Firefox).
  static const char* const captiveUrls[] = {
    "/generate_204",              // Android
    "/gen_204",                   // Android (alt)
    "/hotspot-detect.html",       // iOS / macOS
    "/library/test/success.html", // iOS / macOS (alt)
    "/redirect",                  // Windows
    "/ncsi.txt",                  // Windows
    "/connecttest.txt",           // Windows
    "/success.txt",               // Windows
    "/canonical.html",            // Firefox
    "/success",                   // Firefox
    "/default"                    // Firefox
  };
  for (const char* u : captiveUrls) {
    server.on(u, HTTP_GET, handleCaptiveProbe);
  }
}
