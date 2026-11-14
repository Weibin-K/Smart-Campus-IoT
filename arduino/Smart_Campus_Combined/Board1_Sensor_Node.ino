// BOARD 1 - SENSOR NODE (Temp_Hum_Sound + PIR + light sensor). Added parts are marked NEW.
// This board only senses and reports. Board 2 (Energy Saving module) decides the HVAC level,
// the LED and the fan, and reports them back over MQTT for the LCD and the Google Sheet.
// PINS: A0 DHT11 | A1 sound | A2 LDR | D2 PIR | D4 button | SDA/SCL LCD

#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include "DFRobot_RGBLCD1602.h"
#include "dht.h"

#define dht_apin A0
#define sound_pin A1
#define ldr_pin A2            // NEW: light sensor (LDR divider)
#define pir_pin 2             // NEW: PIR output
#define button_pin 4

// ================= SETTINGS =================
const char ssid[] = "weibin";
const char pass[] = "31415926";

const char broker[] = "broker.hivemq.com";
const int  port = 1883;
const char MQTT_ID[] = "sitg7-weibin";
const char TOPIC_BASE[] = "sitg7/room1/";

const char GAS_HOST[] = "script.google.com";
const char DEPLOY_ID[] = "AKfycbzlVsZVIWkLt818jMaaC5FyiXxicgMHUnufDmqna2tomFhcsaretr3Tu6svYqhBWqiJOQ";
const char API_KEY[] = "env-g7-8f3k2q";      // must match ENV_API_KEY in Script Properties

const unsigned long SEND_INTERVAL  = 2000;   // sensors + MQTT
const unsigned long SHEET_INTERVAL = 15000;  // Google Sheet
const unsigned long SELECT_TIMEOUT = 5000;   // location menu auto-confirm

const float TEMP_OFFSET = 0.0;               // calibration
const float HUM_OFFSET  = 0.0;
// NEW: PIR
const unsigned long VACANCY_TIMEOUT = 3000;  // room counts as empty this long after the PIR goes LOW
const unsigned long PIR_WARMUP = 60000;      // PIR is ignored for the first 60 s after power-on

const unsigned long LINK_TIMEOUT = 10000;    // NEW: nothing from Board 2 for this long = no link
const int PAGE_TICKS = 2;                    // NEW: LCD swaps page every 2 readings (4 s)

// 0 = follows real temperature, 1 = HVAC colours, 2 = always green
const int BACKLIGHT_MODE = 0;

// ================= LOCATIONS =================
const int NUM_LOC = 25;
const char* const LOC_IDS[NUM_LOC] = {
  "GYM", "GYM2", "GYM3",
  "LIB", "LIB2", "LIB3", "LIB4",
  "FCA", "FCB", "FCC",
  "DR1", "STB",
  "E2L2M", "E2L5A", "E2L5B", "E2L7", "E2L9",
  "E6L6", "E6L7", "E6L8", "E6L9",
  "W1L5", "W1L6",
  "CLS", "OUT"
};
const char* const LOC_NAMES[NUM_LOC] = {     // max 16 characters
  "Gym 1", "Gym 2", "Outdoor Gym",
  "Learning Commons", "Book Collection", "Lib Amphitheatre", "Lounge Library",
  "E4 Food Court", "W3 Food Court", "Takeaway Kiosk",
  "Discussion Room", "Study Space B",
  "E2 L2M Study", "E2 L5 Study 1", "E2 L5 Study 2", "E2 L7 Study", "E2 L9 Study",
  "E6 L6 Study", "E6 L7 Study", "E6 L8 Study", "E6 L9 Study",
  "W1 L5 Proj Hub", "W1 L6 Proj Hub",
  "Classroom", "Outdoor Area"
};

// ================= TEMPERATURE COLOURS =================
const int NUM_STOPS = 10;
const float stopsT[NUM_STOPS] = {18, 20, 22, 23.5, 26.5, 27.5, 28.5, 30, 32, 34};
const uint8_t stopsC[NUM_STOPS][3] = {
  {0, 30, 200}, {80, 170, 255}, {0, 210, 220}, {0, 255, 30}, {60, 255, 0},
  {170, 255, 0}, {255, 220, 0}, {255, 120, 0}, {255, 10, 0}, {230, 0, 90}
};

