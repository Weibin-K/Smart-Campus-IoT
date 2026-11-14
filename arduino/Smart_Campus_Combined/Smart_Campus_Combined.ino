// =====================================================
// SMART CAMPUS ROOM 1 - COMBINED SKETCH (SIT Group 7)
// Board: Arduino Uno WiFi Rev2
//
// Combines:
//   Temp_Humidity_LCD          RGB LCD + DHT11 + MQTT
//   Smart_Energy_System_MQTT   PIR + LDR + LED + HVAC logic + phone commands
//   NEW                        fan speed follows the HVAC level
//   NEW                        Google Sheets logging (Apps Script web app)
//
// PIN MAP
//   D2       PIR output
//   D3       DHT11 data     (moved from A0, because the LDR uses A0)
//   D5       LED            (through a resistor)
//   D6       Fan signal     (PWM, into a transistor / fan module, NOT straight into the fan)
//   A0       LDR divider
//   SDA/SCL  RGB LCD        (I2C)
//
// LIBRARIES (Library Manager)
//   WiFiNINA, ArduinoMqttClient, DHT sensor library (Adafruit),
//   Adafruit Unified Sensor, DFRobot_RGBLCD1602
// =====================================================

#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <Wire.h>
#include <DHT.h>
#include "DFRobot_RGBLCD1602.h"

// =====================================================
// 1. SETTINGS (edit these)
// =====================================================

char ssid[] = "weibin";
char pass[] = "31415926";

const char BROKER[] = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char CLIENT_ID[] = "sitg7-room1-uno01";   // every device on the broker needs its own ID

// Google Sheets: paste ONLY the long Deployment ID of your Apps Script web app.
// While this still says PASTE_ID_HERE, Sheets logging is skipped.
const char SHEET_ID[] = "PASTE_ID_HERE";
const unsigned long SHEET_INTERVAL = 30000UL;   // one row every 30 s

const bool HVAC_COLOURS = true;        // false = LCD stays green instead of HVAC colours
const bool PUBLISH_OLD_TOPICS = true;  // also send sitg7/room1/temp, hum, crowd, hvac (old LCD sketch topics)

// =====================================================
// 2. PINS
// =====================================================

const int PIR_PIN = 2;
const int DHT_PIN = 3;
const int LED_PIN = 5;
const int FAN_PIN = 6;
const int LDR_PIN = A0;

// =====================================================
// 3. THRESHOLDS AND TIMING
// =====================================================

// Lighting (raw LDR reading). Between the two values the old state is kept.
const int DARK_THRESHOLD = 8;
const int BRIGHT_THRESHOLD = 16;

// HVAC (provisional, from the Energy handover)
const float TEMP_HIGH_THRESHOLD = 31.0;
const float HUM_HIGH_THRESHOLD = 75.0;

// Fan speed in percent for each HVAC level
const int FAN_LOW = 40;
const int FAN_LOW_MEDIUM = 55;
const int FAN_MEDIUM = 70;
const int FAN_MEDIUM_HIGH = 85;
const int FAN_HIGH = 100;
const unsigned long FAN_KICK_MS = 300UL;        // full power for a moment so a stopped fan spins up

const unsigned long VACANCY_TIMEOUT = 3000UL;   // after the PIR goes LOW (PIR itself holds about 10 s)
const unsigned long PIR_WARMUP_MS = 60000UL;
const unsigned long DHT_INTERVAL = 2000UL;
const unsigned long PUBLISH_INTERVAL = 2000UL;
const unsigned long LCD_INTERVAL = 500UL;
const unsigned long PAGE_TIME = 3000UL;         // each LCD page shows for 3 s
const unsigned long WIFI_RETRY_INTERVAL = 15000UL;
const unsigned long MQTT_RETRY_INTERVAL = 10000UL;
const unsigned long SHEET_TIMEOUT = 8000UL;

// =====================================================
// 4. OBJECTS
// =====================================================

DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);
DHT dht(DHT_PIN, DHT11);
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
WiFiSSLClient sheetClient;

// =====================================================
// 5. VARIABLES
// =====================================================

// Lighting
bool occupied = false;
bool isDark = false;
bool lightingOn = false;
bool pirReady = false;
int ldrValue = 0;
unsigned long pirWarmupStart = 0;
unsigned long lastMotionTime = 0;

// DHT11
float actualTemperature = NAN;
float actualHumidity = NAN;
bool dhtValid = false;
unsigned long lastDHTReadTime = 0;

// Phone controls
bool simulationMode = false;            // default LIVE
String selectedScenario = "NORMAL";
int crowdLevel = 0;                     // 0 none, 1 low, 2 medium, 3 high  (ONE shared variable)

// HVAC
float effectiveTemperature = NAN;       // value the HVAC logic uses (live or simulated)
float effectiveHumidity = NAN;
bool effectiveDataValid = false;
String hvacDetailed = "SENSOR_FAULT";   // OFF / LOW / LOW-MEDIUM / MEDIUM / MEDIUM-HIGH / HIGH
String hvacStandard = "SENSOR_FAULT";   // OFF / LOW / MEDIUM / HIGH

// Fan
int fanPercent = 0;                     // speed the HVAC level asks for
int fanOutput = -1;                     // what is on the pin right now (0-255)
bool fanKicking = false;
unsigned long fanKickStart = 0;

// Timers
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastPublish = 0;
unsigned long lastLCD = 0;
unsigned long lastSheetLog = 0;
unsigned long sheetStart = 0;
bool sheetWaiting = false;

int publishStep = 0;                    // 0 = idle, 1-18 = which message goes out next
String lcdShown[2] = { "", "" };        // what each LCD row shows now
float breath = 0;                       // used by the start-up animations

// =====================================================
// 6. LCD COLOUR HELPERS (from Temp_Humidity_LCD)
// =====================================================

// hue 0-360 -> r, g, b (full saturation)
void hueToRGB(float h, int &r, int &g, int &b) {
  while (h >= 360) { h = h - 360; }
  float x = 1 - fabs(fmod(h / 60.0, 2) - 1);
  float rf = 0, gf = 0, bf = 0;
  if (h < 60)       { rf = 1; gf = x; }
  else if (h < 120) { rf = x; gf = 1; }
  else if (h < 180) { gf = 1; bf = x; }
  else if (h < 240) { gf = x; bf = 1; }
  else if (h < 300) { rf = x; bf = 1; }
  else              { rf = 1; bf = x; }
  r = rf * 255; g = gf * 255; b = bf * 255;
}

void setScaled(int r, int g, int b, float bright) {
  lcd.setRGB(r * bright, g * bright, b * bright);
}

// slow colour rotation + breathing, for `ms` milliseconds (start-up only)
void connectingGlow(unsigned long ms) {
  static float hue = 0;
  unsigned long start = millis();
  while (millis() - start < ms) {
    int r, g, b;
    hueToRGB(hue, r, g, b);
    float level = (sin(breath) + 1) / 2;
    setScaled(r, g, b, 0.15 + 0.35 * level);
    hue = hue + 0.6;
    breath = breath + 0.05;
    delay(30);
  }
}

// fast rainbow at medium brightness (start-up only)
void rainbowBurst(unsigned long ms) {
  float hue = 0;
  unsigned long start = millis();
  while (millis() - start < ms) {
    int r, g, b;
    hueToRGB(hue, r, g, b);
    setScaled(r, g, b, 0.5);
    hue = hue + 12;
    delay(15);
  }
}

// green breathing, for `ms` milliseconds (start-up only)
void greenBreathe(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    float level = (sin(breath) + 1) / 2;
    setScaled(0, 255, 60, 0.2 + 0.4 * level);
    breath = breath + 0.05;
    delay(30);
  }
}

