#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include "DFRobot_RGBLCD1602.h"
#include "dht.h"

#define dht_apin A0
#define sound_pin A1
#define ldr_pin A2
#define pir_pin 2
#define button_pin 4
#define DEGREE_CHAR 223

const char ssid[] = "weibin";
const char pass[] = "31415926";

const char broker[] = "broker.emqx.io";
const int  port = 1883;
const char MQTT_ID[] = "sitg7-weibin";
const char TOPIC_BASE[] = "sitg7/room1/";

const char GAS_HOST[] = "script.google.com";
const char DEPLOY_ID[] = "AKfycbzlVsZVIWkLt818jMaaC5FyiXxicgMHUnufDmqna2tomFhcsaretr3Tu6svYqhBWqiJOQ";
const char API_KEY[] = "env-g7-8f3k2q";

const unsigned long SEND_INTERVAL  = 2000;
const unsigned long SHEET_INTERVAL = 60000;
const unsigned long ENERGY_MIN_GAP  = 20000;
const unsigned long SELECT_TIMEOUT = 5000;

const float TEMP_OFFSET = 0.0;
const float HUM_OFFSET  = 0.0;

const unsigned long VACANCY_TIMEOUT = 3000;
const unsigned long PIR_WARMUP = 60000;

const unsigned long LINK_TIMEOUT = 10000;
const int PAGE_TICKS = 2;

const int NUM_LOC = 27;
const char* const LOC_IDS[NUM_LOC] = {
  "GYM", "GYM2", "GYM3",
  "LIB", "LIB2", "LIB3", "LIB4",
  "FCA", "FCB", "FCC",
  "DR1", "STB",
  "E2L2M", "E2L5A", "E2L5B", "E2L7", "E2L9",
  "E6L6", "E6L7", "E6L8", "E6L9",
  "W1L5", "W1L6",
  "CLS", "OUT", "DR2", "DR3"
};
const char* const LOC_NAMES[NUM_LOC] = {
  "Gym 1", "Gym 2", "Outdoor Gym",
  "Learning Commons", "Book Collection", "Lib Amphitheatre", "Lounge Library",
  "E4 Food Court", "W3 Food Court", "Takeaway Kiosk",
  "Discussion Room", "Common Study B",
  "E2 L2M Study", "E2 L5 Study 1", "E2 L5 Study 2", "E2 L7 Study", "E2 L9 Study",
  "E6 L6 Study", "E6 L7 Study", "E6 L8 Study", "E6 L9 Study",
  "W1 L5 Proj Hub", "W1 L6 Proj Hub",
  "Classroom", "Outdoor Area", "Discussion Rm 2", "Discussion Rm 3"
};

const byte RGB_GREEN[3] = {0, 255, 60};
const byte RGB_BLUE[3] = {0, 150, 255};
const byte RGB_RED[3] = {255, 0, 60};

const char* const HVAC_NAMES[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};

DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);
dht DHT;
WiFiClient wifiClient;
WiFiSSLClient sheetClient;
MqttClient mqttClient(wifiClient);

int locIdx = 0;
int crowd = 0;
int hvacLevel = -1;
float temp = 0, hum = 0;
float soundAvg = 0;
int sound = 0;
int soundRaw = 0;
bool haveData = false;

const unsigned long DHT_STALE_MS = 10000;
unsigned long lastGoodDht = 0;

bool dhtFresh() {
  return haveData && (millis() - lastGoodDht < DHT_STALE_MS);
}

bool pirReady = false;
bool occupied = false;
bool sentOccupied = false;
int ldr = 0;
unsigned long lastMotion = 0;
int lcdTick = 0;

int energyLed = -1;
int energyDark = -1;
int energyFan = -1;
bool energySeen = false;
unsigned long lastEnergyMsg = 0;

int sentLed = -2, sentFan = -2, sentHvac = -2;
unsigned long lastEnergySheet = 0;

unsigned long lastSend = 0, lastSheet = 0, lastLight = 0;

char lastLine0[17] = "";
char lastLine1[17] = "";

volatile bool btnFlag = false;
volatile unsigned long lastIsr = 0;

void onButton() {
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

void setColor(const byte rgb[3]) {
  lcd.setRGB(rgb[0], rgb[1], rgb[2]);
}

void updateBacklight() {
  if (!mqttClient.connected()) {
    setColor(RGB_RED);
  } else if (WiFi.status() == WL_CONNECTED) {
    setColor(RGB_GREEN);
  } else {
    setColor(RGB_BLUE);
  }
}

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

  analogRead(ldr_pin);
  ldr = analogRead(ldr_pin);
}