const char* const HVAC_NAMES[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};

// ================= OBJECTS & STATE =================
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);
dht DHT;
WiFiClient wifiClient;
WiFiSSLClient sheetClient;
MqttClient mqttClient(wifiClient);

int locIdx = 0;
int crowd = 0;
int hvacLevel = -1;           // index into HVAC_NAMES, reported by Board 2 (-1 = not known)
float temp = 0, hum = 0;
float soundAvg = 0;
int sound = 0;
bool haveData = false;        // true after the first valid DHT reading

// NEW: PIR + light sensor (sensed on this board)
bool pirReady = false;        // true after the PIR warm-up
bool occupied = false;
bool sentOccupied = false;    // last occupancy value sent to Board 2
int ldr = 0;                  // raw light reading
unsigned long lastMotion = 0;
int lcdTick = 0;

// NEW: decided by Board 2 and reported back (-1 = not known)
int energyLed = -1;           // 1 = LED on, 0 = off
int energyDark = -1;          // 1 = dark, 0 = bright
int energyFan = -1;           // fan speed in %
bool energySeen = false;
unsigned long lastEnergyMsg = 0;

float breath = 0, hueWave = 0;
unsigned long lastSend = 0, lastSheet = 0, lastLight = 0;

char lastLine0[17] = "";
char lastLine1[17] = "";

// ================= BUTTON (interrupt) =================
volatile bool btnFlag = false;
volatile unsigned long lastIsr = 0;

void onButton() {                        // keep tiny: just set a flag
  unsigned long now = millis();
  if (now - lastIsr > 200) {
    btnFlag = true;
    lastIsr = now;
  }
}

bool buttonPressed() {
  if (btnFlag) {
    btnFlag = false;
    return true;
  }
  return false;
}

// ================= BACKLIGHT =================
// only talks to the LCD when the colour actually changes
void applyRGB(int r, int g, int b) {
  static int lr = -1, lg = -1, lb = -1;
  if (r != lr || g != lg || b != lb) {
    lcd.setRGB(r, g, b);
    lr = r; lg = g; lb = b;
  }
}

void setScaled(float r, float g, float b, float bright) {
  applyRGB((int)(r * bright), (int)(g * bright), (int)(b * bright));
}

void hueToRGB(float h, float &r, float &g, float &b) {
  while (h >= 360) { h = h - 360; }
  float x = 1 - fabs(fmod(h / 60.0, 2) - 1);
  r = 0; g = 0; b = 0;
  if (h < 60)       { r = 1; g = x; }
  else if (h < 120) { r = x; g = 1; }
  else if (h < 180) { g = 1; b = x; }
  else if (h < 240) { g = x; b = 1; }
  else if (h < 300) { r = x; b = 1; }
  else              { r = 1; b = x; }
  r = r * 255; g = g * 255; b = b * 255;
}

void tempToColour(float t, float &r, float &g, float &b) {
  int last = NUM_STOPS - 1;
  if (t <= stopsT[0])    { r = stopsC[0][0];    g = stopsC[0][1];    b = stopsC[0][2];    return; }
  if (t >= stopsT[last]) { r = stopsC[last][0]; g = stopsC[last][1]; b = stopsC[last][2]; return; }
  for (int i = 0; i < last; i++) {
    if (t <= stopsT[i + 1]) {
      float k = (t - stopsT[i]) / (stopsT[i + 1] - stopsT[i]);
      r = stopsC[i][0] + (stopsC[i + 1][0] - stopsC[i][0]) * k;
      g = stopsC[i][1] + (stopsC[i + 1][1] - stopsC[i][1]) * k;
      b = stopsC[i][2] + (stopsC[i + 1][2] - stopsC[i][2]) * k;
      return;
    }
  }
}

void connectingGlow(unsigned long ms) {
  static float hue = 0;
  unsigned long start = millis();
  while (millis() - start < ms) {
    float r, g, b;
    hueToRGB(hue, r, g, b);
    setScaled(r, g, b, 0.15 + 0.35 * (sin(breath) + 1) / 2);
    hue = hue + 0.6;
    breath = breath + 0.05;
    delay(30);
  }
}