// write one LCD row, padded to 16 characters, only if the text changed
void lcdLine(int row, String text) {
  while (text.length() < 16) { text = text + " "; }
  if (text == lcdShown[row]) { return; }
  lcdShown[row] = text;
  lcd.setCursor(0, row);
  lcd.print(text);
}

// =====================================================
// 7. MQTT COMMANDS FROM THE PHONE
//    sitg7/room1/cmd/mode      LIVE / SIMULATION
//    sitg7/room1/cmd/scenario  NORMAL / HOT / HUMID / HOT_HUMID
//    sitg7/room1/cmd/crowd     0 / 1 / 2 / 3
// =====================================================

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

  Serial.print("MQTT in: ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(value);

  if (topic.endsWith("/cmd/mode")) {
    if (value == "LIVE") { simulationMode = false; }
    else if (value == "SIMULATION") { simulationMode = true; }
    else { Serial.println("ERROR: invalid mode"); }
  }
  else if (topic.endsWith("/cmd/scenario")) {
    if (value == "NORMAL" || value == "HOT" || value == "HUMID" || value == "HOT_HUMID") {
      selectedScenario = value;
    } else {
      Serial.println("ERROR: invalid scenario");
    }
  }
  else if (topic.endsWith("/cmd/crowd")) {
    if (value == "0" || value == "1" || value == "2" || value == "3") {
      crowdLevel = value.toInt();
    } else {
      Serial.println("ERROR: invalid crowd level");
    }
  }
}

// crowd level typed into Serial Monitor (0-3), handy without the phone
void readSerialCrowd() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c >= '0' && c <= '3') {
      crowdLevel = c - '0';
      Serial.print("Crowd level set from Serial: ");
      Serial.println(crowdLevel);
    }
  }
}

// =====================================================
// 8. WIFI + MQTT CONNECTION
// =====================================================

bool connectMQTT() {
  Serial.println("Connecting to HiveMQ...");
  bool ok = mqttClient.connect(BROKER, MQTT_PORT);    // can pause for several seconds if it fails
  lastMQTTAttempt = millis();                         // timed from the END of the try, so the
                                                      // sensors always get to run between tries
  if (ok) {
    Serial.println("MQTT connected");
    mqttClient.subscribe("sitg7/room1/cmd/#");
    return true;
  }
  Serial.print("MQTT failed, error ");
  Serial.println(mqttClient.connectError());
  return false;
}

// start-up only: animated connect. Gives up after 3 tries so the room
// still works offline (lights, fan, LCD), then loop() keeps retrying.
void startupConnect() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 3) {
    connectingGlow(1000);
    WiFi.begin(ssid, pass);                 // board pauses here for a few s
    tries = tries + 1;
  }
  lastWiFiAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi yet, running offline");
    lcd.clear();
    lcd.print("No WiFi: offline");
    connectingGlow(2000);
    lcd.clear();
    return;
  }

  Serial.println("WiFi connected");
  lcd.clear();
  lcd.print("WiFi connected!");
  rainbowBurst(2000);

  lcd.clear();
  lcd.print("Connecting MQTT");
  greenBreathe(500);
  lcd.clear();
  if (connectMQTT()) { lcd.print("All connected :)"); }
  else { lcd.print("MQTT retry later"); }
  greenBreathe(2000);
  lcd.clear();
}

// called every loop pass: reconnects when needed, without stopping the sensors for long
void maintainNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) { mqttClient.stop(); }
    if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.begin(ssid, pass);
      lastWiFiAttempt = millis();
    }
    return;
  }

  if (!mqttClient.connected()) {
    if (millis() - lastMQTTAttempt >= MQTT_RETRY_INTERVAL) { connectMQTT(); }
  } else {
    mqttClient.poll();                      // receives phone commands
  }
}

// =====================================================
// 9. DHT11
// =====================================================

