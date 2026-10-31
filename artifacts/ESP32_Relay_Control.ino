#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <BluetoothSerial.h>
#include <IRremote.hpp>

#define PIN_CLOCK  18
#define PIN_DATA   19
#define PIN_LATCH  21
#define PIN_ENABLE 22
#define PIN_IR_RX  15
#define PIN_IR_TX  4

struct IRCode {
  uint32_t value;
  uint8_t  bits;
  uint8_t  protocol;
  char     raw[48];
};

struct Config {
  char     ssid[32];
  char     pass[64];
  uint32_t ip, gw, mask;
  uint8_t  static_ip;
  uint8_t  activeProtocol; // 1=IR 2=BLE 3=WiFi 4=MQTT
  char     ble_name[32];
  char     ble_addr[48];
  char     mqtt_server[40];
  uint16_t mqtt_port;
  char     mqtt_user[32];
  char     mqtt_pass[32];
  char     mqtt_topic[40];
  char     mqtt_codes[16][24];
  uint8_t  ir_rx_pin;
  uint8_t  ir_tx_pin;
  IRCode   irCodes[16];
  uint32_t valid;
};

#define CONFIG_MAGIC 0xABCD1243

WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
BluetoothSerial SerialBT;
uint16_t relayStates = 0;
bool statusIR = false, statusBLE = false, statusWiFi = false, statusMQTT = false;
bool isLearning = false;
uint8_t learningRelay = 0;
uint32_t learnStartTime = 0;
Config config;

void updateShiftRegister();
void handleRoot();
void handleSetRelay();
void handleAllRelays();
void handleStatus();
void handleSetProtocol();
void handleSettingsPage();
void handleScanWifi();
void handleSaveConfig();
void handleLearnIR();
void handleGetIRCodes();
void handleBleInfo();
void loadConfig();
void saveConfig();
void updateConnections();
void applyProtocol(uint8_t proto);
void handleBLE();
String btMac();
String jsonEscape(const char* s);

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  digitalWrite(PIN_ENABLE, LOW);
  digitalWrite(PIN_CLOCK, LOW);
  digitalWrite(PIN_LATCH, LOW);
  digitalWrite(PIN_DATA, LOW);

  EEPROM.begin(sizeof(Config));
  loadConfig();
  updateShiftRegister();
  digitalWrite(PIN_ENABLE, HIGH);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32-Relay-Control", "12345678");
  if (config.activeProtocol < 1 || config.activeProtocol > 4) config.activeProtocol = 3;
  applyProtocol(config.activeProtocol);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle", HTTP_GET, handleSetRelay);
  server.on("/all", HTTP_GET, handleAllRelays);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/protocol", HTTP_GET, handleSetProtocol);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/scan", HTTP_GET, handleScanWifi);
  server.on("/save", HTTP_POST, handleSaveConfig);
  server.on("/learn", HTTP_GET, handleLearnIR);
  server.on("/ircodes", HTTP_GET, handleGetIRCodes);
  server.on("/bleinfo", HTTP_GET, handleBleInfo);
  server.begin();
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
  updateConnections();
  handleBLE();

  if (isLearning) {
    if (millis() - learnStartTime > 10000) isLearning = false;
    else if (IrReceiver.decode()) {
      uint32_t v = IrReceiver.decodedIRData.decodedRawData;
      config.irCodes[learningRelay].value = v;
      config.irCodes[learningRelay].bits = IrReceiver.decodedIRData.numberOfBits;
      config.irCodes[learningRelay].protocol = IrReceiver.decodedIRData.protocol;
      String dump = String(v, HEX);
      dump.toUpperCase();
      strncpy(config.irCodes[learningRelay].raw, dump.c_str(), 47);
      saveConfig();
      isLearning = false;
      IrReceiver.resume();
    }
  } else if (config.activeProtocol == 1 && IrReceiver.decode()) {
    IrReceiver.resume();
  }

  if (config.activeProtocol == 4 && statusWiFi) {
    if (!mqtt.connected()) mqtt.connect("ESP32-Relay", config.mqtt_user, config.mqtt_pass);
    else mqtt.loop();
  }
}

String btMac() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(buf);
}