void rainbowBurst(unsigned long ms) {
  float hue = 0;
  unsigned long start = millis();
  while (millis() - start < ms) {
    float r, g, b;
    hueToRGB(hue, r, g, b);
    setScaled(r, g, b, 0.5);
    hue = hue + 12;
    delay(15);
  }
}

void greenBreathe(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    setScaled(0, 255, 60, 0.2 + 0.4 * (sin(breath) + 1) / 2);
    breath = breath + 0.05;
    delay(30);
  }
}

void updateBacklight() {
  if (BACKLIGHT_MODE == 0) {
    float r, g, b;
    tempToColour(temp + sin(hueWave) * 0.7, r, g, b);
    float speed = 0.03 + (temp - 20) * 0.004;
    if (speed < 0.02) { speed = 0.02; }
    breath = breath + speed;
    hueWave = hueWave + 0.013;
    setScaled(r, g, b, 0.25 + 0.45 * (sin(breath) + 1) / 2);
  } else {
    float bright = 0.25 + 0.35 * (sin(breath) + 1) / 2;
    breath = breath + 0.04;
    if (BACKLIGHT_MODE == 2 || hvacLevel == 1) { setScaled(40, 255, 40, bright); }
    else if (hvacLevel <= 0) { setScaled(120, 120, 120, bright * 0.6); }   // OFF or not known
    else if (hvacLevel == 2) { setScaled(255, 120, 0, bright); }
    else                     { setScaled(255, 0, 0, bright); }
  }
}

// ================= LCD =================
// only rewrites a line if its text changed (no flicker, less I2C traffic)
void lcdLine(int row, const char* text, char* last) {
  if (strcmp(text, last) != 0) {
    char padded[17];
    snprintf(padded, sizeof(padded), "%-16s", text);
    lcd.setCursor(0, row);
    lcd.print(padded);
    strcpy(last, text);
  }
}

void lcdReset() {
  lcd.clear();
  lastLine0[0] = '\0';
  lastLine1[0] = '\0';
}

// ================= SENSING: PIR + LIGHT SENSOR (NEW) =================
// Only reads. Sets occupied and ldr. Runs every loop pass.
void readRoom() {
  unsigned long now = millis();

  if (!pirReady && now >= PIR_WARMUP) {
    pirReady = true;
    Serial.println(F("PIR ready"));
  }
  if (pirReady) {
    if (digitalRead(pir_pin) == HIGH) {
      lastMotion = now;
      occupied = true;
    } else if (occupied && now - lastMotion >= VACANCY_TIMEOUT) {
      occupied = false;
    }
  }

  analogRead(ldr_pin);                                   // throw-away read (ADC settles after the sound pin)
  ldr = analogRead(ldr_pin);
}

// ================= LINK WITH BOARD 2 (NEW) =================
void publish(const char* sub, const char* value);        // defined further down, in CONNECTIONS

// tell Board 2 at once when the room becomes occupied or vacant (not only every 2 s)
void reportOccupancy() {
  if (occupied != sentOccupied && mqttClient.connected()) {
    publish("occupancy", occupied ? "OCCUPIED" : "VACANT");
    sentOccupied = occupied;
  }
}

// true while Board 2 is still reporting
bool energyOnline() {
  return energySeen && (millis() - lastEnergyMsg < LINK_TIMEOUT);
}

// Board 2 reports what it decided; keep it for the LCD and the Google Sheet
void onMqttMessage(int messageSize) {
  (void)messageSize;
  String topic = mqttClient.messageTopic();
  char value[16];
  int n = 0;
  while (mqttClient.available()) {
    char c = (char)mqttClient.read();
    if (n < 15) { value[n] = c; n++; }
  }
  value[n] = '\0';

  if (topic.endsWith("/energy/hvac")) {
    hvacLevel = -1;                                      // stays -1 for SENSOR_FAULT
    for (int i = 0; i < 4; i++) {
      if (strcmp(value, HVAC_NAMES[i]) == 0) { hvacLevel = i; }
    }
  }
  else if (topic.endsWith("/energy/lighting")) { energyLed = (strcmp(value, "ON") == 0) ? 1 : 0; }
  else if (topic.endsWith("/energy/ambient"))  { energyDark = (strcmp(value, "DARK") == 0) ? 1 : 0; }
  else if (topic.endsWith("/energy/fan"))      { energyFan = atoi(value); }
  else { return; }

  energySeen = true;
  lastEnergyMsg = millis();
}