void readDHT(unsigned long now) {
  if (now - lastDHTReadTime < DHT_INTERVAL) { return; }
  lastDHTReadTime = now;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    if (dhtValid) { Serial.println("DHT11 sensor fault detected!"); }
    dhtValid = false;
  } else {
    actualTemperature = t;
    actualHumidity = h;
    dhtValid = true;
  }
}

// =====================================================
// 10. HVAC DECISION (project decision table, from the Energy sketch)
// =====================================================

String calculateHVAC(float temp, float hum, int crowd) {
  bool temperatureHigh = (temp >= TEMP_HIGH_THRESHOLD);
  bool humidityHigh = (hum >= HUM_HIGH_THRESHOLD);
  bool environmentHigh = temperatureHigh || humidityHigh;

  if (crowd == 0) { return "OFF"; }

  // normal environment
  if (!environmentHigh) {
    if (crowd == 1) { return "LOW"; }
    if (crowd == 2) { return "LOW-MEDIUM"; }
    if (crowd == 3) { return "MEDIUM"; }
  }

  // special case: high crowd + high temperature + normal humidity
  if (crowd == 3 && temperatureHigh && !humidityHigh) { return "MEDIUM-HIGH"; }

  // high environment
  if (crowd == 1) { return "MEDIUM"; }
  if (crowd == 2) { return "HIGH"; }
  if (crowd == 3) { return "HIGH"; }

  return "OFF";
}

void updateHVAC() {
  // pick simulated or real readings
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

  // ---- CROWD INTEGRATION POINT ----
  // When the real crowd counter is ready, set crowdLevel (0-3) from it
  // just above this line. Everything below already uses crowdLevel.

  hvacDetailed = calculateHVAC(effectiveTemperature, effectiveHumidity, crowdLevel);

  // four-level version for dashboards that only know OFF / LOW / MEDIUM / HIGH
  if (hvacDetailed == "LOW-MEDIUM") { hvacStandard = "MEDIUM"; }
  else if (hvacDetailed == "MEDIUM-HIGH") { hvacStandard = "HIGH"; }
  else { hvacStandard = hvacDetailed; }
}

// =====================================================
// 11. FAN (shows the HVAC level physically)
// =====================================================

void updateFan(unsigned long now) {
  int target = 0;                                   // OFF or SENSOR_FAULT -> stopped
  if (hvacDetailed == "LOW")              { target = FAN_LOW; }
  else if (hvacDetailed == "LOW-MEDIUM")  { target = FAN_LOW_MEDIUM; }
  else if (hvacDetailed == "MEDIUM")      { target = FAN_MEDIUM; }
  else if (hvacDetailed == "MEDIUM-HIGH") { target = FAN_MEDIUM_HIGH; }
  else if (hvacDetailed == "HIGH")        { target = FAN_HIGH; }

  // starting from a stop: short full-power kick
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

  if (output != fanOutput) {                        // only touch the pin when it changes
    fanOutput = output;
    analogWrite(FAN_PIN, fanOutput);
  }
}

// =====================================================
// 12. SMART LIGHTING (PIR + LDR -> LED, from the Energy sketch)
//     LED ON only when the room is occupied AND dark
// =====================================================

