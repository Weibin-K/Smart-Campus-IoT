#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>

char ssid[] = "weibin";
char pass[] = "31415926";

const char broker[] = "broker.emqx.io";
const int MQTT_PORT = 1883;
const char CLIENT_ID[] = "sitg7-energy-uno02";

const int LED_PIN = 5;
const int FAN_PIN = 9;

const unsigned long OFF_DELAY_MS = 120000UL;
unsigned long lastOccupiedMs = 0;
bool haveOccupied = false;

String currentLocation = "";
bool manualFan = false;
bool manualLight = false;
bool manualLightOn = false;
int lightBrightness = 100;
bool simOccupied = false;
bool simDark = false;
int manualFanPct = 0;

const int DARK_THRESHOLD = 8;
const int BRIGHT_THRESHOLD = 16;

const float TEMP_HIGH_THRESHOLD = 31.0;
const float HUM_HIGH_THRESHOLD = 75.0;

const int FAN_LOW = 70;
const int FAN_LOW_MEDIUM = 78;
const int FAN_MEDIUM = 85;
const int FAN_MEDIUM_HIGH = 92;
const int FAN_HIGH = 100;
const unsigned long FAN_KICK_MS = 300UL;

const unsigned long DATA_TIMEOUT = 20000UL;

const unsigned long PUBLISH_INTERVAL = 2000UL;
const unsigned long WIFI_RETRY_INTERVAL = 15000UL;
const unsigned long MQTT_RETRY_INTERVAL = 10000UL;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

float actualTemperature = NAN;
float actualHumidity = NAN;
bool reportedOccupied = false;
int ldrValue = 0;
bool gotTemp = false, gotHum = false, gotOccupancy = false, gotLdr = false;
unsigned long lastTempMsg = 0, lastHumMsg = 0, lastOccupancyMsg = 0, lastLdrMsg = 0;

bool dhtValid = false;
bool roomValid = false;

bool occupied = false;
bool isDark = false;
bool lightingOn = false;

bool simulationMode = false;
String selectedScenario = "NORMAL";
int crowdLevel = 0;

float effectiveTemperature = NAN;
float effectiveHumidity = NAN;
bool effectiveDataValid = false;
String hvacDetailed = "SENSOR_FAULT";
String hvacStandard = "SENSOR_FAULT";

int fanPercent = 0;
int fanOutput = -1;
bool fanKicking = false;
unsigned long fanKickStart = 0;

bool wifiTried = false, mqttTried = false;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastPublish = 0;
int publishStep = 0;

void applyCrowdLevel(String value) {
  if (value == "0" || value == "1" || value == "2" || value == "3") {
    crowdLevel = value.toInt();
  } else {
    Serial.println("ERROR: invalid crowd level");
  }
}

void handleLightCommand(String v) {
  if (v == "AUTO") { manualLight = false; Serial.println("Light: back to AUTO"); }
  else if (v == "ON") { manualLight = true; manualLightOn = true; Serial.println("Light: manual ON"); }
  else if (v == "OFF") { manualLight = true; manualLightOn = false; Serial.println("Light: manual OFF"); }
  else { Serial.println("ERROR: light must be AUTO, ON or OFF"); }
}

void handleBrightnessCommand(String v) {
  int b = v.toInt();
  if (b == 25 || b == 50 || b == 75 || b == 100) {
    lightBrightness = b;
    Serial.print("Brightness: "); Serial.print(b); Serial.println("%");
  } else {
    Serial.println("ERROR: brightness must be 25, 50, 75 or 100");
  }
}

void handleOccupancyCommand(String v) {
  if (!simulationMode) { Serial.println("Occupancy command ignored: not in SIMULATION mode"); return; }
  if (v == "OCCUPIED") { simOccupied = true; }
  else if (v == "VACANT") { simOccupied = false; }
  else { Serial.println("ERROR: occupancy must be OCCUPIED or VACANT"); return; }
  Serial.print("Occupancy (simulated): ");
  Serial.println(simOccupied ? "OCCUPIED" : "VACANT");
}