void publish(const char* sub, const char* value);

void reportOccupancy() {
  if (occupied != sentOccupied && mqttClient.connected()) {
    publish("occupancy", occupied ? "OCCUPIED" : "VACANT");
    sentOccupied = occupied;
  }
}

bool energyOnline() {
  return energySeen && (millis() - lastEnergyMsg < LINK_TIMEOUT);
}

char pendingLoc[16] = "";
bool locPending = false;

void applyPendingLocation() {
  if (!locPending) { return; }
  locPending = false;
  char up[16];
  int k = 0;
  for (; pendingLoc[k] && k < 15; k++) { up[k] = toupper(pendingLoc[k]); }
  up[k] = '\0';
  for (int i = 0; i < NUM_LOC; i++) {
    if (strcmp(LOC_IDS[i], up) == 0) {
      locIdx = i;
      EEPROM.update(0, locIdx);
      sentLed = -2;
      publish("location", LOC_IDS[locIdx]);
      Serial.print(F("Location set from panel: "));
      Serial.println(LOC_IDS[locIdx]);
      return;
    }
  }
  Serial.print(F("ERROR: unknown location from panel: "));
  Serial.println(up);
}

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

  if (topic.endsWith("/cmd/location")) {
    strncpy(pendingLoc, value, 15);
    pendingLoc[15] = '\0';
    locPending = true;
    return;
  }

  if (topic.endsWith("/energy/hvac")) {
    hvacLevel = -1;
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

void selectLocation() {
  lcdReset();
  unsigned long lastPress = millis();
  int lastShown = -1;
  Serial.println(F("Select location: press to change, wait 5 s to confirm"));

  while (millis() - lastPress < SELECT_TIMEOUT) {
    if (btnFlag) {
      btnFlag = false;
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

    lcd.setRGB(150, 0, 255);
    if (mqttClient.connected()) { mqttClient.poll(); }
    readRoom();
    reportOccupancy();
    delay(10);
  }

  EEPROM.update(0, locIdx);
  sentLed = -2;
  Serial.print(F("Location confirmed: "));
  Serial.println(LOC_IDS[locIdx]);

  lcdReset();
  lcdLine(0, "Location set:", lastLine0);
  lcdLine(1, LOC_IDS[locIdx], lastLine1);
  setColor(RGB_GREEN);
  delay(1200);
  lcdReset();
  lcdLine(0, "Loading data...", lastLine0);
  delay(600);
  lcdReset();

  lastSend = millis() - SEND_INTERVAL;
  lastSheet = millis() - SHEET_INTERVAL + 3000;
}

const unsigned long SOUND_SLICE_MS = 10;
const int SOUND_WINDOW_SLICES = 5;
const float SOUND_ALPHA = 0.1;

int sliceMin = 1023, sliceMax = 0;
int sliceMinArr[SOUND_WINDOW_SLICES], sliceMaxArr[SOUND_WINDOW_SLICES];
int sliceCount = 0, sliceNext = 0;
unsigned long sliceStart = 0;
bool soundStarted = false;

void updateSound() {
  unsigned long now = millis();
  if (!soundStarted) { sliceStart = now; soundStarted = true; }

  int v = analogRead(sound_pin);
  if (v < sliceMin) { sliceMin = v; }
  if (v > sliceMax) { sliceMax = v; }

  if (now - sliceStart < SOUND_SLICE_MS) { return; }

  sliceMinArr[sliceNext] = sliceMin;
  sliceMaxArr[sliceNext] = sliceMax;
  sliceNext = (sliceNext + 1) % SOUND_WINDOW_SLICES;
  if (sliceCount < SOUND_WINDOW_SLICES) { sliceCount++; }
  sliceMin = 1023;
  sliceMax = 0;
  sliceStart = now;

  int hi = 0, lo = 1023;
  for (int i = 0; i < sliceCount; i++) {
    if (sliceMaxArr[i] > hi) { hi = sliceMaxArr[i]; }
    if (sliceMinArr[i] < lo) { lo = sliceMinArr[i]; }
  }
  soundRaw = (hi - lo) * 2;
  soundAvg = soundAvg + SOUND_ALPHA * (soundRaw - soundAvg);
  sound = (int)(soundAvg + 0.5);
}

void readSensors() {
  int chk = DHT.read11(dht_apin);
  float t = DHT.temperature;
  float h = DHT.humidity;

  if ((chk == 0 || chk == -1) && t > 0 && t < 60 && h > 0 && h <= 100) {
    temp = t + TEMP_OFFSET;
    hum = h + HUM_OFFSET;
    haveData = true;
    lastGoodDht = millis();
  } else {
    Serial.print(F("DHT read rejected, status "));
    Serial.println(chk);
  }

}

void connectWiFi() {
  lcdReset();
  lcdLine(0, "Connecting WiFi", lastLine0);
  setColor(RGB_BLUE);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1500);
    WiFi.begin(ssid, pass);
    delay(1500);
  }
  Serial.println(F("WiFi connected"));
  lcdLine(0, "WiFi connected!", lastLine0);
  setColor(RGB_GREEN);
  delay(2000);
}