void updateLighting(unsigned long now) {
  // PIR warm-up
  if (!pirReady && now - pirWarmupStart >= PIR_WARMUP_MS) {
    pirReady = true;
    Serial.println("PIR sensor ready!");
  }

  // occupancy + vacancy timer
  if (pirReady) {
    if (digitalRead(PIR_PIN) == HIGH) {
      lastMotionTime = now;
      if (!occupied) {
        occupied = true;
        Serial.println("ROOM OCCUPIED");
      }
    }
    else if (occupied && now - lastMotionTime >= VACANCY_TIMEOUT) {
      occupied = false;
      Serial.println("ROOM VACANT");
    }
  }

  // brightness with hysteresis
  ldrValue = analogRead(LDR_PIN);
  if (isDark && ldrValue >= BRIGHT_THRESHOLD) {
    isDark = false;
    Serial.println("AMBIENT: BRIGHT");
  }
  else if (!isDark && ldrValue <= DARK_THRESHOLD) {
    isDark = true;
    Serial.println("AMBIENT: DARK");
  }

  // decision
  bool newLightingState = occupied && isDark;
  if (newLightingState != lightingOn) {
    lightingOn = newLightingState;
    Serial.print("LIGHTING: ");
    if (lightingOn) { Serial.println("ON"); } else { Serial.println("OFF"); }
  }

  if (lightingOn) { digitalWrite(LED_PIN, HIGH); }
  else { digitalWrite(LED_PIN, LOW); }
}

// =====================================================
// 13. MQTT PUBLISHING
//     One message per loop pass, so the 18 messages go out over
//     about a second without freezing the LCD or the sensors.
// =====================================================

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
  if (publishStep == 0) { return; }                         // nothing to send right now
  if (!mqttClient.connected()) { publishStep = 0; return; }

  switch (publishStep) {
    // Energy dashboard topics (same as Smart_Energy_System_MQTT)
    case 1:  publishValue("sitg7/room1/energy/actual_temp", numberOrError(actualTemperature, dhtValid)); break;
    case 2:  publishValue("sitg7/room1/energy/actual_hum", numberOrError(actualHumidity, dhtValid)); break;
    case 3:  publishValue("sitg7/room1/energy/temp", numberOrError(effectiveTemperature, effectiveDataValid)); break;
    case 4:  publishValue("sitg7/room1/energy/hum", numberOrError(effectiveHumidity, effectiveDataValid)); break;
    case 5:  publishValue("sitg7/room1/energy/crowd", String(crowdLevel)); break;
    case 6:  publishValue("sitg7/room1/energy/hvac", hvacStandard); break;
    case 7:  publishValue("sitg7/room1/energy/hvac_detail", hvacDetailed); break;
    case 8:  if (lightingOn) { publishValue("sitg7/room1/energy/lighting", "ON"); }
             else { publishValue("sitg7/room1/energy/lighting", "OFF"); }
             break;
    case 9:  if (occupied) { publishValue("sitg7/room1/energy/occupancy", "OCCUPIED"); }
             else { publishValue("sitg7/room1/energy/occupancy", "VACANT"); }
             break;
    case 10: if (isDark) { publishValue("sitg7/room1/energy/ambient", "DARK"); }
             else { publishValue("sitg7/room1/energy/ambient", "BRIGHT"); }
             break;
    case 11: if (simulationMode) { publishValue("sitg7/room1/energy/mode", "SIMULATION"); }
             else { publishValue("sitg7/room1/energy/mode", "LIVE"); }
             break;
    case 12: publishValue("sitg7/room1/energy/scenario", selectedScenario); break;
    case 13: if (dhtValid) { publishValue("sitg7/room1/energy/dht_status", "NORMAL"); }
             else { publishValue("sitg7/room1/energy/dht_status", "FAULT"); }
             break;
    case 14: publishValue("sitg7/room1/energy/fan", String(fanPercent)); break;       // NEW: fan speed in %

    // Old topics from Temp_Humidity_LCD (for the app)
    case 15: publishValue("sitg7/room1/temp", numberOrError(effectiveTemperature, effectiveDataValid)); break;
    case 16: publishValue("sitg7/room1/hum", numberOrError(effectiveHumidity, effectiveDataValid)); break;
    case 17: publishValue("sitg7/room1/crowd", String(crowdLevel)); break;
    case 18: publishValue("sitg7/room1/hvac", hvacStandard); break;
  }

  publishStep = publishStep + 1;
  if (publishStep > 18) { publishStep = 0; }
  if (publishStep > 14 && !PUBLISH_OLD_TOPICS) { publishStep = 0; }
}