void handleAmbientCommand(String v) {
  if (!simulationMode) { Serial.println("Ambient command ignored: not in SIMULATION mode"); return; }
  if (v == "DARK") { simDark = true; }
  else if (v == "BRIGHT") { simDark = false; }
  else { Serial.println("ERROR: ambient must be DARK or BRIGHT"); return; }
  Serial.print("Ambient (simulated): ");
  Serial.println(simDark ? "DARK" : "BRIGHT");
}

void handleFanCommand(String v) {
  int colon = v.indexOf(':');
  String val = (colon < 0) ? v : v.substring(colon + 1);
  val.trim();

  if (val == "AUTO") {
    manualFan = false;
    Serial.println("Fan: back to AUTO");
    return;
  }
  int pct = -1;
  if (val == "OFF")         { pct = 0; }
  else if (val == "LOW")    { pct = FAN_LOW; }
  else if (val == "MEDIUM") { pct = FAN_MEDIUM; }
  else if (val == "HIGH")   { pct = FAN_HIGH; }
  else if (val.length() > 0 && isDigit(val[0])) { pct = val.toInt(); }

  if (pct < 0 || pct > 100) { Serial.println("ERROR: invalid fan value"); return; }
  manualFan = true;
  manualFanPct = pct;
  Serial.print("Fan: manual ");
  Serial.print(pct);
  Serial.println("%");
}

void onMqttMessage(int messageSize) {
  (void)messageSize;

  String topic = mqttClient.messageTopic();
  String value = "";
  while (mqttClient.available()) {
    char c = (char)mqttClient.read();
    if (value.length() < 40) { value = value + c; }
  }
  value.trim();
  value.toUpperCase();

  if (topic == "sitg7/room1/temp") {
    float t = value.toFloat();
    if (t > 0 && t < 60) { actualTemperature = t; lastTempMsg = millis(); gotTemp = true; }
  }
  else if (topic == "sitg7/room1/hum") {
    float h = value.toFloat();
    if (h > 0 && h <= 100) { actualHumidity = h; lastHumMsg = millis(); gotHum = true; }
  }
  else if (topic == "sitg7/room1/occupancy") {
    if (value == "OCCUPIED" || value == "VACANT") {
      reportedOccupied = (value == "OCCUPIED");
      lastOccupancyMsg = millis();
      gotOccupancy = true;
    }
  }
  else if (topic == "sitg7/room1/ldr") {
    if (value.length() > 0 && isDigit(value[0]) && value.toInt() <= 1023) {
      ldrValue = value.toInt();
      lastLdrMsg = millis();
      gotLdr = true;
    }
  }
  else if (topic == "sitg7/room1/crowd") {
    if (!simulationMode) { applyCrowdLevel(value); }
  }

  else if (topic == "sitg7/room1/location") {
    currentLocation = value;
  }

  else if (topic.endsWith("/cmd/fan")) {
    handleFanCommand(value);
  }
  else if (topic.endsWith("/cmd/light")) {
    handleLightCommand(value);
  }
  else if (topic.endsWith("/cmd/brightness")) {
    handleBrightnessCommand(value);
  }
  else if (topic.endsWith("/cmd/occupancy")) {
    handleOccupancyCommand(value);
  }
  else if (topic.endsWith("/cmd/ambient")) {
    handleAmbientCommand(value);
  }

  else if (topic.endsWith("/cmd/mode")) {
    if (value == "LIVE") { simulationMode = false; Serial.println("Mode: LIVE"); }
    else if (value == "SIMULATION") { simulationMode = true; Serial.println("Mode: SIMULATION"); }
    else { Serial.println("ERROR: invalid mode"); }
  }
  else if (topic.endsWith("/cmd/scenario")) {
    if (value == "NORMAL" || value == "HOT" || value == "HUMID" || value == "HOT_HUMID") {
      selectedScenario = value;
      Serial.print("Scenario: ");
      Serial.println(selectedScenario);
    } else {
      Serial.println("ERROR: invalid scenario");
    }
  }
  else if (topic.endsWith("/cmd/crowd")) {
    if (simulationMode) { applyCrowdLevel(value); }
    else { Serial.println("Phone crowd ignored in LIVE mode (Board 1 sends the crowd level)"); }
  }
}