void applyProtocol(uint8_t proto) {
  IrReceiver.end();
  SerialBT.end();
  mqtt.disconnect();
  if (proto < 1 || proto > 4) proto = 3;
  config.activeProtocol = proto;
  switch (proto) {
    case 1:
      IrReceiver.begin(config.ir_rx_pin ? config.ir_rx_pin : PIN_IR_RX, ENABLE_LED_FEEDBACK);
      IrSender.begin(config.ir_tx_pin ? config.ir_tx_pin : PIN_IR_TX);
      break;
    case 2:
      SerialBT.begin(strlen(config.ble_name) ? config.ble_name : "ESP32-Relay");
      if (!strlen(config.ble_addr)) strncpy(config.ble_addr, btMac().c_str(), 47);
      break;
    case 3:
    case 4:
      if (strlen(config.ssid)) {
        if (config.static_ip && config.ip)
          WiFi.config(IPAddress(config.ip), IPAddress(config.gw), IPAddress(config.mask));
        WiFi.begin(config.ssid, config.pass);
      }
      if (proto == 4 && strlen(config.mqtt_server))
        mqtt.setServer(config.mqtt_server, config.mqtt_port ? config.mqtt_port : 1883);
      break;
  }
  saveConfig();
}

void updateConnections() {
  statusWiFi = WiFi.status() == WL_CONNECTED;
  statusBLE = (config.activeProtocol == 2) && SerialBT.hasClient();
  statusMQTT = (config.activeProtocol == 4) && mqtt.connected();
  statusIR = config.activeProtocol == 1;
}

void handleBLE() {
  if (config.activeProtocol != 2 || !SerialBT.available()) return;
  String cmd = SerialBT.readStringUntil('\n');
  cmd.trim();
  if (cmd.startsWith("IR,")) {
    int p1 = cmd.indexOf(',');
    int p2 = cmd.indexOf(',', p1 + 1);
    if (p1 > 0 && p2 > p1) {
      int relay = cmd.substring(p1 + 1, p2).toInt();
      if (relay >= 0 && relay < 16) {
        config.irCodes[relay].value = strtoul(cmd.substring(p2 + 1).c_str(), NULL, 16);
        config.irCodes[relay].bits = 32;
        saveConfig();
        SerialBT.println("OK");
      }
    }
  }
}

void updateShiftRegister() {
  digitalWrite(PIN_LATCH, LOW);
  for (int i = 15; i >= 0; i--) {
    digitalWrite(PIN_CLOCK, LOW);
    digitalWrite(PIN_DATA, (relayStates & (1 << i)) ? HIGH : LOW);
    digitalWrite(PIN_CLOCK, HIGH);
  }
  digitalWrite(PIN_LATCH, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_LATCH, LOW);
}

void handleLearnIR() {
  if (!server.hasArg("relay")) {
    server.send(400, "application/json", "{\"error\":\"relay missing\"}");
    return;
  }
  int relay = server.arg("relay").toInt();
  if (relay < 0 || relay > 15) {
    server.send(400, "application/json", "{\"error\":\"invalid relay\"}");
    return;
  }
  isLearning = true;
  learningRelay = relay;
  learnStartTime = millis();
  if (config.activeProtocol != 1) applyProtocol(1);
  IrReceiver.start();
  server.send(200, "application/json", "{\"status\":\"learning\",\"relay\":" + String(relay) + "}");
}

