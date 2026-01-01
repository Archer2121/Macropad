// Rewritten Macropad firmware
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

#define BTN1_PIN 2
#define BTN2_PIN 4
#define LED_PIN 48

#define FW_VERSION "1.1.0"

USBHIDKeyboard Keyboard;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences prefs;

enum LedMode { LED_BOOT, LED_WIFI, LED_READY, LED_BTN1, LED_BTN2, LED_OTA, LED_ERROR };
volatile LedMode ledMode = LED_BOOT;

// defaults
String macro1 = "CTRL+ALT+DEL";
String macro2 = "WIN+D";
String saved_ssid = "";
String saved_pw = "";
bool apMode = false;
String apName;

// debounce
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastBtn1 = 0;
unsigned long lastBtn2 = 0;

// LED blink timing
unsigned long lastBlink = 0;
bool blinkState = false;

// helper: map some named keys
bool isModifier(const String &tok) {
  return tok == "CTRL" || tok == "ALT" || tok == "SHIFT" || tok == "WIN" || tok == "GUI";
}

void applyModifier(const String &tok) {
  if (tok == "CTRL") Keyboard.press(KEY_LEFT_CTRL);
  else if (tok == "ALT") Keyboard.press(KEY_LEFT_ALT);
  else if (tok == "SHIFT") Keyboard.press(KEY_LEFT_SHIFT);
  else if (tok == "WIN" || tok == "GUI") Keyboard.press(KEY_LEFT_GUI);
}

int namedKeyCode(const String &tok) {
  if (tok == "ENTER") return KEY_RETURN;
  if (tok == "TAB") return KEY_TAB;
  if (tok == "ESC" || tok == "ESCAPE") return KEY_ESC;
  if (tok == "SPACE") return ' ';
  if (tok == "BACKSPACE") return KEY_BACKSPACE;
  return 0;
}

// send a macro such as "CTRL+ALT+DEL" or "WIN+D" or single char
void sendMacro(const String &raw) {
  String macro = raw;
  macro.trim();
  if (macro.length() == 0) return;

  // split by + into a small fixed array to avoid dynamic allocations
  const int MAX_TOK = 8;
  String tokens[MAX_TOK];
  int tokCount = 0;
  int start = 0;
  int len = macro.length();
  for (int i = 0; i <= len && tokCount < MAX_TOK; ++i) {
    if (i == len || macro.charAt(i) == '+') {
      String tok = macro.substring(start, i);
      tok.trim();
      tok.toUpperCase();
      if (tok.length()) {
        tokens[tokCount++] = tok;
      }
      start = i + 1;
    }
  }

  // Apply modifiers
  for (int i = 0; i < tokCount; ++i) {
    if (isModifier(tokens[i])) applyModifier(tokens[i]);
  }

  // find last non-modifier token as the key
  String keyTok = "";
  for (int i = tokCount - 1; i >= 0; --i) {
    if (!isModifier(tokens[i])) { keyTok = tokens[i]; break; }
  }

  if (keyTok.length()) {
    int code = namedKeyCode(keyTok);
    if (code) {
      Keyboard.press(code);
    } else if (keyTok.length() == 1) {
      char c = keyTok.charAt(0);
      Keyboard.press(c);
    } else {
      // fallback: send first character
      Keyboard.press(keyTok.charAt(0));
    }
  }

  delay(40);
  Keyboard.releaseAll();
}

// LED update (non-blocking)
void updateLED() {
  unsigned long now = millis();
  if (now - lastBlink < 250) return;
  lastBlink = now;
  blinkState = !blinkState;

  uint32_t c = 0;
  switch (ledMode) {
    case LED_BOOT: c = pixel.Color(0, 0, 40); break;
    case LED_WIFI: c = blinkState ? pixel.Color(40, 30, 0) : 0; break;
    case LED_READY: c = pixel.Color(8, 8, 8); break;
    case LED_BTN1: c = pixel.Color(0, 48, 0); break;
    case LED_BTN2: c = pixel.Color(0, 36, 36); break;
    case LED_OTA: c = blinkState ? pixel.Color(32, 0, 32) : 0; break;
    case LED_ERROR: c = blinkState ? pixel.Color(48, 0, 0) : 0; break;
  }
  pixel.setPixelColor(0, c);
  pixel.show();
}

// Web endpoints
void handleRoot() {
  String html = "<h2>Macropad</h2><p>Firmware " FW_VERSION "</p>";
  html += "<p>Macros: m1=" + macro1 + " m2=" + macro2 + "</p>";
  server.send(200, "text/html", html);
}