bool connectMQTT() {
  Serial.println("Connecting to MQTT broker...");
  bool ok = mqttClient.connect(broker, MQTT_PORT);
  lastMQTTAttempt = millis();
  if (ok) {
    Serial.println("MQTT connected");
    mqttClient.subscribe("sitg7/room1/temp");
    mqttClient.subscribe("sitg7/room1/hum");
    mqttClient.subscribe("sitg7/room1/occupancy");
    mqttClient.subscribe("sitg7/room1/ldr");
    mqttClient.subscribe("sitg7/room1/crowd");
    mqttClient.subscribe("sitg7/room1/location");
    mqttClient.subscribe("sitg7/room1/cmd/#");
    return true;
  }
  Serial.print("MQTT failed, error ");
  Serial.println(mqttClient.connectError());
  return false;
}

void maintainNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);
    if (mqttClient.connected()) { mqttClient.stop(); }
    if (!wifiTried || millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
      wifiTried = true;
      Serial.println("Connecting WiFi...");
      WiFi.begin(ssid, pass);
      lastWiFiAttempt = millis();
      if (WiFi.status() == WL_CONNECTED) { Serial.println("WiFi connected"); }
    }
    return;
  }

  if (!mqttClient.connected()) {
    digitalWrite(LED_BUILTIN, LOW);
    if (!mqttTried || millis() - lastMQTTAttempt >= MQTT_RETRY_INTERVAL) {
      mqttTried = true;
      connectMQTT();
    }
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    mqttClient.poll();
  }
}

bool isFresh(bool got, unsigned long lastTime) {
  return got && (millis() - lastTime < DATA_TIMEOUT);
}

void checkSensorData() {
  bool dhtNow = isFresh(gotTemp, lastTempMsg) && isFresh(gotHum, lastHumMsg);
  if (dhtNow != dhtValid) {
    dhtValid = dhtNow;
    if (dhtValid) { Serial.println("Board 1 temperature + humidity: OK"); }
    else { Serial.println("Board 1 temperature + humidity: LOST"); }
  }

  bool roomNow = isFresh(gotOccupancy, lastOccupancyMsg) && isFresh(gotLdr, lastLdrMsg);
  if (roomNow != roomValid) {
    roomValid = roomNow;
    if (roomValid) { Serial.println("Board 1 PIR + light data: OK"); }
    else { Serial.println("Board 1 PIR + light data: LOST (LED forced OFF)"); }
  }
}

bool dataOk() {
  return simulationMode || roomValid;
}

void updateLighting() {
  if (simulationMode) {
    occupied = simOccupied;
    isDark = simDark;
  }
  else if (roomValid) {

    if (reportedOccupied) {
      occupied = true;
      lastOccupiedMs = millis();
      haveOccupied = true;
    } else if (haveOccupied && millis() - lastOccupiedMs < OFF_DELAY_MS) {
      occupied = true;
    } else {
      occupied = false;
    }

    if (isDark && ldrValue >= BRIGHT_THRESHOLD) {
      isDark = false;
      Serial.println("AMBIENT: BRIGHT");
    }
    else if (!isDark && ldrValue <= DARK_THRESHOLD) {
      isDark = true;
      Serial.println("AMBIENT: DARK");
    }
  } else {
    occupied = false;
  }

  bool autoLight = dataOk() && occupied && isDark;
  bool newLightingState = manualLight ? manualLightOn : autoLight;
  if (newLightingState != lightingOn) {
    lightingOn = newLightingState;
    Serial.print("LIGHTING: ");
    if (lightingOn) { Serial.println("ON"); } else { Serial.println("OFF"); }
  }

  if (lightingOn) { analogWrite(LED_PIN, map(lightBrightness, 0, 100, 0, 255)); }
  else { analogWrite(LED_PIN, 0); }
}