// =====================================================
// 14. GOOGLE SHEETS LOGGING
//     Sends one HTTPS GET to the Apps Script web app. The script
//     adds a row. The reply is checked later so loop() keeps running.
// =====================================================

void startSheetLog() {
  if (strcmp(SHEET_ID, "PASTE_ID_HERE") == 0) { return; }   // not set up yet
  if (WiFi.status() != WL_CONNECTED) { return; }

  sheetClient.stop();
  if (!sheetClient.connect("script.google.com", 443)) {     // pauses 1-3 s here
    Serial.println("Sheet: could not connect");
    return;
  }

  String request = "";
  request.reserve(360);
  request += "GET /macros/s/";
  request += SHEET_ID;
  request += "/exec?temp=";
  request += numberOrError(effectiveTemperature, effectiveDataValid);
  request += "&hum=";
  request += numberOrError(effectiveHumidity, effectiveDataValid);
  request += "&crowd=";
  request += String(crowdLevel);
  request += "&hvac=";
  request += hvacDetailed;
  request += "&fan=";
  request += String(fanPercent);
  if (lightingOn) { request += "&light=ON"; } else { request += "&light=OFF"; }
  if (occupied) { request += "&occ=OCCUPIED"; } else { request += "&occ=VACANT"; }
  if (isDark) { request += "&ambient=DARK"; } else { request += "&ambient=BRIGHT"; }
  if (simulationMode) { request += "&mode=SIMULATION"; } else { request += "&mode=LIVE"; }
  request += " HTTP/1.1\r\n";
  request += "Host: script.google.com\r\n";
  request += "User-Agent: Arduino\r\n";
  request += "Connection: close\r\n\r\n";

  sheetClient.print(request);
  sheetWaiting = true;
  sheetStart = millis();
}

void checkSheetReply() {
  if (!sheetWaiting) { return; }

  if (sheetClient.available()) {
    String status = sheetClient.readStringUntil('\n');      // e.g. HTTP/1.1 302 Moved Temporarily
    status.trim();
    if (status.indexOf(" 302") > 0 || status.indexOf(" 200") > 0) {
      Serial.println("Sheet: row logged");                  // 302 is normal for Apps Script
    } else {
      Serial.print("Sheet: unexpected reply: ");
      Serial.println(status);
    }
    sheetClient.stop();
    sheetWaiting = false;
  }
  else if (millis() - sheetStart >= SHEET_TIMEOUT) {
    Serial.println("Sheet: no reply");
    sheetClient.stop();
    sheetWaiting = false;
  }
}

// =====================================================
// 15. LCD TEXT (3 pages, 3 s each) + BACKLIGHT
// =====================================================

void updateLCD(unsigned long now) {
  if (now - lastLCD < LCD_INTERVAL) { return; }
  lastLCD = now;

  int page = (now / PAGE_TIME) % 3;
  String top = "";
  String bottom = "";

  if (page == 0) {
    // readings + HVAC          e.g.  T:30C H:75% C:2  /  HVAC:MEDIUM-HIGH
    if (effectiveDataValid) {
      top = "T:" + String(effectiveTemperature, 0) + "C H:" + String(effectiveHumidity, 0) + "% C:" + String(crowdLevel);
      bottom = "HVAC:" + hvacDetailed;
    } else {
      top = "T:--C H:--% C:" + String(crowdLevel);
      bottom = "HVAC:DHT FAULT";
    }
  }
  else if (page == 1) {
    // room + lighting          e.g.  Room:OCCUPIED  /  Light:ON  DARK
    if (!pirReady) { top = "Room:PIR WARMUP"; }
    else if (occupied) { top = "Room:OCCUPIED"; }
    else { top = "Room:VACANT"; }

    if (lightingOn) { bottom = "Light:ON  "; } else { bottom = "Light:OFF "; }
    if (isDark) { bottom = bottom + "DARK"; } else { bottom = bottom + "BRIGHT"; }
  }
  else {
    // network + mode           e.g.  WiFi:OK  MQTT:OK  /  Sim:HOT_HUMID
    if (WiFi.status() == WL_CONNECTED) { top = "WiFi:OK  "; } else { top = "WiFi:--  "; }
    if (mqttClient.connected()) { top = top + "MQTT:OK"; } else { top = top + "MQTT:--"; }

    if (simulationMode) { bottom = "Sim:" + selectedScenario; }
    else { bottom = "Mode:LIVE"; }
  }

  lcdLine(0, top);
  lcdLine(1, bottom);
}

