#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <Adafruit_NeoPixel.h>

// Pins
#define BUTTON_PIN_1 2
#define BUTTON_PIN_2 4

// Captive portal
#define AP_SSID "LolinS3-Setup"
#define AP_PASSWORD ""

// Globals
WebServer server(80);
DNSServer dnsServer;
USBHIDKeyboard Keyboard;
Preferences prefs;

struct DeviceConfig {
  String wifi_ssid = "";
  String wifi_pass = "";
  String macro1 = "";
  String macro2 = "";
  String action1 = "macro"; // 'macro' | 'keystroke' | 'disabled'
  String action2 = "macro";
  String keymap1 = ""; // serialized keystroke payload
  String keymap2 = "";
} cfg;

// Status NeoPixel
#define STATUS_PIN 48
#define STATUS_COUNT 1
Adafruit_NeoPixel statusPix(STATUS_COUNT, STATUS_PIN, NEO_GRB + NEO_KHZ800);

enum DeviceLEDState { DS_UNKNOWN=0, DS_AP, DS_CONNECTING, DS_CONNECTED, DS_ERROR };
DeviceLEDState ledState = DS_UNKNOWN;

void setStatusLED(DeviceLEDState s) {
  ledState = s;
  uint32_t c = statusPix.Color(0,0,0);
  switch (s) {
    case DS_AP: c = statusPix.Color(0,0,64); break;         // blue
    case DS_CONNECTING: c = statusPix.Color(48,48,0); break; // yellow
    case DS_CONNECTED: c = statusPix.Color(0,64,0); break;   // green
    case DS_ERROR: c = statusPix.Color(64,0,0); break;       // red
    default: c = statusPix.Color(0,0,0); break;
  }
  statusPix.setPixelColor(0, c);
  statusPix.show();
}

// Debounce
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 50;
int lastButtonState1 = HIGH;
int lastButtonState2 = HIGH;

void saveConfig() {
  // Always attempt to save to LittleFS (best for human inspection)
  bool lf_ok = false;
  File f = LittleFS.open("/config.json", "w");
  if (f) {
    StaticJsonDocument<512> doc;
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["wifi_pass"] = cfg.wifi_pass;
    doc["macro1"] = cfg.macro1;
    doc["macro2"] = cfg.macro2;
    doc["action1"] = cfg.action1;
    doc["action2"] = cfg.action2;
    doc["keymap1"] = cfg.keymap1;
    doc["keymap2"] = cfg.keymap2;
    if (serializeJson(doc, f) > 0) {
      f.flush();
      lf_ok = true;
      Serial.println("Config saved to LittleFS");
    } else {
      Serial.println("Failed to write JSON to LittleFS file");
    }
    f.close();
  } else {
    Serial.println("LittleFS open('/config.json','w') failed");
  }

  // Also persist to NVS so settings survive if LittleFS is not available
  prefs.begin("macropad", false);
  prefs.putString("wifi_ssid", cfg.wifi_ssid);
  prefs.putString("wifi_pass", cfg.wifi_pass);
  prefs.putString("macro1", cfg.macro1);
  prefs.putString("macro2", cfg.macro2);
  prefs.putString("action1", cfg.action1);
  prefs.putString("action2", cfg.action2);
  prefs.putString("keymap1", cfg.keymap1);
  prefs.putString("keymap2", cfg.keymap2);
  prefs.end();
  Serial.println("Config saved to NVS (Preferences)");

  if (!lf_ok) {
    Serial.println("Warning: LittleFS save failed; using NVS as primary storage.");
  }
}