String calculateHVAC(float temp, float hum, int crowd) {
  bool temperatureHigh = (temp >= TEMP_HIGH_THRESHOLD);
  bool humidityHigh = (hum >= HUM_HIGH_THRESHOLD);
  bool environmentHigh = temperatureHigh || humidityHigh;

  if (crowd == 0) { return "OFF"; }

  if (!environmentHigh) {
    if (crowd == 1) { return "LOW"; }
    if (crowd == 2) { return "LOW-MEDIUM"; }
    if (crowd == 3) { return "MEDIUM"; }
  }

  if (crowd == 3 && temperatureHigh && !humidityHigh) { return "MEDIUM-HIGH"; }

  if (crowd == 1) { return "MEDIUM"; }
  if (crowd == 2) { return "HIGH"; }
  if (crowd == 3) { return "HIGH"; }

  return "OFF";
}

void updateHVAC() {

  if (simulationMode) {
    if (selectedScenario == "NORMAL")         { effectiveTemperature = 25.0; effectiveHumidity = 55.0; }
    else if (selectedScenario == "HOT")       { effectiveTemperature = 33.0; effectiveHumidity = 55.0; }
    else if (selectedScenario == "HUMID")     { effectiveTemperature = 25.0; effectiveHumidity = 85.0; }
    else if (selectedScenario == "HOT_HUMID") { effectiveTemperature = 33.0; effectiveHumidity = 85.0; }
    effectiveDataValid = true;
  }
  else if (dhtValid) {
    effectiveTemperature = actualTemperature;
    effectiveHumidity = actualHumidity;
    effectiveDataValid = true;
  }
  else {

    effectiveDataValid = false;
    hvacDetailed = "SENSOR_FAULT";
    hvacStandard = "SENSOR_FAULT";
    return;
  }

  hvacDetailed = calculateHVAC(effectiveTemperature, effectiveHumidity, hvacCrowd());

  if (hvacDetailed == "LOW-MEDIUM") { hvacStandard = "MEDIUM"; }
  else if (hvacDetailed == "MEDIUM-HIGH") { hvacStandard = "HIGH"; }
  else { hvacStandard = hvacDetailed; }
}

int hvacCrowd() {
  if (!dataOk() || !occupied) { return 0; }
  return crowdLevel < 1 ? 1 : crowdLevel;
}

String fanLevel() {
  if (!dataOk() || !occupied || !effectiveDataValid) { return "OFF"; }
  return calculateHVAC(effectiveTemperature, effectiveHumidity, hvacCrowd());
}

void updateFan(unsigned long now) {
  String level = fanLevel();
  int target = 0;
  if (manualFan)                        { target = manualFanPct; }
  else if (level == "LOW")              { target = FAN_LOW; }
  else if (level == "LOW-MEDIUM")       { target = FAN_LOW_MEDIUM; }
  else if (level == "MEDIUM")           { target = FAN_MEDIUM; }
  else if (level == "MEDIUM-HIGH")      { target = FAN_MEDIUM_HIGH; }
  else if (level == "HIGH")             { target = FAN_HIGH; }

  if (fanPercent == 0 && target > 0) {
    fanKicking = true;
    fanKickStart = now;
  }
  if (fanKicking && now - fanKickStart >= FAN_KICK_MS) { fanKicking = false; }
  if (target == 0) { fanKicking = false; }

  if (target != fanPercent) {
    fanPercent = target;
    Serial.print("FAN: ");
    Serial.print(fanPercent);
    Serial.println("%");
  }

  int output = map(fanPercent, 0, 100, 0, 255);
  if (fanKicking) { output = 255; }

  if (output != fanOutput) {
    fanOutput = output;
    analogWrite(FAN_PIN, fanOutput);
  }
}

void publishValue(const char* topic, String value) {
  mqttClient.beginMessage(topic);
  mqttClient.print(value);
  mqttClient.endMessage();
}

String numberOrError(float value, bool valid) {
  if (valid) { return String(value, 1); }
  return "ERROR";
}