void handleGetIRCodes() {
  String json = "[";
  for (int i = 0; i < 16; i++) {
    if (i) json += ",";
    String hex = String(config.irCodes[i].value, HEX);
    hex.toUpperCase();
    json += "{\"dec\":" + String(config.irCodes[i].value);
    json += ",\"hex\":\"" + hex + "\"";
    json += ",\"raw\":\"" + String(config.irCodes[i].raw) + "\"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleBleInfo() {
  String json = "{\"name\":\"" + String(config.ble_name) + "\",\"address\":\"" + btMac() + "\"}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>16-Relay Control</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,sans-serif}
body{background:#0f172a;color:#f8fafc;padding:16px}
.header{display:flex;justify-content:space-between;align-items:center;max-width:700px;margin:0 auto 16px}
h1{font-size:1.35rem;color:#38bdf8}
.nav-btn{background:#334155;color:#fff;text-decoration:none;padding:8px 14px;border-radius:8px}
.proto-box{max-width:700px;margin:0 auto 18px;display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.proto-item{display:flex;align-items:center;gap:8px;background:#0f172a;padding:12px;border-radius:8px;border:1px solid #334155}
.proto-item.active{border-color:#22c55e;background:#052e16}
.controls{display:flex;gap:10px;max-width:700px;margin:0 auto 18px}
.ctrl-btn{flex:1;padding:12px;border:none;border-radius:8px;font-weight:bold;cursor:pointer;color:#fff}
.btn-on{background:#16a34a}.btn-off{background:#dc2626}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:12px;max-width:700px;margin:0 auto}
.card{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:16px;text-align:center;cursor:pointer}
.card.active{background:#1e3a8a;border-color:#3b82f6}
</style></head><body>
<div class="header"><h1>16-Relay Dashboard</h1><a href="/settings" class="nav-btn">Settings</a></div>
<div class="proto-box">
<label class="proto-item" id="pIR"><input type="radio" name="p" value="1" onchange="setProtocol(1)">IR</label>
<label class="proto-item" id="pBLE"><input type="radio" name="p" value="2" onchange="setProtocol(2)">BLE</label>
<label class="proto-item" id="pWiFi"><input type="radio" name="p" value="3" onchange="setProtocol(3)">WiFi</label>
<label class="proto-item" id="pMQTT"><input type="radio" name="p" value="4" onchange="setProtocol(4)">MQTT</label>
</div>
<div class="controls">
<button class="ctrl-btn btn-on" onclick="setAll(1)">ALL ON</button>
<button class="ctrl-btn btn-off" onclick="setAll(0)">ALL OFF</button>
</div>
<div class="grid" id="grid"></div>
<script>
let states=0,active=3;
function render(){
  let g=document.getElementById('grid'); g.innerHTML='';
  for(let i=0;i<16;i++){
    let on=states&(1<<i);
    g.innerHTML+=`<div class="card ${on?'active':''}" onclick="toggle(${i})"><b>Relay ${i+1}</b><div>${on?'ON':'OFF'}</div></div>`;
  }
  ['IR','BLE','WiFi','MQTT'].forEach(p=>document.getElementById('p'+p).classList.remove('active'));
  const m={1:'IR',2:'BLE',3:'WiFi',4:'MQTT'};
  document.getElementById('p'+m[active]).classList.add('active');
  let r=document.querySelector(`input[value="${active}"]`); if(r) r.checked=true;
}
function poll(){fetch('/status').then(r=>r.json()).then(d=>{states=d.states;active=d.active;render();});}
function toggle(i){fetch('/toggle?id='+i).then(r=>r.json()).then(d=>{states=d.states;render();});}
function setAll(v){fetch('/all?state='+v).then(r=>r.json()).then(d=>{states=d.states;render();});}
function setProtocol(v){fetch('/protocol?id='+v).then(r=>r.json()).then(d=>{active=d.active;render();});}
poll(); setInterval(poll,1500);
</script></body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleSetRelay() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < 16) {
      relayStates ^= (1 << id);
      updateShiftRegister();
    }
  }
  server.send(200, "application/json", "{\"states\":" + String(relayStates) + "}");
}

void handleAllRelays() {
  if (server.hasArg("state")) {
    relayStates = server.arg("state").toInt() ? 0xFFFF : 0;
    updateShiftRegister();
  }
  server.send(200, "application/json", "{\"states\":" + String(relayStates) + "}");
}

void handleStatus() {
  updateConnections();
  String json = "{\"states\":" + String(relayStates) +
                ",\"active\":" + String(config.activeProtocol) +
                ",\"IR\":" + String(statusIR ? "true" : "false") +
                ",\"BLE\":" + String(statusBLE ? "true" : "false") +
                ",\"WiFi\":" + String(statusWiFi ? "true" : "false") +
                ",\"MQTT\":" + String(statusMQTT ? "true" : "false") +
                ",\"learning\":" + String(isLearning ? "true" : "false") +
                ",\"learnRelay\":" + String(learningRelay) + "}";
  server.send(200, "application/json", json);
}

void handleSetProtocol() {
  if (server.hasArg("id")) {
    uint8_t id = server.arg("id").toInt();
    if (id >= 1 && id <= 4) applyProtocol(id);
  }
  handleStatus();
}

void handleSettingsPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Settings</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,sans-serif}
body{background:#0f172a;color:#f8fafc;padding:16px}
.container{max-width:560px;margin:0 auto;background:#1e293b;padding:20px;border-radius:12px}
h2{margin:18px 0 10px;color:#38bdf8;font-size:1.05rem}
label{display:block;margin:8px 0 4px;font-size:.8rem;color:#94a3b8}
input,select,textarea{width:100%;padding:10px;background:#0f172a;border:1px solid #334155;color:#fff;border-radius:6px}
.proto-btns{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.proto-btn{padding:14px 4px;border:none;border-radius:8px;background:#334155;color:#e2e8f0;font-weight:700;cursor:pointer}
.proto-btn.active{background:#16a34a;color:#fff}
.card{background:#0f172a;padding:12px;border-radius:8px;margin-bottom:10px;border:1px solid #334155}
.btn{width:100%;padding:12px;border:none;border-radius:8px;background:#2563eb;color:#fff;font-weight:700;margin-top:12px;cursor:pointer}
.btn-scan{background:#334155}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.back{color:#94a3b8;text-decoration:none;display:inline-block;margin-bottom:12px}
</style></head><body><div class="container">
<a class="back" href="/">&larr; Dashboard</a>
<h2 style="margin-top:0">Protocol</h2>
<div class="proto-btns">
<button type="button" class="proto-btn" id="btnIR" onclick="setProtocol(1)">IR</button>
<button type="button" class="proto-btn" id="btnBLE" onclick="setProtocol(2)">BLE</button>
<button type="button" class="proto-btn" id="btnWiFi" onclick="setProtocol(3)">WiFi</button>
<button type="button" class="proto-btn" id="btnMQTT" onclick="setProtocol(4)">MQTT</button>
</div>
<p id="hint" style="color:#94a3b8;font-size:.85rem;margin:10px 0"></p>
<form action="/save" method="POST">
<div id="secIR" style="display:none">
<p style="color:#94a3b8;font-size:.8rem">HEX, DEC and raw dump for every relay. Learn listens 10 seconds.</p>
<div id="irList"></div>
</div>
<div id="secBLE" style="display:none">
<button type="button" class="btn btn-scan" onclick="scanBle()">Scan BLE for ESP32</button>
<label>Device name</label><input name="ble_name" id="ble_name">
<label>Device address</label><input name="ble_addr" id="ble_addr">
<p id="bleMsg" style="color:#94a3b8;font-size:.8rem;margin-top:8px"></p>
</div>
<div id="secWiFi" style="display:none">
<button type="button" class="btn btn-scan" onclick="scanWifi()">Scan SSIDs</button>
<label>SSID</label>
<select id="ssidSelect" onchange="document.getElementById('ssid').value=this.value"><option value="">Select...</option></select>
<input id="ssid" name="ssid" style="margin-top:6px">
<label>Password</label><input type="password" name="pass" id="pass">
<label>IP mode</label>
<select name="ip_mode" id="ip_mode" onchange="document.getElementById('sf').style.display=this.value=='1'?'block':'none'">
<option value="0">DHCP</option><option value="1">Static</option></select>
<div id="sf">
<label>IP address</label><input name="ip" id="ip">
<label>Gateway</label><input name="gw" id="gw">
<label>Mask</label><input name="mask" id="mask">
</div>
</div>
<div id="secMQTT" style="display:none">
<label>Server</label><input name="mqtt_server" id="mqtt_server">
<label>Port</label><input name="mqtt_port" id="mqtt_port">
<label>User</label><input name="mqtt_user" id="mqtt_user">
<label>Password</label><input type="password" name="mqtt_pass" id="mqtt_pass">
<label>Topic</label><input name="mqtt_topic" id="mqtt_topic">
<h2>Code for each relay</h2>
<div id="mqList"></div>
</div>
<button class="btn" type="submit">Save & reboot</button>
</form></div>
<script>
let active=3;
const saved=)rawliteral";
  html += "{ssid:" + jsonEscape(config.ssid) + ",pass:" + jsonEscape(config.pass);
  html += ",ipMode:" + String(config.static_ip);
  html += ",ip:" + jsonEscape(config.ip ? IPAddress(config.ip).toString().c_str() : "");
  html += ",gw:" + jsonEscape(config.gw ? IPAddress(config.gw).toString().c_str() : "");
  html += ",mask:" + jsonEscape(config.mask ? IPAddress(config.mask).toString().c_str() : "255.255.255.0");
  html += ",bleName:" + jsonEscape(config.ble_name);
  html += ",bleAddr:" + jsonEscape(strlen(config.ble_addr) ? config.ble_addr : btMac().c_str());
  html += ",mqttServer:" + jsonEscape(config.mqtt_server);
  html += ",mqttPort:" + jsonEscape(String(config.mqtt_port ? config.mqtt_port : 1883).c_str());
  html += ",mqttUser:" + jsonEscape(config.mqtt_user);
  html += ",mqttPass:" + jsonEscape(config.mqtt_pass);
  html += ",mqttTopic:" + jsonEscape(config.mqtt_topic);
  html += ",mqttCodes:[";
  for (int i = 0; i < 16; i++) {
    if (i) html += ",";
    html += jsonEscape(config.mqtt_codes[i]);
  }
  html += "],active:" + String(config.activeProtocol) + "};";
  html += R"rawliteral(
function fill(){
  document.getElementById('ssid').value=saved.ssid;
  document.getElementById('pass').value=saved.pass;
  document.getElementById('ip_mode').value=String(saved.ipMode||0);
  document.getElementById('sf').style.display=saved.ipMode==1?'block':'none';
  document.getElementById('ip').value=saved.ip; document.getElementById('gw').value=saved.gw; document.getElementById('mask').value=saved.mask;
  document.getElementById('ble_name').value=saved.bleName||'ESP32-Relay';
  document.getElementById('ble_addr').value=saved.bleAddr;
  document.getElementById('mqtt_server').value=saved.mqttServer;
  document.getElementById('mqtt_port').value=saved.mqttPort;
  document.getElementById('mqtt_user').value=saved.mqttUser;
  document.getElementById('mqtt_pass').value=saved.mqttPass;
  document.getElementById('mqtt_topic').value=saved.mqttTopic;
  let mq='';
  for(let i=0;i<16;i++) mq+=`<label>Relay ${i+1}</label><input name="mqtt_code_${i}" value="${saved.mqttCodes[i]||''}">`;
  document.getElementById('mqList').innerHTML=mq;
}
function show(){
  ['IR','BLE','WiFi','MQTT'].forEach(p=>document.getElementById('btn'+p).classList.remove('active'));
  const m={1:'IR',2:'BLE',3:'WiFi',4:'MQTT'};
  document.getElementById('btn'+m[active]).classList.add('active');
  document.getElementById('secIR').style.display=active===1?'block':'none';
  document.getElementById('secBLE').style.display=active===2?'block':'none';
  document.getElementById('secWiFi').style.display=active===3?'block':'none';
  document.getElementById('secMQTT').style.display=active===4?'block':'none';
  const hints={1:'IR codes for all 16 relays',2:'Scan and store the ESP32 BLE address',3:'Scan SSID, save password and IP',4:'Broker settings and a code per relay'};
  document.getElementById('hint').innerText=hints[active];
}
function setProtocol(v){fetch('/protocol?id='+v).then(r=>r.json()).then(d=>{active=d.active;show();});}
function renderIR(){
  let html='';
  for(let i=0;i<16;i++){
    html+=`<div class="card"><b>Relay ${i+1}</b><div class="row"><div><label>HEX</label><input name="ir_hex_${i}" id="hex${i}"></div><div><label>DEC</label><input name="ir_dec_${i}" id="dec${i}"></div></div><label>RAW dump</label><textarea name="ir_raw_${i}" id="raw${i}" rows="2"></textarea><button type="button" class="btn btn-scan" id="learn${i}" onclick="startLearn(${i})">Learn</button></div>`;
  }
  document.getElementById('irList').innerHTML=html;
  fetch('/ircodes').then(r=>r.json()).then(codes=>{
    codes.forEach((c,i)=>{document.getElementById('dec'+i).value=c.dec||'';document.getElementById('hex'+i).value=c.hex||'';document.getElementById('raw'+i).value=c.raw||'';});
  });
}
let learning=false;
function startLearn(relay){
  if(learning) return; learning=true;
  let b=document.getElementById('learn'+relay); b.innerText='Listening...';
  fetch('/learn?relay='+relay).then(()=>{
    let t=setInterval(()=>fetch('/status').then(r=>r.json()).then(s=>{
      if(!s.learning){clearInterval(t);learning=false;b.innerText='Learn';renderIR();}
    }),700);
  });
}
async function scanBle(){
  const msg=document.getElementById('bleMsg');
  if(!navigator.bluetooth){msg.innerText='Use Chrome on Android to scan. This board address is filled below.';return;}
  try{
    const d=await navigator.bluetooth.requestDevice({filters:[{namePrefix:'ESP'}],optionalServices:[]});
    document.getElementById('ble_name').value=d.name||'ESP32';
    document.getElementById('ble_addr').value=d.id||'';
    msg.innerText='Found '+d.name+'. Press Save.';
  }catch(e){msg.innerText='Scan cancelled';}
}
function scanWifi(){
  let s=document.getElementById('ssidSelect'); s.innerHTML='<option>Scanning...</option>';
  fetch('/scan').then(r=>r.json()).then(d=>{
    s.innerHTML='<option value="">Select...</option>';
    d.forEach(n=>s.innerHTML+=`<option value="${n.ssid}">${n.ssid} (${n.rssi} dBm)</option>`);
  });
}
fetch('/status').then(r=>r.json()).then(d=>{active=d.active||3;fill();renderIR();show();});
</script></body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

String jsonEscape(const char* s) {
  String o = "\"";
  for (const char* p = s; *p; p++) {
    if (*p == '\\' || *p == '"') o += '\\';
    if (*p == '\n' || *p == '\r') continue;
    o += *p;
  }
  o += "\"";
  return o;
}

void handleScanWifi() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSaveConfig() {
  if (server.hasArg("ssid")) {
    strncpy(config.ssid, server.arg("ssid").c_str(), 31);
    strncpy(config.pass, server.arg("pass").c_str(), 63);
    config.static_ip = server.arg("ip_mode").toInt();
    IPAddress ip, gw, mask;
    if (ip.fromString(server.arg("ip"))) config.ip = (uint32_t)ip;
    if (gw.fromString(server.arg("gw"))) config.gw = (uint32_t)gw;
    if (mask.fromString(server.arg("mask"))) config.mask = (uint32_t)mask;
  }
  if (server.hasArg("mqtt_server")) {
    strncpy(config.mqtt_server, server.arg("mqtt_server").c_str(), 39);
    config.mqtt_port = server.arg("mqtt_port").toInt();
    strncpy(config.mqtt_user, server.arg("mqtt_user").c_str(), 31);
    strncpy(config.mqtt_pass, server.arg("mqtt_pass").c_str(), 31);
    strncpy(config.mqtt_topic, server.arg("mqtt_topic").c_str(), 39);
  }
  if (server.hasArg("ble_name")) strncpy(config.ble_name, server.arg("ble_name").c_str(), 31);
  if (server.hasArg("ble_addr")) strncpy(config.ble_addr, server.arg("ble_addr").c_str(), 47);
  for (int i = 0; i < 16; i++) {
    String key = "mqtt_code_" + String(i);
    if (server.hasArg(key)) strncpy(config.mqtt_codes[i], server.arg(key).c_str(), 23);
    String hexKey = "ir_hex_" + String(i);
    String decKey = "ir_dec_" + String(i);
    String rawKey = "ir_raw_" + String(i);
    if (server.hasArg(hexKey) && server.arg(hexKey).length()) {
      String h = server.arg(hexKey);
      if (h.startsWith("0x") || h.startsWith("0X")) h = h.substring(2);
      config.irCodes[i].value = strtoul(h.c_str(), NULL, 16);
    } else if (server.hasArg(decKey) && server.arg(decKey).length()) {
      config.irCodes[i].value = strtoul(server.arg(decKey).c_str(), NULL, 10);
    }
    if (server.hasArg(rawKey)) strncpy(config.irCodes[i].raw, server.arg(rawKey).c_str(), 47);
  }
  config.valid = CONFIG_MAGIC;
  saveConfig();
  server.send(200, "text/html", "<html><body style='background:#0f172a;color:#fff;text-align:center;padding:40px;font-family:sans-serif'><h2>Saved. Rebooting...</h2></body></html>");
  delay(1200);
  ESP.restart();
}

void loadConfig() {
  EEPROM.get(0, config);
  if (config.valid != CONFIG_MAGIC) {
    memset(&config, 0, sizeof(config));
    config.activeProtocol = 3;
    config.mqtt_port = 1883;
    config.ir_rx_pin = PIN_IR_RX;
    config.ir_tx_pin = PIN_IR_TX;
    strcpy(config.ble_name, "ESP32-Relay");
    strcpy(config.mqtt_topic, "relay/control");
  }
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}