void loadConfig() {
  // Try LittleFS first
  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    if (f) {
      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        const char* _s1 = doc["wifi_ssid"] | "";
        const char* _p1 = doc["wifi_pass"] | "";
        const char* _m1 = doc["macro1"] | "";
        const char* _a1 = doc["action1"] | "macro";
        const char* _a2 = doc["action2"] | "macro";
        const char* _k1 = doc["keymap1"] | "";
        const char* _k2 = doc["keymap2"] | "";
        const char* _m2 = doc["macro2"] | "";
        cfg.wifi_ssid = String(_s1);
        cfg.wifi_pass = String(_p1);
        cfg.macro1 = String(_m1);
        cfg.action1 = String(_a1);
        cfg.action2 = String(_a2);
        cfg.keymap1 = String(_k1);
        cfg.keymap2 = String(_k2);
        cfg.macro2 = String(_m2);
        Serial.println("Config loaded from LittleFS");
        return;
      } else {
        Serial.println("Failed to parse config.json from LittleFS");
      }
    } else {
      Serial.println("LittleFS open('/config.json','r') failed");
    }
  } else {
    Serial.println("config.json not found on LittleFS");
  }

  // Fallback to Preferences (NVS)
  prefs.begin("macropad", true);
  cfg.wifi_ssid = prefs.getString("wifi_ssid", "");
  cfg.wifi_pass = prefs.getString("wifi_pass", "");
  cfg.macro1 = prefs.getString("macro1", "");
  cfg.macro2 = prefs.getString("macro2", "");
  prefs.end();
  Serial.println("Config loaded from NVS (Preferences)");

  // Also load action/keymap if present in NVS
  prefs.begin("macropad", true);
  cfg.action1 = prefs.getString("action1", cfg.action1);
  cfg.action2 = prefs.getString("action2", cfg.action2);
  cfg.keymap1 = prefs.getString("keymap1", cfg.keymap1);
  cfg.keymap2 = prefs.getString("keymap2", cfg.keymap2);
  prefs.end();
}

void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);
  setStatusLED(DS_AP);
}