void publishNextMessage() {
  if (publishStep == 0) { return; }
  if (!mqttClient.connected()) { publishStep = 0; return; }

  switch (publishStep) {

    case 1:  publishValue("sitg7/room1/energy/hvac", hvacStandard); break;
    case 2:  if (lightingOn) { publishValue("sitg7/room1/energy/lighting", "ON"); }
             else { publishValue("sitg7/room1/energy/lighting", "OFF"); }
             break;
    case 3:  if (isDark) { publishValue("sitg7/room1/energy/ambient", "DARK"); }
             else { publishValue("sitg7/room1/energy/ambient", "BRIGHT"); }
             break;
    case 4:  publishValue("sitg7/room1/energy/fan", String(fanPercent)); break;

    case 5:  publishValue("sitg7/room1/energy/hvac_detail", hvacDetailed); break;
    case 6:  publishValue("sitg7/room1/energy/actual_temp", numberOrError(actualTemperature, dhtValid)); break;
    case 7:  publishValue("sitg7/room1/energy/actual_hum", numberOrError(actualHumidity, dhtValid)); break;
    case 8:  publishValue("sitg7/room1/energy/temp", numberOrError(effectiveTemperature, effectiveDataValid)); break;
    case 9:  publishValue("sitg7/room1/energy/hum", numberOrError(effectiveHumidity, effectiveDataValid)); break;
    case 10: publishValue("sitg7/room1/energy/crowd", String(crowdLevel)); break;
    case 11: if (occupied) { publishValue("sitg7/room1/energy/occupancy", "OCCUPIED"); }
             else { publishValue("sitg7/room1/energy/occupancy", "VACANT"); }
             break;
    case 12: if (simulationMode) { publishValue("sitg7/room1/energy/mode", "SIMULATION"); }
             else { publishValue("sitg7/room1/energy/mode", "LIVE"); }
             break;
    case 13: publishValue("sitg7/room1/energy/scenario", selectedScenario); break;
    case 14: if (dhtValid) { publishValue("sitg7/room1/energy/dht_status", "NORMAL"); }
             else { publishValue("sitg7/room1/energy/dht_status", "FAULT"); }
             break;
    case 15: if (dhtValid && roomValid) { publishValue("sitg7/room1/energy/link", "OK"); }
             else { publishValue("sitg7/room1/energy/link", "LOST"); }
             break;

    case 16: publishValue("sitg7/room1/hvac", hvacStandard); break;
    case 17: publishValue("sitg7/room1/energy/brightness", String(lightingOn ? lightBrightness : 0)); break;
  }

  publishStep = publishStep + 1;
  if (publishStep > 17) { publishStep = 0; }
}

void printStatus() {
  Serial.print("T=");        Serial.print(numberOrError(effectiveTemperature, effectiveDataValid));
  Serial.print(" H=");       Serial.print(numberOrError(effectiveHumidity, effectiveDataValid));
  Serial.print(" Crowd=");   Serial.print(crowdLevel);
  Serial.print(" HVAC=");    Serial.print(hvacDetailed);
  Serial.print(" Fan=");     Serial.print(fanPercent);
  Serial.print("% | ");
  if (!roomValid) { Serial.print("NO-ROOM-DATA"); }
  else if (occupied) { Serial.print("OCCUPIED"); }
  else { Serial.print("VACANT"); }
  Serial.print(" LDR=");     Serial.print(ldrValue);
  if (isDark) { Serial.print(" DARK"); } else { Serial.print(" BRIGHT"); }
  if (lightingOn) { Serial.print(" Light=ON"); } else { Serial.print(" Light=OFF"); }
  Serial.print(" | ");
  if (simulationMode) { Serial.print("SIM "); Serial.print(selectedScenario); }
  else { Serial.print("LIVE"); }
  if (mqttClient.connected()) { Serial.println(" | MQTT=OK"); }
  else { Serial.println(" | MQTT=--"); }
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(LED_BUILTIN, LOW);

  mqttClient.setId(CLIENT_ID);
  mqttClient.setConnectionTimeout(5000);
  mqttClient.onMessage(onMqttMessage);
  WiFi.setTimeout(8000);

  Serial.println("BOARD 2 - ENERGY SAVING MODULE"); 
}

void loop() {
  maintainNetwork();

  unsigned long now = millis();

  checkSensorData();
  updateLighting();
  updateHVAC();
  updateFan(now);

  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    publishStep = 1;
    printStatus();
  }
  publishNextMessage();

  delay(20);
}