void connectMQTT() {
  lcdReset();
  lcdLine(0, "Connecting MQTT", lastLine0);
  setColor(RGB_BLUE);
  mqttClient.setId(MQTT_ID);
  while (!mqttClient.connect(broker, port)) {
    Serial.print(F("MQTT failed, error "));
    Serial.println(mqttClient.connectError());
    delay(2000);
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
  }
  Serial.println(F("MQTT connected"));
  mqttClient.subscribe("sitg7/room1/energy/hvac");
  mqttClient.subscribe("sitg7/room1/energy/lighting");
  mqttClient.subscribe("sitg7/room1/energy/ambient");
  mqttClient.subscribe("sitg7/room1/energy/fan");
  mqttClient.subscribe("sitg7/room1/cmd/location");
  lcdLine(0, "All connected :)", lastLine0);
  setColor(RGB_GREEN);
  delay(2000);
  lcdReset();
}

void publish(const char* sub, const char* value) {
  char topic[40];
  snprintf(topic, sizeof(topic), "%s%s", TOPIC_BASE, sub);
  mqttClient.beginMessage(topic);
  mqttClient.print(value);
  mqttClient.endMessage();
}

void postToSheet(const char* json) {
  if (sheetClient.connectSSL(GAS_HOST, 443)) {
    sheetClient.setConnectionTimeout(3000);
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
    Serial.println(status);
    sheetClient.stop();
  } else {
    Serial.println(F("Sheet connection failed"));
  }
}

void sendToSheet() {
  char tBuf[8], hBuf[8];
  dtostrf(temp, 1, 1, tBuf);
  dtostrf(hum, 1, 1, hBuf);

  char json[300];
  int n = snprintf(json, sizeof(json),
    "{\"api_key\":\"%s\",\"facility_id\":\"%s\",\"facility_name\":\"%s\","
    "\"temperature\":%s,\"humidity\":%s,\"sound_level\":%d,"
    "\"occupancy\":\"%s\",\"light_level\":%d,"
    "\"hvac\":\"%s\",\"data_source\":\"SENSOR\"",
    API_KEY, LOC_IDS[locIdx], LOC_NAMES[locIdx], tBuf, hBuf, sound,
    occupied ? "OCCUPIED" : "VACANT", ldr, hvacText(""));

  if (energyLed >= 0) { n += snprintf(json + n, sizeof(json) - n, ",\"lighting\":\"%s\"", energyLed ? "ON" : "OFF"); }
  if (energyFan >= 0) { n += snprintf(json + n, sizeof(json) - n, ",\"fan_speed\":%d", energyFan); }
  if (energyDark >= 0) { n += snprintf(json + n, sizeof(json) - n, ",\"ambient\":\"%s\"", energyDark ? "DARK" : "BRIGHT"); }
  snprintf(json + n, sizeof(json) - n, "}");

  Serial.print(F("Sending to sheet ("));
  Serial.print(LOC_IDS[locIdx]);
  Serial.println(F(")..."));
  postToSheet(json);
}

void sendEnergyToSheet() {
  char json[200];
  int n = snprintf(json, sizeof(json),
    "{\"api_key\":\"%s\",\"type\":\"energy\",\"facility_id\":\"%s\",\"light_on\":%s,\"fan_pct\":%d",
    API_KEY, LOC_IDS[locIdx], energyLed ? "true" : "false", energyFan);
  if (hvacLevel >= 0) {
    n += snprintf(json + n, sizeof(json) - n, ",\"hvac\":\"%s\"", HVAC_NAMES[hvacLevel]);
  }
  snprintf(json + n, sizeof(json) - n, ",\"data_source\":\"SENSOR\"}");

  Serial.print(F("Sending Board 2 status to sheet ("));
  Serial.print(LOC_IDS[locIdx]);
  Serial.println(F(")..."));
  postToSheet(json);

  sentLed = energyLed;
  sentFan = energyFan;
  sentHvac = hvacLevel;
}