// gentle breathing on top of the HVAC colour (timed with millis, so it stays smooth)
void updateBacklight() {
  float angle = (millis() % 4712UL) / 750.0;          // one breath is about 4.7 s
  float level = (sin(angle) + 1) / 2;
  float bright = 0.25 + 0.35 * level;

  if (!HVAC_COLOURS) {
    setScaled(0, 255, 60, bright);                    // always green
  } else if (hvacStandard == "OFF") {
    setScaled(120, 120, 120, bright * 0.6);           // dim grey
  } else if (hvacStandard == "LOW") {
    setScaled(0, 255, 60, bright);                    // green
  } else if (hvacStandard == "MEDIUM") {
    setScaled(255, 120, 0, bright);                   // orange
  } else if (hvacStandard == "HIGH") {
    setScaled(255, 0, 0, bright);                     // red
  } else {
    setScaled(150, 0, 255, bright);                   // purple = DHT11 fault
  }
}

// =====================================================
// 16. SERIAL MONITOR STATUS (one line every 2 s)
// =====================================================

void printStatus() {
  Serial.print("T=");        Serial.print(numberOrError(effectiveTemperature, effectiveDataValid));
  Serial.print(" H=");       Serial.print(numberOrError(effectiveHumidity, effectiveDataValid));
  Serial.print(" Crowd=");   Serial.print(crowdLevel);
  Serial.print(" HVAC=");    Serial.print(hvacDetailed);
  Serial.print(" Fan=");     Serial.print(fanPercent);
  Serial.print("% | ");
  if (!pirReady) { Serial.print("PIR-WARMUP"); }
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

// =====================================================
// 17. SETUP
// =====================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);

  dht.begin();
  lcd.init();

  mqttClient.setId(CLIENT_ID);
  mqttClient.setConnectionTimeout(5000);
  mqttClient.onMessage(onMqttMessage);
  WiFi.setTimeout(8000);                    // WiFi.begin gives up after 8 s instead of 50 s

  isDark = (analogRead(LDR_PIN) <= DARK_THRESHOLD);
  pirWarmupStart = millis();                // PIR warms up while WiFi connects

  Serial.println("SMART CAMPUS ROOM 1 - COMBINED");
  Serial.println("Type 0-3 here to set the crowd level");

  startupConnect();
}

// =====================================================
// 18. MAIN LOOP
// =====================================================

void loop() {
  maintainNetwork();          // WiFi / MQTT reconnect + phone commands (can pause while reconnecting)

  unsigned long now = millis();   // read the clock after that, so the timers below are right

  readSerialCrowd();          // crowd level from Serial Monitor
  readDHT(now);               // temperature + humidity every 2 s
  updateLighting(now);        // PIR + LDR -> LED
  updateHVAC();               // temp + hum + crowd -> HVAC level
  updateFan(now);             // HVAC level -> fan speed

  // every 2 s: start a new round of MQTT messages
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    publishStep = 1;
    printStatus();
  }
  publishNextMessage();

  // every 30 s: one row to Google Sheets
  if (now - lastSheetLog >= SHEET_INTERVAL) {
    lastSheetLog = now;
    startSheetLog();
  }
  checkSheetReply();

  updateLCD(now);
  updateBacklight();

  delay(20);
}