// text versions of Board 2's decisions; `unknown` is shown when there is nothing to show
const char* hvacText(const char* unknown) {
  if (hvacLevel < 0) { return unknown; }
  return HVAC_NAMES[hvacLevel];
}
const char* ledText(const char* unknown) {
  if (energyLed < 0) { return unknown; }
  return energyLed ? "ON" : "OFF";
}
const char* ambientText(const char* unknown) {
  if (energyDark < 0) { return unknown; }
  return energyDark ? "DARK" : "BRIGHT";
}

// ================= LOCATION MENU =================
void selectLocation() {
  lcdReset();
  unsigned long lastPress = millis();
  int lastShown = -1;
  Serial.println(F("Select location: press to change, wait 5 s to confirm"));

  while (millis() - lastPress < SELECT_TIMEOUT) {
    if (buttonPressed()) {
      locIdx = (locIdx + 1) % NUM_LOC;
      lastPress = millis();
      lastShown = -1;
    }

    int secondsLeft = (SELECT_TIMEOUT - (millis() - lastPress)) / 1000 + 1;
    if (secondsLeft != lastShown) {
      char line[17];
      snprintf(line, sizeof(line), ">%-10s %ds", LOC_IDS[locIdx], secondsLeft);
      lcdLine(0, line, lastLine0);
      lcdLine(1, LOC_NAMES[locIdx], lastLine1);
      lastShown = secondsLeft;
    }

    setScaled(150, 0, 255, 0.25 + 0.4 * (sin(breath) + 1) / 2);   // purple = menu
    breath = breath + 0.06;
    if (mqttClient.connected()) { mqttClient.poll(); }
    readRoom();                                                    // NEW: PIR is still reported in the menu
    reportOccupancy();
    delay(20);
  }

  EEPROM.update(0, locIdx);
  Serial.print(F("Location confirmed: "));
  Serial.println(LOC_IDS[locIdx]);

  lcdReset();
  lcdLine(0, "Location set:", lastLine0);
  lcdLine(1, LOC_IDS[locIdx], lastLine1);
  greenBreathe(1200);
  lcdReset();
  lcdLine(0, "Loading data...", lastLine0);
  delay(600);
  lcdReset();

  lastSend = millis() - SEND_INTERVAL;                 // read sensors now
  lastSheet = millis() - SHEET_INTERVAL + 3000;        // log new location in ~3 s
}

// ================= SENSORS =================
int readSoundRaw() {
  unsigned long start = millis();
  int hi = 0, lo = 1023;
  while (millis() - start < 50) {
    int v = analogRead(sound_pin);
    if (v > hi) { hi = v; }
    if (v < lo) { lo = v; }
  }
  return hi - lo;                                      // peak-to-peak loudness
}

void readSensors() {
  int chk = DHT.read11(dht_apin);
  float t = DHT.temperature;
  float h = DHT.humidity;
  // accept OK (0) or checksum warning (-1) only if the values make sense
  if ((chk == 0 || chk == -1) && t > 0 && t < 60 && h > 0 && h <= 100) {
    temp = t + TEMP_OFFSET;
    hum = h + HUM_OFFSET;
    haveData = true;
  } else {
    Serial.print(F("DHT read rejected, status "));
    Serial.println(chk);
  }

  soundAvg = soundAvg * 0.7 + readSoundRaw() * 0.3;   // smoothing
  sound = (int)(soundAvg + 0.5);
}

// (the HVAC level is no longer decided here: Board 2 decides it and reports it back)