void setup() {
  Serial.begin(9600);
  pinMode(button_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(button_pin), onButton, FALLING);

  pinMode(pir_pin, INPUT);
  mqttClient.onMessage(onMqttMessage);
  WiFi.setTimeout(5000);
  mqttClient.setConnectionTimeout(5000);

  lcd.init();

  locIdx = EEPROM.read(0);
  if (locIdx >= NUM_LOC) { locIdx = 0; }

  selectLocation();
  connectWiFi();
  connectMQTT();

  lastSend = millis() - SEND_INTERVAL;
  lastSheet = millis() - SHEET_INTERVAL + 10000;
}

void loop() {
  unsigned long now = millis();
  applyPendingLocation();

  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); connectMQTT(); }
  if (!mqttClient.connected()) { connectMQTT(); }
  mqttClient.poll();

  if (buttonPressed()) { selectLocation(); }

  readRoom();
  updateSound();
  reportOccupancy();
  if (!energyOnline()) { hvacLevel = -1; energyLed = -1; energyDark = -1; energyFan = -1; }

  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c >= '0' && c <= '3') { crowd = c - '0'; }
  }

  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;
    readSensors();

    char buf[12];
    publish("occupancy", occupied ? "OCCUPIED" : "VACANT");
    sentOccupied = occupied;
    itoa(ldr, buf, 10);
    publish("ldr", buf);

    if (haveData) {
      char line0[17], line1[17];

      if (lcdTick < PAGE_TICKS) {
        snprintf(line0, sizeof(line0), "%d%cC %d%% S:%d", (int)(temp + 0.5), DEGREE_CHAR, (int)(hum + 0.5), sound);
        snprintf(line1, sizeof(line1), "%s %s C:%d", LOC_IDS[locIdx], hvacText("--"), crowd);
      } else {
        snprintf(line0, sizeof(line0), "%s %s", pirReady ? (occupied ? "OCCUPIED" : "VACANT") : "WARMUP", ambientText("--"));
        if (energyOnline() && energyFan >= 0) {
          snprintf(line1, sizeof(line1), "LED:%-3s FAN:%d%%", ledText("--"), energyFan);
        } else {
          snprintf(line1, sizeof(line1), "Board 2: no link");
        }
      }
      lcdTick = (lcdTick + 1) % (2 * PAGE_TICKS);

      lcdLine(0, line0, lastLine0);
      lcdLine(1, line1, lastLine1);

      if (dhtFresh()) {
        dtostrf(temp, 1, 1, buf); publish("temp", buf);
        dtostrf(hum, 1, 1, buf);  publish("hum", buf);
      } else {
        publish("temp", "ERROR");
        publish("hum", "ERROR");
      }
      itoa(sound, buf, 10);     publish("sound", buf);
      publish("location", LOC_IDS[locIdx]);

      Serial.print("temp:"); Serial.print(temp);
      Serial.print(" hum:"); Serial.print(hum);
      Serial.print(" soundRaw:"); Serial.print(soundRaw);
      Serial.print(" soundAvg:"); Serial.print(sound);
      Serial.print(" ldr:"); Serial.println(ldr);

      Serial.print(LOC_IDS[locIdx]);
      Serial.print(F("  T=")); Serial.print(temp, 1);
      Serial.print(F(" H="));  Serial.print(hum, 1);
      Serial.print(F(" S="));  Serial.print(sound);
      Serial.print(F(" C="));  Serial.print(crowd);
      Serial.print(F(" | "));    Serial.print(pirReady ? (occupied ? F("OCCUPIED") : F("VACANT")) : F("PIR-WARMUP"));
      Serial.print(F(" LDR="));  Serial.print(ldr);
      if (energyOnline()) {
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

  if (dhtFresh() && now - lastSheet >= SHEET_INTERVAL) {
    lastSheet = now;
    sendToSheet();
  }

  if (energyOnline() && energyLed >= 0 && energyFan >= 0) {
    bool changed = (energyLed != sentLed || energyFan != sentFan || hvacLevel != sentHvac);
    if (changed && now - lastEnergySheet >= ENERGY_MIN_GAP) {
      lastEnergySheet = now;
      sendEnergyToSheet();
    }
  }

  if (now - lastLight >= 30) {
    lastLight = now;
    updateBacklight();
  }
}