String getIndexHtml() {
  // Serve different pages depending on whether we're connected (STA) or running AP for setup
  if (WiFi.status() == WL_CONNECTED) {
    // Connected: Synapse-like dark UI with device preview and per-button settings
    return R"=====([
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>LolinS3 — Device Config</title>
  <style>
    :root{--bg:#0b0d0f;--panel:#0f1113;--muted:#9aa7a6;--accent:#00ff6a}
    body{margin:0;font-family:Inter,Arial,Helvetica,sans-serif;background:var(--bg);color:#e6efef}
    .app{display:flex;min-height:100vh}
    .sidebar{width:320px;background:linear-gradient(180deg,#0d0f11 0,#0b0d0f 100%);padding:24px;box-shadow:2px 0 8px rgba(0,0,0,.6)}
    .main{flex:1;padding:24px}
    h1{margin:0 0 12px;font-size:20px}
    .device{background:linear-gradient(180deg,#07110a,#0b1210);border-radius:8px;padding:16px;text-align:center}
    .device svg{width:240px;height:auto}
    .controls{margin-top:12px}
    .btn{background:transparent;border:1px solid rgba(255,255,255,0.06);color:var(--muted);padding:8px 12px;border-radius:6px;margin-right:8px}
    .panel{background:var(--panel);padding:16px;border-radius:8px}
    .toolbar{display:flex;gap:12px;margin-bottom:12px}
    .row{display:flex;gap:12px;align-items:flex-start}
    label{display:block;font-size:13px;color:var(--muted);margin-bottom:6px}
    select,textarea,input{width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.04);background:#071013;color:#e6efef}
    textarea{min-height:80px}
    .button-list{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
    .bitem{background:#081010;padding:10px;border-radius:6px;border:1px solid rgba(255,255,255,0.03);cursor:pointer}
    .bitem.active{outline:2px solid var(--accent)}
    .save{background:var(--accent);color:#002108;border:none;padding:10px 14px;border-radius:8px;font-weight:600}
    .muted{color:var(--muted)}
  </style>
</head>
<body>
  <div class="app">
    <aside class="sidebar">
      <h1>Lolin S3</h1>
      <div class="device panel">
        <!-- Simple SVG device mockup with two buttons -->
        <svg viewBox="0 0 240 120" xmlns="http://www.w3.org/2000/svg">
          <rect x="8" y="8" width="224" height="104" rx="12" fill="#041011" stroke="#061617"/>
          <circle id="btn1" cx="70" cy="60" r="14" fill="#07201a" stroke="#083630"/>
          <circle id="btn2" cx="170" cy="60" r="14" fill="#07201a" stroke="#083630"/>
        </svg>
        <div class="controls muted">Click a button on the device to edit it.</div>
      </div>
      <div style="margin-top:12px" class="panel">
        <div class="muted">Status</div>
        <div id="statusText" class="muted">Not Connected</div>
      </div>
    </aside>

    <main class="main">
      <div class="toolbar">
        <div class="panel" style="flex:1">
          <div style="display:flex;justify-content:space-between;align-items:center">
            <div>
              <div class="muted">Active Profile</div>
              <div>Default</div>
            </div>
            <div>
              <button class="btn">New Profile</button>
              <button class="btn">Import</button>
              <button class="btn">Export</button>
            </div>
          </div>
        </div>
      </div>

      <div class="row">
        <div style="flex:0.5" class="panel">
          <div class="muted">Buttons</div>
          <div class="button-list" id="buttonList">
            <!-- JS populates button tiles -->
          </div>
        </div>

        <div style="flex:1" class="panel">
          <div class="muted">Button Settings</div>
          <div style="margin-top:8px">
            <label>Selected Button</label>
            <div id="selectedLabel">None</div>

            <label style="margin-top:12px">Action Type</label>
            <select id="actionType">
              <option value="macro">Text Macro</option>
              <option value="keystroke">Keystroke</option>
              <option value="disabled">Disabled</option>
            </select>

            <label style="margin-top:12px">Macro / Payload</label>
            <textarea id="actionPayload" placeholder="Enter text to send when button pressed"></textarea>

            <div id="keymapEditor" style="margin-top:8px;display:none">
              <label class="muted">Visual Keymap Editor</label>
              <div style="margin-top:8px">
                <div style="margin-bottom:6px">
                  <button class="btn" type="button" onclick="km_toggleMod('Ctrl')">Ctrl</button>
                  <button class="btn" type="button" onclick="km_toggleMod('Alt')">Alt</button>
                  <button class="btn" type="button" onclick="km_toggleMod('Shift')">Shift</button>
                </div>
                <div id="kmKeys" style="display:flex;flex-wrap:wrap;gap:6px">
                  <!-- keys populated by JS -->
                </div>
                <div style="margin-top:8px">
                  <button class="btn" type="button" onclick="km_backspace()">Backspace</button>
                  <button class="btn" type="button" onclick="km_clear()">Clear</button>
                </div>
              </div>
            </div>

            <div style="display:flex;gap:8px;margin-top:12px;align-items:center">
              <button class="save" id="saveBtn">Save</button>
              <div class="muted" id="saveStatus"></div>
            </div>
          </div>
        </div>
      </div>
    </main>
  </div>

  <script>
    // Define buttons (must match firmware mapping)
    const BUTTONS = [ { id:1, key:'macro1', pin:2 }, { id:2, key:'macro2', pin:4 } ];
    let selected = null;

    function makeTile(b){
      const d = document.createElement('div');
      d.className = 'bitem';
      d.id = 'tile-'+b.id;
      d.textContent = 'Button ' + b.id;
      d.onclick = ()=>selectButton(b.id);
      return d;
    }

    function selectButton(id){
      selected = BUTTONS.find(b=>b.id===id);
      document.querySelectorAll('.bitem').forEach(n=>n.classList.remove('active'));
      document.getElementById('tile-'+id).classList.add('active');
      document.getElementById('selectedLabel').textContent = 'Button ' + id + ' (pin ' + selected.pin + ')';
      // load payload and action type from currentConfig
      const actKey = 'action'+id;
      const macroKey = 'macro'+id;
      const keymapKey = 'keymap'+id;
      const act = (window.currentConfig && window.currentConfig[actKey]) ? window.currentConfig[actKey] : 'macro';
      document.getElementById('actionType').value = act;
      if (act === 'keystroke') {
        document.getElementById('actionPayload').value = (window.currentConfig && window.currentConfig[keymapKey]) ? window.currentConfig[keymapKey] : '';
      } else if (act === 'macro') {
        document.getElementById('actionPayload').value = (window.currentConfig && window.currentConfig[macroKey]) ? window.currentConfig[macroKey] : '';
      } else {
        document.getElementById('actionPayload').value = '';
      }
      // highlight SVG button
      try{document.querySelector('svg #btn'+id).setAttribute('fill','#083e2f');}catch(e){}
    }

    function buildUI(){
      const list = document.getElementById('buttonList');
      BUTTONS.forEach(b=>list.appendChild(makeTile(b)));
      // wire SVG clicks
      BUTTONS.forEach(b=>{
        const el = document.querySelector('svg #btn'+b.id);
        if(el) el.style.cursor='pointer', el.addEventListener('click', ()=>selectButton(b.id));
      });
    }

    async function fetchConfig(){
      try{
        let r = await fetch('/config');
        if(!r.ok) return;
        let j = await r.json();
        window.currentConfig = j;
        document.getElementById('statusText').textContent = 'Connected';
        // set initial payloads if a button is preselected
        if(BUTTONS.length) selectButton(BUTTONS[0].id);
      }catch(e){console.log(e);}
    }

    document.getElementById('saveBtn').addEventListener('click', async ()=>{
      if(!selected) return alert('Select a button first');
      const payload = document.getElementById('actionPayload').value;
      const actionType = document.getElementById('actionType').value;
      // Build body with existing config and updated selected keys
      const body = Object.assign({}, window.currentConfig || {});
      const id = selected.id;
      const macroKey = 'macro'+id;
      const actionKey = 'action'+id;
      const keymapKey = 'keymap'+id;
      if (actionType === 'macro') {
        body[actionKey] = 'macro';
        body[macroKey] = payload;
        body[keymapKey] = '';
      } else if (actionType === 'keystroke') {
        body[actionKey] = 'keystroke';
        body[keymapKey] = payload;
        // keep macroKey unchanged or clear
        body[macroKey] = body[macroKey] || '';
      } else {
        body[actionKey] = 'disabled';
        body[macroKey] = '';
        body[keymapKey] = '';
      }
      document.getElementById('saveStatus').textContent = 'Saving...';
      try{
        let r = await fetch('/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
        if(r.ok){ document.getElementById('saveStatus').textContent = 'Saved'; window.currentConfig = body; setTimeout(()=>document.getElementById('saveStatus').textContent='',1200);} else { document.getElementById('saveStatus').textContent = 'Save failed'; }
      }catch(e){ document.getElementById('saveStatus').textContent = 'Error'; }
    });

    // Keymap helper functions
    window.km_activeMods = {};
    function km_toggleMod(m) {
      if (window.km_activeMods[m]) delete window.km_activeMods[m]; else window.km_activeMods[m]=true;
      // toggle button style
      // append nothing yet; user clicks a key next
    }
    function km_appendKey(k) {
      // build token with mods if present
      const mods = Object.keys(window.km_activeMods);
      let token = '';
      if (mods.length) token = mods.join('+') + '+' + k; else token = k;
      // append to payload as sequence (space-separated)
      const ta = document.getElementById('actionPayload');
      if (ta.value && ta.value.length) ta.value = ta.value + ' ' + token; else ta.value = token;
      // reset mods
      window.km_activeMods = {};
    }
    function km_backspace(){
      const ta = document.getElementById('actionPayload');
      ta.value = ta.value.replace(/\s+$/,'');
      const parts = ta.value.split(/\s+/);
      parts.pop();
      ta.value = parts.join(' ');
    }
    function km_clear(){ document.getElementById('actionPayload').value = ''; }

    // populate visual keys A-Z,0-9
    function km_buildKeys(){
      const container = document.getElementById('kmKeys');
      const keys = [];
      for(let c=65;c<=90;c++) keys.push(String.fromCharCode(c));
      for(let n=0;n<=9;n++) keys.push(String(n));
      keys.push('Enter','Tab','Space');
      keys.forEach(k=>{
        const b = document.createElement('button');
        b.className='btn'; b.type='button'; b.textContent=k;
        b.onclick = ()=>km_appendKey(k==='Space'? 'Space' : k);
        container.appendChild(b);
      });
    }

    // show/hide keymap editor based on actionType
    document.getElementById('actionType').addEventListener('change', (e)=>{
      const v = e.target.value;
      const km = document.getElementById('keymapEditor');
      if (v === 'keystroke') km.style.display='block'; else km.style.display='none';
    });

    km_buildKeys();
    buildUI(); fetchConfig();
  </script>
</body>
</html>
 )=====";
  } else {
    // AP mode: only show wifi fields (no macros)
    return R"=====([
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>LolinS3 WiFi Setup</title>
  <style>body{font-family:Arial; padding:10px;}label{display:block;margin-top:8px;}input{width:100%;}</style>
</head>
<body>
  <h3>Setup WiFi</h3>
  <label>WiFi SSID: <input id="ssid" type="text" /></label>
  <label>WiFi Password: <input id="pass" type="password" /></label>
  <button id="save">Save & Connect</button>
  <p id="status"></p>

  <script>
  document.getElementById('save').addEventListener('click', async ()=>{
    let body = { wifi_ssid: document.getElementById('ssid').value, wifi_pass: document.getElementById('pass').value };
    document.getElementById('status').textContent = 'Saving...';
    try{
      let r = await fetch('/config', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
      if(r.ok) document.getElementById('status').textContent = 'Saved. Rebooting to apply...';
      else document.getElementById('status').textContent = 'Save failed';
    }catch(e){document.getElementById('status').textContent = 'Error: '+e;}
  });
  </script>
</body>
</html>
 )=====";
  }
}

void handleRoot() {
  server.send(200, "text/html", getIndexHtml());
}

void handleGetConfig() {
  StaticJsonDocument<512> doc;
  doc["wifi_ssid"] = cfg.wifi_ssid;
  doc["wifi_pass"] = cfg.wifi_pass;
  doc["macro1"] = cfg.macro1;
  doc["macro2"] = cfg.macro2;
  doc["action1"] = cfg.action1;
  doc["action2"] = cfg.action2;
  doc["keymap1"] = cfg.keymap1;
  doc["keymap2"] = cfg.keymap2;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostConfig() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "Body required");
    return;
  }
  String body = server.arg("plain");
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  // If we're in AP/setup mode, expect wifi fields and reboot after saving.
  if (WiFi.status() != WL_CONNECTED) {
    const char* _s2 = doc["wifi_ssid"] | "";
    const char* _p2 = doc["wifi_pass"] | "";
    cfg.wifi_ssid = String(_s2);
    cfg.wifi_pass = String(_p2);
    saveConfig();
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
    return;
  }
  // Otherwise we're connected: update macros, actions and keymaps (no reboot)
  const char* _m3 = doc["macro1"] | "";
  const char* _m4 = doc["macro2"] | "";
  const char* _a1 = doc["action1"] | "macro";
  const char* _a2 = doc["action2"] | "macro";
  const char* _k1 = doc["keymap1"] | "";
  const char* _k2 = doc["keymap2"] | "";
  cfg.macro1 = String(_m3);
  cfg.macro2 = String(_m4);
  cfg.action1 = String(_a1);
  cfg.action2 = String(_a2);
  cfg.keymap1 = String(_k1);
  cfg.keymap2 = String(_k2);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

// Try to connect to saved WiFi credentials. Returns true on success.
bool attemptWiFiConnect() {
  if (cfg.wifi_ssid.length() == 0) return false;
  Serial.printf("Attempting WiFi connect to '%s'...\n", cfg.wifi_ssid.c_str());
  setStatusLED(DS_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
  unsigned long start = millis();
  const unsigned long timeout = 10000;
  while (millis() - start < timeout) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connected. IP: ");
      Serial.println(WiFi.localIP());
      setStatusLED(DS_CONNECTED);
      return true;
    }
    delay(250);
  }
  Serial.println("WiFi connect attempt failed");
  setStatusLED(DS_ERROR);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin();
  }
  loadConfig();

  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);

  Keyboard.begin();
  USB.begin();
  statusPix.begin();
  statusPix.show();

  // Connection flow:
  // - If no saved creds: start captive portal (AP) so user can enter them.
  // - If creds exist: attempt to connect. On success continue.
  // - If connection fails: clear saved creds, save, and reboot so AP will appear.
  if (cfg.wifi_ssid.length() == 0) {
    Serial.println("No WiFi credentials saved — starting captive portal");
    startCaptivePortal();
  } else {
    if (attemptWiFiConnect()) {
      Serial.println("WiFi connected — continuing normal operation");
      // continue; server will run on station interface
    } else {
      Serial.println("Saved WiFi failed — clearing credentials and rebooting to AP");
      // flash error briefly
      for (int i=0;i<3;i++) { setStatusLED(DS_ERROR); delay(200); setStatusLED(DS_UNKNOWN); delay(100); }
      cfg.wifi_ssid = "";
      cfg.wifi_pass = "";
      saveConfig();
      delay(300);
      ESP.restart();
    }
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/config", HTTP_POST, handlePostConfig);
  server.begin();
}

void executeMacro(const String &m) {
  if (m.length() == 0) return;
  // simple: send as characters via HID keyboard
  Keyboard.print(m.c_str());
}

// Execute action for a given button id (1-based)
void executeAction(int id) {
  String action = (id == 1) ? cfg.action1 : cfg.action2;
  String macro = (id == 1) ? cfg.macro1 : cfg.macro2;
  String keymap = (id == 1) ? cfg.keymap1 : cfg.keymap2;

  if (action == "disabled") return;
  if (action == "macro") {
    executeMacro(macro);
    return;
  }
  if (action == "keystroke") {
    // Parse tokens separated by spaces. Each token may be like 'Ctrl+Shift+A' or 'Enter'
    String s = keymap;
    int pos = 0;
    while (pos < s.length()) {
      // find next token
      int sp = s.indexOf(' ', pos);
      String token;
      if (sp == -1) { token = s.substring(pos); pos = s.length(); }
      else { token = s.substring(pos, sp); pos = sp + 1; }
      token.trim();
      if (token.length() == 0) continue;

      // split modifiers and key by '+'
      int plus = token.lastIndexOf('+');
      String keypart = token;
      String mods = "";
      if (plus != -1) { mods = token.substring(0, plus); keypart = token.substring(plus + 1); }

      bool modCtrl = false, modAlt = false, modShift = false;
      if (mods.length()) {
        // parse each modifier
        int p = 0;
        while (p < mods.length()) {
          int p2 = mods.indexOf('+', p);
          String m;
          if (p2 == -1) { m = mods.substring(p); p = mods.length(); } else { m = mods.substring(p, p2); p = p2 + 1; }
          m.trim();
          m.toLowerCase();
          if (m == "ctrl" || m == "control") modCtrl = true;
          else if (m == "alt") modAlt = true;
          else if (m == "shift") modShift = true;
        }
      }

      // Press modifiers if supported
#ifdef KEY_LEFT_CTRL
      if (modCtrl) Keyboard.press(KEY_LEFT_CTRL);
#endif
#ifdef KEY_LEFT_ALT
      if (modAlt) Keyboard.press(KEY_LEFT_ALT);
#endif
#ifdef KEY_LEFT_SHIFT
      if (modShift) Keyboard.press(KEY_LEFT_SHIFT);
#endif

      // Send key
      String k = keypart;
      k.trim();
      if (k.equalsIgnoreCase("Enter")) {
        Keyboard.write('\n');
      } else if (k.equalsIgnoreCase("Tab")) {
        Keyboard.write('\t');
      } else if (k.equalsIgnoreCase("Space")) {
        Keyboard.write(' ');
      } else if (k.length() == 1) {
        // single character
        Keyboard.write(k.charAt(0));
      } else {
        // long names: try first char
        Keyboard.print(k.c_str());
      }

      delay(10);
      // release modifiers
#ifdef KEY_LEFT_CTRL
      if (modCtrl) Keyboard.release(KEY_LEFT_CTRL);
#endif
#ifdef KEY_LEFT_ALT
      if (modAlt) Keyboard.release(KEY_LEFT_ALT);
#endif
#ifdef KEY_LEFT_SHIFT
      if (modShift) Keyboard.release(KEY_LEFT_SHIFT);
#endif
      Keyboard.releaseAll();
      delay(10);
    }
  }
}

// ==========================================
// Serial debug interface
// ==========================================
void printHelpSerial() {
  Serial.println("Serial debug commands:");
  Serial.println("  help                : Show this help");
  Serial.println("  cfg                 : Print saved configuration");
  Serial.println("  macros              : Show macros");
  Serial.println("  set macro1 <text>   : Set macro for button 1 and save");
  Serial.println("  set macro2 <text>   : Set macro for button 2 and save");
  Serial.println("  set wifi <ssid> <pass> : Set WiFi credentials and save");
  Serial.println("  set wifi_ssid <ssid>   : Set saved WiFi SSID");
  Serial.println("  set wifi_pass <pass>   : Set saved WiFi password");
  Serial.println("  test1               : Trigger macro1 immediately");
  Serial.println("  test2               : Trigger macro2 immediately");
  Serial.println("  led [ap|connecting|connected|error|off] : Set status LED state");
  Serial.println("  status              : Print runtime status (WiFi, LED)");
  Serial.println("  reboot              : Restart the device");
  Serial.println("  wifi scan           : Scan nearby WiFi networks (shows SSID,RSSI,enc)");
  Serial.println("  wifi connect        : Try connecting to saved WiFi credentials");
}

void printConfigSerial() {
  Serial.println("--- Config ---");
  Serial.print("WiFi SSID: "); Serial.println(cfg.wifi_ssid);
  Serial.print("WiFi Pass: "); Serial.println(cfg.wifi_pass.length() ? "(hidden)" : "(empty)");
  Serial.print("Macro1: "); Serial.println(cfg.macro1);
  Serial.print("Macro2: "); Serial.println(cfg.macro2);
}

void printStatusSerial() {
  Serial.println("--- Status ---");
  Serial.print("WiFi status: "); Serial.println(WiFi.status());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  }
  Serial.print("LED state: "); Serial.println((int)ledState);
}

void wifiScanSerial() {
  Serial.println("Scanning WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found");
    return;
  }
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    String enc = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SEC";
    Serial.printf("%d: %s (%d dBm) %s\n", i+1, ssid.c_str(), rssi, enc.c_str());
  }
}

void handleSerial() {
  if (Serial.available() == 0) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  Serial.print("CMD: "); Serial.println(line);
  String lc = line;
  lc.toLowerCase();
  int sp = lc.indexOf(' ');
  String verb = (sp == -1) ? lc : lc.substring(0, sp);
  String arg = "";
  if (sp != -1) arg = line.substring(sp + 1);

  if (verb == "help") {
    printHelpSerial();
  } else if (verb == "cfg") {
    printConfigSerial();
  } else if (verb == "macros") {
    Serial.print("Macro1: "); Serial.println(cfg.macro1);
    Serial.print("Macro2: "); Serial.println(cfg.macro2);
  } else if (verb == "set") {
    // arg expected: "macro1 <text>" or "macro2 <text>" or "wifi <ssid> <pass>" or wifi_ssid/wifi_pass
    int sp2 = arg.indexOf(' ');
    if (sp2 == -1) {
      Serial.println("Usage: set macro1 <text> | set wifi <ssid> <pass> | set wifi_ssid <ssid> | set wifi_pass <pass>");
    } else {
      String key = arg.substring(0, sp2);
      String value = arg.substring(sp2 + 1);
      if (key == "macro1") {
        cfg.macro1 = value;
        saveConfig();
        Serial.println("macro1 saved");
      } else if (key == "macro2") {
        cfg.macro2 = value;
        saveConfig();
        Serial.println("macro2 saved");
      } else if (key == "wifi") {
        // value: '<ssid> <pass>' (password may contain spaces)
        int sp3 = value.indexOf(' ');
        if (sp3 == -1) {
          cfg.wifi_ssid = value;
          cfg.wifi_pass = "";
        } else {
          String ssid = value.substring(0, sp3);
          String pass = value.substring(sp3 + 1);
          cfg.wifi_ssid = ssid;
          cfg.wifi_pass = pass;
        }
        saveConfig();
        Serial.println("WiFi credentials saved");
      } else if (key == "wifi_ssid") {
        cfg.wifi_ssid = value;
        saveConfig();
        Serial.println("wifi_ssid saved");
      } else if (key == "wifi_pass") {
        cfg.wifi_pass = value;
        saveConfig();
        Serial.println("wifi_pass saved");
      } else {
        Serial.println("Unknown set key. Use macro1, macro2, wifi, wifi_ssid, or wifi_pass.");
      }
    }
  } else if (verb == "wifi") {
    String a = arg;
    a.trim();
    if (a.length() == 0) {
      Serial.println("Usage: wifi scan | wifi connect");
    } else if (a.startsWith("scan")) {
      wifiScanSerial();
    } else if (a.startsWith("connect")) {
      Serial.println("Attempting WiFi connect using saved credentials...");
      if (attemptWiFiConnect()) Serial.println("Connected"); else Serial.println("Connect failed");
    } else {
      Serial.println("Unknown wifi command. Use 'wifi scan' or 'wifi connect'");
    }
  } else if (verb == "test1") {
    Serial.println("Triggering macro1");
    executeAction(1);
  } else if (verb == "test2") {
    Serial.println("Triggering macro2");
    executeAction(2);
  } else if (verb == "led") {
    String a = arg;
    a.toLowerCase();
    if (a == "ap") setStatusLED(DS_AP);
    else if (a == "connecting") setStatusLED(DS_CONNECTING);
    else if (a == "connected") setStatusLED(DS_CONNECTED);
    else if (a == "error") setStatusLED(DS_ERROR);
    else if (a == "off") setStatusLED(DS_UNKNOWN);
    else Serial.println("Usage: led [ap|connecting|connected|error|off]");
  } else if (verb == "status") {
    printStatusSerial();
  } else if (verb == "reboot") {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  } else {
    Serial.println("Unknown command. Type 'help'.");
  }
}

void loop() {
  server.handleClient();
  dnsServer.processNextRequest();
  // Handle serial debug commands
  handleSerial();

  int reading1 = digitalRead(BUTTON_PIN_1);
  if (reading1 != lastButtonState1) {
    lastDebounceTime1 = millis();
  }
  if ((millis() - lastDebounceTime1) > debounceDelay) {
    if (reading1 == LOW && lastButtonState1 == HIGH) {
      executeAction(1);
    }
  }
  lastButtonState1 = reading1;

  int reading2 = digitalRead(BUTTON_PIN_2);
  if (reading2 != lastButtonState2) {
    lastDebounceTime2 = millis();
  }
  if ((millis() - lastDebounceTime2) > debounceDelay) {
    if (reading2 == LOW && lastButtonState2 == HIGH) {
      executeAction(2);
    }
  }
  lastButtonState2 = reading2;

  delay(10);
}