// ================= CONNECTIONS =================
void connectWiFi() {
  lcdReset();
  lcdLine(0, "Connecting WiFi", lastLine0);
  while (WiFi.status() != WL_CONNECTED) {
    connectingGlow(1500);
    WiFi.begin(ssid, pass);
    connectingGlow(1500);
  }
  Serial.println(F("WiFi connected"));
  lcdLine(0, "WiFi connected!", lastLine0);
  rainbowBurst(2000);
}

void connectMQTT() {
  lcdReset();
  lcdLine(0, "Connecting MQTT", lastLine0);
  mqttClient.setId(MQTT_ID);
  while (!mqttClient.connect(broker, port)) {
    Serial.print(F("MQTT failed, error "));
    Serial.println(mqttClient.connectError());
    greenBreathe(2000);
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
  }
  Serial.println(F("MQTT connected"));
  mqttClient.subscribe("sitg7/room1/energy/hvac");       // NEW: Board 2's decisions
  mqttClient.subscribe("sitg7/room1/energy/lighting");
  mqttClient.subscribe("sitg7/room1/energy/ambient");
  mqttClient.subscribe("sitg7/room1/energy/fan");
  lcdLine(0, "All connected :)", lastLine0);
  greenBreathe(2000);
  lcdReset();
}

void publish(const char* sub, const char* value) {
  char topic[40];
  snprintf(topic, sizeof(topic), "%s%s", TOPIC_BASE, sub);
  mqttClient.beginMessage(topic);
  mqttClient.print(value);
  mqttClient.endMessage();
}

// ================= GOOGLE SHEET =================
void sendToSheet() {
  char tBuf[8], hBuf[8];
  dtostrf(temp, 1, 1, tBuf);
  dtostrf(hum, 1, 1, hBuf);

  char json[420];
  snprintf(json, sizeof(json),
    "{\"api_key\":\"%s\",\"facility_id\":\"%s\",\"facility_name\":\"%s\","
    "\"temperature\":%s,\"humidity\":%s,\"sound_level\":%d,"
    "\"hvac\":\"%s\",\"data_source\":\"SENSOR\","
    "\"occupancy\":\"%s\",\"light_level\":%d,\"ambient\":\"%s\","      // NEW fields (UNKNOWN / -1 = no link to Board 2)
    "\"lighting\":\"%s\",\"fan_speed\":%d}",
    API_KEY, LOC_IDS[locIdx], LOC_NAMES[locIdx], tBuf, hBuf, sound, hvacText("UNKNOWN"),
    occupied ? "OCCUPIED" : "VACANT", ldr, ambientText("UNKNOWN"),
    ledText("UNKNOWN"), energyFan);

  Serial.print(F("Sending to sheet ("));
  Serial.print(LOC_IDS[locIdx]);
  Serial.println(F(")..."));

  if (sheetClient.connectSSL(GAS_HOST, 443)) {
    sheetClient.print(F("POST /macros/s/"));
    sheetClient.print(DEPLOY_ID);
    sheetClient.println(F("/exec HTTP/1.1"));
    sheetClient.println(F("Host: script.google.com"));
    sheetClient.println(F("Content-Type: application/json"));
    sheetClient.print(F("Content-Length: "));
    sheetClient.println(strlen(json));
    sheetClient.println(F("Connection: close"));
    sheetClient.println();
    sheetClient.print(json);

    unsigned long t0 = millis();
    while (!sheetClient.available() && millis() - t0 < 1500) { delay(10); }
    char status[40] = "";
    int n = sheetClient.readBytesUntil('\n', status, sizeof(status) - 1);
    status[n] = '\0';
    Serial.print(F("Sheet reply: "));
    Serial.println(status);                // "HTTP/1.1 302 ..." = OK
    sheetClient.stop();
  } else {
    Serial.println(F("Sheet connection failed"));
  }
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(9600);
  pinMode(button_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(button_pin), onButton, FALLING);

  pinMode(pir_pin, INPUT);                             // NEW
  mqttClient.onMessage(onMqttMessage);                 // NEW: listen to Board 2

  lcd.init();

  locIdx = EEPROM.read(0);
  if (locIdx >= NUM_LOC) { locIdx = 0; }

  selectLocation();
  connectWiFi();
  connectMQTT();

  lastSend = millis() - SEND_INTERVAL;
  lastSheet = millis() - SHEET_INTERVAL + 10000;   // first sheet post after ~10 s
}

