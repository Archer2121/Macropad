#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

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

struct DeviceConfig {
  String wifi_ssid = "";
  String wifi_pass = "";
  String macro1 = "";
  String macro2 = "";
} cfg;

// Debounce
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 50;
int lastButtonState1 = HIGH;
int lastButtonState2 = HIGH;

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  StaticJsonDocument<512> doc;
  doc["wifi_ssid"] = cfg.wifi_ssid;
  doc["wifi_pass"] = cfg.wifi_pass;
  doc["macro1"] = cfg.macro1;
  doc["macro2"] = cfg.macro2;
  serializeJson(doc, f);
  f.close();
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  const char* _s1 = doc["wifi_ssid"] | "";
  const char* _p1 = doc["wifi_pass"] | "";
  const char* _m1 = doc["macro1"] | "";
  const char* _m2 = doc["macro2"] | "";
  cfg.wifi_ssid = String(_s1);
  cfg.wifi_pass = String(_p1);
  cfg.macro1 = String(_m1);
  cfg.macro2 = String(_m2);
}

void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);
}

String getIndexHtml() {
  // Minimal single-file UI that fetches/saves JSON
  return R"=====([
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>LolinS3 Buttons</title>
  <style>body{font-family:Arial; padding:10px;}label{display:block;margin-top:8px;}textarea{width:100%;height:80px;}</style>
</head>
<body>
  <h3>Lolin S3 Button Config</h3>
  <label>WiFi SSID: <input id="ssid" type="text" /></label>
  <label>WiFi Password: <input id="pass" type="password" /></label>
  <label>Button 1 Macro: <textarea id="m1" placeholder="Text to send as HID on Button 1"></textarea></label>
  <label>Button 2 Macro: <textarea id="m2" placeholder="Text to send as HID on Button 2"></textarea></label>
  <button id="save">Save</button>
  <p id="status"></p>

  <script>
  async function fetchConfig(){
    try{
      let r = await fetch('/config');
      if(!r.ok) return;
      let j = await r.json();
      document.getElementById('ssid').value = j.wifi_ssid || '';
      document.getElementById('pass').value = j.wifi_pass || '';
      document.getElementById('m1').value = j.macro1 || '';
      document.getElementById('m2').value = j.macro2 || '';
    }catch(e){console.log(e);}
  }
  document.getElementById('save').addEventListener('click', async ()=>{
    let body = {
      wifi_ssid: document.getElementById('ssid').value,
      wifi_pass: document.getElementById('pass').value,
      macro1: document.getElementById('m1').value,
      macro2: document.getElementById('m2').value
    };
    document.getElementById('status').textContent = 'Saving...';
    try{
      let r = await fetch('/config', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
      if(r.ok) document.getElementById('status').textContent = 'Saved. Rebooting to apply...';
      else document.getElementById('status').textContent = 'Save failed';
    }catch(e){document.getElementById('status').textContent = 'Error: '+e;}
  });
  fetchConfig();
  </script>
</body>
</html>
 )=====";
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
  const char* _s2 = doc["wifi_ssid"] | "";
  const char* _p2 = doc["wifi_pass"] | "";
  const char* _m3 = doc["macro1"] | "";
  const char* _m4 = doc["macro2"] | "";
  cfg.wifi_ssid = String(_s2);
  cfg.wifi_pass = String(_p2);
  cfg.macro1 = String(_m3);
  cfg.macro2 = String(_m4);
  saveConfig();
  server.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

void attemptWiFiConnect() {
  if (cfg.wifi_ssid.length() == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
  unsigned long start = millis();
  while (millis() - start < 8000) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(200);
  }
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

  attemptWiFiConnect();
  if (WiFi.status() != WL_CONNECTED) {
    startCaptivePortal();
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

void loop() {
  server.handleClient();
  dnsServer.processNextRequest();

  int reading1 = digitalRead(BUTTON_PIN_1);
  if (reading1 != lastButtonState1) {
    lastDebounceTime1 = millis();
  }
  if ((millis() - lastDebounceTime1) > debounceDelay) {
    if (reading1 == LOW && lastButtonState1 == HIGH) {
      executeMacro(cfg.macro1);
    }
  }
  lastButtonState1 = reading1;

  int reading2 = digitalRead(BUTTON_PIN_2);
  if (reading2 != lastButtonState2) {
    lastDebounceTime2 = millis();
  }
  if ((millis() - lastDebounceTime2) > debounceDelay) {
    if (reading2 == LOW && lastButtonState2 == HIGH) {
      executeMacro(cfg.macro2);
    }
  }
  lastButtonState2 = reading2;

  delay(10);
}