void handleSet() {
  if (server.hasArg("m1")) macro1 = server.arg("m1");
  if (server.hasArg("m2")) macro2 = server.arg("m2");
  prefs.putString("m1", macro1);
  prefs.putString("m2", macro2);
  server.send(200, "text/plain", "Saved");
}

void handleGet() {
  String out = "{";
  out += "\"m1\": \"" + macro1 + "\",";
  out += "\"m2\": \"" + macro2 + "\"}";
  server.send(200, "application/json", out);
}

void setupWeb() {
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/wifi", [](){
    String page = "<h3>Configure WiFi</h3>";
    page += "<form action=\"/savewifi\" method=\"POST\">SSID: <input name='ssid'><br>Password: <input name='pw' type=\"password\"><br><input type=\"submit\" value=\"Save\"></form>";
    page += "<p><a href=\"/scan\">Scan networks</a></p>";
    page += "<p><a href=\"/\">Back</a></p>";
    server.send(200, "text/html", page);
  });
  server.on("/savewifi", HTTP_POST, [](){
    if (server.hasArg("ssid")) saved_ssid = server.arg("ssid");
    if (server.hasArg("pw")) saved_pw = server.arg("pw");
    prefs.putString("ssid", saved_ssid);
    prefs.putString("pw", saved_pw);
    String resp = "Saved WiFi credentials. Attempting to connect...<br><a href=\"/\">Home</a>";
    server.send(200, "text/html", resp);
  });
  server.on("/scan", [](){
    int n = WiFi.scanNetworks();
    String out = "<h3>Scan Results</h3>";
    out += "<ul>";
    for (int i=0;i<n;i++) {
      out += "<li>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + ") <a href=\"/wifi?ssid=" + WiFi.SSID(i) + "\">Select</a></li>";
    }
    out += "</ul>";
    out += "<p><a href=\"/\">Back</a></p>";
    server.send(200, "text/html", out);
  });
  server.on("/connect", [](){
    String html = "<h3>Connecting...</h3><p>If nothing happens, open <a href='/wifi'>WiFi setup</a></p>";
    server.send(200, "text/html", html);
  });
  server.on("/get", handleGet);
  server.begin();
}

// Setup
void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.setBrightness(40);
  ledMode = LED_BOOT;

  prefs.begin("macros", false);
  macro1 = prefs.getString("m1", macro1);
  macro2 = prefs.getString("m2", macro2);
  saved_ssid = prefs.getString("ssid", "");
  saved_pw = prefs.getString("pw", "");

  Keyboard.begin();
  USB.begin();

  ArduinoOTA.onStart([](){ ledMode = LED_OTA; });
  ArduinoOTA.onEnd([](){ ledMode = LED_READY; });

  // Try to connect with saved credentials first
  if (saved_ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(saved_ssid.c_str(), saved_pw.c_str());
    ledMode = LED_WIFI;
    unsigned long start = millis();
    while (millis() - start < 8000) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(200);
      updateLED();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    ledMode = LED_READY;
    setupWeb();
    // start OTA now that networking and server are initialized
    ArduinoOTA.begin();
  } else {
    // Start captive portal AP
    apName = "Macropad-Setup-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(8);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str());
    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(DNS_PORT, "*", apIP);
    apMode = true;
    ledMode = LED_WIFI;
    setupWeb();
    // start OTA in AP mode as well (after network/AP is configured)
    ArduinoOTA.begin();
  }
}

// Loop
void loop() {
  ArduinoOTA.handle();
  if (apMode) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  updateLED();

  // If user saved credentials via web UI, attempt to connect
  if (apMode && saved_ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(saved_ssid.c_str(), saved_pw.c_str());
    unsigned long start = millis();
    while (millis() - start < 8000) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(200);
      updateLED();
    }
    if (WiFi.status() == WL_CONNECTED) {
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      apMode = false;
      ledMode = LED_READY;
      setupWeb();
    } else {
      // failed, remain in AP mode
      ledMode = LED_ERROR;
    }
    // clear temp creds only after attempting
    // keep saved_ssid so device will auto-connect on next boot
  }

  unsigned long now = millis();

  // Button 1 (active low)
  if (!digitalRead(BTN1_PIN)) {
    if (now - lastBtn1 > DEBOUNCE_MS) {
      lastBtn1 = now;
      ledMode = LED_BTN1;
      sendMacro(macro1);
      // brief visual
      delay(150);
      ledMode = LED_READY;
    }
  }

  // Button 2
  if (!digitalRead(BTN2_PIN)) {
    if (now - lastBtn2 > DEBOUNCE_MS) {
      lastBtn2 = now;
      ledMode = LED_BTN2;
      sendMacro(macro2);
      delay(150);
      ledMode = LED_READY;
    }
  }
}