void loop() {
  unsigned long now = millis();

  // keep connections alive
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); connectMQTT(); }
  if (!mqttClient.connected()) { connectMQTT(); }
  mqttClient.poll();

  // button -> location menu
  if (buttonPressed()) { selectLocation(); }

  // NEW: sense PIR + light every pass; tell Board 2 at once when the room changes
  readRoom();
  reportOccupancy();
  if (!energyOnline()) { hvacLevel = -1; energyLed = -1; energyDark = -1; energyFan = -1; }   // forget old decisions

  // crowd level from Serial Monitor (simulated until the PIR board is linked)
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c >= '0' && c <= '3') { crowd = c - '0'; }
  }

  // every 2 s: sensors -> HVAC -> LCD -> MQTT
  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;
    readSensors();

    // NEW: room readings go out even without DHT data, so Board 2 can still run the lights
    char buf[12];
    publish("occupancy", occupied ? "OCCUPIED" : "VACANT");
    sentOccupied = occupied;
    itoa(ldr, buf, 10);
    publish("ldr", buf);

    if (haveData) {
      char line0[17], line1[17];
      if (lcdTick < PAGE_TICKS) {                          // page 1: readings (as before)
        snprintf(line0, sizeof(line0), "%dC %d%% S:%d", (int)(temp + 0.5), (int)(hum + 0.5), sound);
        snprintf(line1, sizeof(line1), "%s %s C:%d", LOC_IDS[locIdx], hvacText("--"), crowd);
      } else {                                             // page 2 (NEW): room here, LED + fan from Board 2
        snprintf(line0, sizeof(line0), "%s %s",
                 pirReady ? (occupied ? "OCCUPIED" : "VACANT") : "WARMUP", ambientText("--"));
        if (energyOnline() && energyFan >= 0) {
          snprintf(line1, sizeof(line1), "LED:%-3s FAN:%d%%", ledText("--"), energyFan);
        } else {
          snprintf(line1, sizeof(line1), "Board 2: no link");
        }
      }
      lcdTick = (lcdTick + 1) % (2 * PAGE_TICKS);
      lcdLine(0, line0, lastLine0);
      lcdLine(1, line1, lastLine1);

      dtostrf(temp, 1, 1, buf); publish("temp", buf);
      dtostrf(hum, 1, 1, buf);  publish("hum", buf);
      itoa(sound, buf, 10);     publish("sound", buf);
      itoa(crowd, buf, 10);     publish("crowd", buf);
      publish("location", LOC_IDS[locIdx]);
      // (sitg7/room1/hvac is now published by Board 2, the board that decides it)

      Serial.print(LOC_IDS[locIdx]);
      Serial.print(F("  T=")); Serial.print(temp, 1);
      Serial.print(F(" H="));  Serial.print(hum, 1);
      Serial.print(F(" S="));  Serial.print(sound);
      Serial.print(F(" C="));  Serial.print(crowd);
      Serial.print(F(" | "));    Serial.print(pirReady ? (occupied ? F("OCCUPIED") : F("VACANT")) : F("PIR-WARMUP"));   // NEW
      Serial.print(F(" LDR="));  Serial.print(ldr);
      if (energyOnline()) {                                // NEW: what Board 2 decided
        Serial.print(F(" | Board2: HVAC=")); Serial.print(hvacText("--"));
        Serial.print(F(" "));     Serial.print(ambientText("--"));
        Serial.print(F(" LED=")); Serial.print(ledText("--"));
        Serial.print(F(" FAN=")); Serial.print(energyFan); Serial.println(F("%"));
      } else {
        Serial.println(F(" | Board2: no link"));
      }
    } else {
      lcdLine(0, "Waiting for DHT", lastLine0);
    }
  }

  // every 60 s: Google Sheet (only with real data)
  if (haveData && now - lastSheet >= SHEET_INTERVAL) {
    lastSheet = now;
    sendToSheet();
  }

  // backlight ~30 times a second
  if (now - lastLight >= 30) {
    lastLight = now;
    updateBacklight();
  }
}