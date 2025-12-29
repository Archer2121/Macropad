#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <Update.h>

/* ================= CONFIG ================= */
#define FIRMWARE_VERSION "1.0.0"

#define BTN1_PIN 2
#define BTN2_PIN 4
#define LED_PIN  48
#define LED_COUNT 1

/* ================= GLOBALS ================= */
USBHIDKeyboard Keyboard;
AsyncWebServer server(80);
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool lastB1 = HIGH;
bool lastB2 = HIGH;

/* ================= DEFAULT MACROS ================= */
String macroBtn1 = R"json(
[
  {"keys":["CTRL","C"],"delay":100},
  {"keys":["CTRL","V"],"delay":0}
]
)json";

String macroBtn2 = R"json(
[
  {"keys":["WIN","R"],"delay":0}
]
)json";

/* ================= KEY MAP ================= */
uint8_t keyFromString(String k) {
  k.toUpperCase();
  if (k == "CTRL")  return KEY_LEFT_CTRL;
  if (k == "SHIFT") return KEY_LEFT_SHIFT;
  if (k == "ALT")   return KEY_LEFT_ALT;
  if (k == "WIN")   return KEY_LEFT_GUI;
  if (k == "TAB")   return KEY_TAB;
  if (k == "ESC")   return KEY_ESC;
  if (k == "DEL")   return KEY_DELETE;
  if (k == "SPACE") return ' ';
  if (k.length() == 1) return k[0];
  return 0;
}

/* ================= MACRO ENGINE ================= */
void runMacro(String json) {
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, json)) return;

  for (JsonObject step : doc.as<JsonArray>()) {
    for (String k : step["keys"].as<JsonArray>()) {
      uint8_t c = keyFromString(k);
      if (c) Keyboard.press(c);
    }
    delay(40);
    Keyboard.releaseAll();
    delay(step["delay"] | 0);
  }
}

/* ================= LED ================= */
void flash(uint32_t color) {
  pixel.setPixelColor(0, color);
  pixel.show();
  delay(120);
  pixel.clear();
  pixel.show();
}

/* ================= WEB UI ================= */
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 Macropad</title>
<style>
body{background:#111;color:#fff;font-family:sans-serif}
.key{display:inline-block;padding:10px;margin:3px;border:1px solid #555;cursor:pointer}
.row{margin-bottom:5px}
textarea{width:100%;height:180px}
button{padding:10px;margin-top:10px}
select{padding:6px}
</style>
</head>
<body>

<h2>ESP32 Macropad Editor</h2>

<label>Target Button:</label>
<select id="target">
<option value="btn1">Button 1</option>
<option value="btn2">Button 2</option>
</select>

<div id="keyboard"></div>

<textarea id="macro"></textarea>

<button onclick="addStep()">Add Step</button>
<button onclick="save()">Save</button>

<hr>
<h3>OTA Firmware Update</h3>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware">
<input type="submit" value="Upload">
</form>

<script>
const layout=[
["ESC","TAB","CTRL","SHIFT","ALT","WIN"],
["Q","W","E","R","T","Y","U","I","O","P"],
["A","S","D","F","G","H","J","K","L"],
["Z","X","C","V","B","N","M"],
["SPACE","DEL"]
];

let macro=[];
let current={keys:[],delay:100};

function draw(){
 const k=document.getElementById("keyboard");
 k.innerHTML="";
 layout.forEach(r=>{
  let row=document.createElement("div");
  row.className="row";
  r.forEach(key=>{
   let b=document.createElement("div");
   b.className="key";
   b.textContent=key;
   b.onclick=()=>toggle(key);
   row.appendChild(b);
  });
  k.appendChild(row);
 });
}

function toggle(k){
 if(current.keys.includes(k))
  current.keys=current.keys.filter(x=>x!==k);
 else
  current.keys.push(k);
}

function addStep(){
 macro.push({...current});
 current={keys:[],delay:100};
 document.getElementById("macro").value=JSON.stringify(macro,null,2);
}

function save(){
 fetch("/save",{
  method:"POST",
  headers:{"Content-Type":"application/json"},
  body:JSON.stringify({
    target:document.getElementById("target").value,
    macro:macro
  })
 });
 alert("Saved");
}

draw();
</script>

</body>
</html>
)rawliteral";

/* ================= SETUP ================= */
void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.setBrightness(40);

  USB.begin();
  Keyboard.begin();

  WiFiManager wm;
  wm.autoConnect("Macropad-Setup");

  MDNS.begin("macropad");
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/version", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send(200, "text/plain", FIRMWARE_VERSION);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *r){
    StaticJsonDocument<2048> d;
    deserializeJson(d, r->arg("plain"));
    String out; serializeJson(d["macro"], out);
    if (d["target"] == "btn1") macroBtn1 = out;
    else macroBtn2 = out;
    r->send(200, "text/plain", "OK");
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *r){ r->send(200); },
    [](AsyncWebServerRequest*,String,size_t,uint8_t* data,size_t len,bool fin){
      if (!Update.isRunning()) Update.begin(UPDATE_SIZE_UNKNOWN);
      Update.write(data,len);
      if (fin) Update.end(true);
    });

  server.begin();
}

/* ================= LOOP ================= */
void loop() {
  ArduinoOTA.handle();

  bool b1 = digitalRead(BTN1_PIN);
  bool b2 = digitalRead(BTN2_PIN);

  if (lastB1 && !b1) { runMacro(macroBtn1); flash(pixel.Color(0,255,0)); }
  if (lastB2 && !b2) { runMacro(macroBtn2); flash(pixel.Color(0,0,255)); }

  lastB1=b1;
  lastB2=b2;
}
