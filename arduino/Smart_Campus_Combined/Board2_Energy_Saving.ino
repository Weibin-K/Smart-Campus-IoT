// =====================================================
// BOARD 2 - ENERGY SAVING MODULE (SIT Group 7, Room 1)
// Board: Arduino Uno WiFi Rev2
//
// This board has NO sensors. It listens to Board 1 (sensor node) over MQTT,
// decides, and switches the LED and the fan. It reports its decisions back
// so Board 1 can show them on the LCD and log them to the Google Sheet.
//
//   Board 1 (sensors) --MQTT--> Board 2 decides --> LED + fan
//   Board 2           --MQTT--> Board 1 LCD / Google Sheet, Energy dashboard
//
// PIN MAP
//   D5        LED         (through a resistor)
//   D6        Fan signal  (PWM, into a transistor / fan module, NOT straight into the fan)
//   Built-in LED          ON while connected to the MQTT broker
//
// LISTENS TO
//   sitg7/room1/temp, hum, occupancy, ldr, crowd     from Board 1
//   sitg7/room1/cmd/mode, scenario, crowd            from the phone
// PUBLISHES
//   sitg7/room1/energy/...   (same topics as Smart_Energy_System_MQTT, plus fan and link)
//   sitg7/room1/hvac
//
// LIBRARIES (Library Manager): WiFiNINA, ArduinoMqttClient
// =====================================================

#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>

// =====================================================
// 1. SETTINGS (edit these)
// =====================================================

char ssid[] = "weibin";                           // must be the same network as Board 1's broker access
char pass[] = "31415926";

const char BROKER[] = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char CLIENT_ID[] = "sitg7-energy-uno02";    // must be different from Board 1's ID

// =====================================================
// 2. PINS
// =====================================================

const int LED_PIN = 5;
const int FAN_PIN = 6;

// =====================================================
// 3. THRESHOLDS AND TIMING
// =====================================================

// Lighting (raw LDR reading sent by Board 1). Between the two values the old state is kept.
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
const unsigned long FAN_KICK_MS = 300UL;          // full power for a moment so a stopped fan spins up

const unsigned long DATA_TIMEOUT = 20000UL;       // nothing from Board 1 for this long = data lost
                                                  // (long enough to ride out Board 1's Google Sheet uploads)
const unsigned long PUBLISH_INTERVAL = 2000UL;
const unsigned long WIFI_RETRY_INTERVAL = 15000UL;
const unsigned long MQTT_RETRY_INTERVAL = 10000UL;

// =====================================================
// 4. OBJECTS
// =====================================================

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// =====================================================
// 5. VARIABLES
// =====================================================

// Latest values received from Board 1, and when each one arrived
float actualTemperature = NAN;
float actualHumidity = NAN;
bool reportedOccupied = false;
int ldrValue = 0;
bool gotTemp = false, gotHum = false, gotOccupancy = false, gotLdr = false;
unsigned long lastTempMsg = 0, lastHumMsg = 0, lastOccupancyMsg = 0, lastLdrMsg = 0;

bool dhtValid = false;                  // fresh temperature AND humidity from Board 1
bool roomValid = false;                 // fresh occupancy AND light reading from Board 1

// Lighting decision
bool occupied = false;
bool isDark = false;
bool lightingOn = false;

// Phone controls
bool simulationMode = false;            // default LIVE
String selectedScenario = "NORMAL";
int crowdLevel = 0;                     // 0 none, 1 low, 2 medium, 3 high  (ONE shared variable)

// HVAC decision
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
bool wifiTried = false, mqttTried = false;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastPublish = 0;
int publishStep = 0;                    // 0 = idle, 1-16 = which message goes out next

// =====================================================
// 6. MQTT IN: SENSOR DATA FROM BOARD 1 + PHONE COMMANDS
// =====================================================

// accept only the agreed 0-3 payloads
void applyCrowdLevel(String value) {
  if (value == "0" || value == "1" || value == "2" || value == "3") {
    crowdLevel = value.toInt();
  } else {
    Serial.println("ERROR: invalid crowd level");
  }
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

  // ---------- sensor readings from Board 1 ----------
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
    if (!simulationMode) { applyCrowdLevel(value); }          // LIVE: crowd comes from Board 1
  }

  // ---------- phone commands ----------
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
    if (simulationMode) { applyCrowdLevel(value); }           // SIMULATION: crowd comes from the phone
    else { Serial.println("Phone crowd ignored in LIVE mode (Board 1 sends the crowd level)"); }
  }
}

// =====================================================
// 7. WIFI + MQTT CONNECTION
// =====================================================

bool connectMQTT() {
  Serial.println("Connecting to HiveMQ...");
  bool ok = mqttClient.connect(BROKER, MQTT_PORT);    // can pause for several seconds if it fails
  lastMQTTAttempt = millis();
  if (ok) {
    Serial.println("MQTT connected");
    mqttClient.subscribe("sitg7/room1/temp");         // from Board 1
    mqttClient.subscribe("sitg7/room1/hum");
    mqttClient.subscribe("sitg7/room1/occupancy");
    mqttClient.subscribe("sitg7/room1/ldr");
    mqttClient.subscribe("sitg7/room1/crowd");
    mqttClient.subscribe("sitg7/room1/cmd/#");        // from the phone
    return true;
  }
  Serial.print("MQTT failed, error ");
  Serial.println(mqttClient.connectError());
  return false;
}

// called every loop pass: connects / reconnects when needed, then receives messages
void maintainNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);
    if (mqttClient.connected()) { mqttClient.stop(); }
    if (!wifiTried || millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
      wifiTried = true;
      Serial.println("Connecting WiFi...");
      WiFi.begin(ssid, pass);                         // pauses here for a few seconds
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
    digitalWrite(LED_BUILTIN, HIGH);                  // built-in LED = connected to the broker
    mqttClient.poll();                                // receives Board 1 data and phone commands
  }
}

// =====================================================
// 8. IS BOARD 1'S DATA STILL FRESH?
// =====================================================

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

// =====================================================
// 9. SMART LIGHTING DECISION
//    LED ON only when the room is occupied AND dark.
//    No fresh data from Board 1 = LED OFF (fail safe).
// =====================================================

void updateLighting() {
  if (roomValid) {
    occupied = reportedOccupied;

    // brightness with hysteresis
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

  bool newLightingState = roomValid && occupied && isDark;
  if (newLightingState != lightingOn) {
    lightingOn = newLightingState;
    Serial.print("LIGHTING: ");
    if (lightingOn) { Serial.println("ON"); } else { Serial.println("OFF"); }
  }

  if (lightingOn) { digitalWrite(LED_PIN, HIGH); }
  else { digitalWrite(LED_PIN, LOW); }
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
    // LIVE mode but no fresh temperature / humidity from Board 1
    effectiveDataValid = false;
    hvacDetailed = "SENSOR_FAULT";
    hvacStandard = "SENSOR_FAULT";
    return;
  }

  hvacDetailed = calculateHVAC(effectiveTemperature, effectiveHumidity, crowdLevel);

  // four-level version for Board 1's LCD and dashboards that only know OFF / LOW / MEDIUM / HIGH
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
// 12. MQTT OUT
//     One message per loop pass, so the 16 messages go out over
//     about a second without holding up the LED and fan.
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
    // the four that Board 1 shows on its LCD go first
    case 1:  publishValue("sitg7/room1/energy/hvac", hvacStandard); break;
    case 2:  if (lightingOn) { publishValue("sitg7/room1/energy/lighting", "ON"); }
             else { publishValue("sitg7/room1/energy/lighting", "OFF"); }
             break;
    case 3:  if (isDark) { publishValue("sitg7/room1/energy/ambient", "DARK"); }
             else { publishValue("sitg7/room1/energy/ambient", "BRIGHT"); }
             break;
    case 4:  publishValue("sitg7/room1/energy/fan", String(fanPercent)); break;

    // rest of the Energy dashboard topics
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
    case 15: if (dhtValid && roomValid) { publishValue("sitg7/room1/energy/link", "OK"); }      // is Board 1 still sending?
             else { publishValue("sitg7/room1/energy/link", "LOST"); }
             break;

    // old topic from the LCD sketch, for the app
    case 16: publishValue("sitg7/room1/hvac", hvacStandard); break;
  }

  publishStep = publishStep + 1;
  if (publishStep > 16) { publishStep = 0; }
}

// =====================================================
// 13. SERIAL MONITOR STATUS (one line every 2 s)
// =====================================================

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

// =====================================================
// 14. SETUP
// =====================================================

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
  WiFi.setTimeout(8000);                    // WiFi.begin gives up after 8 s instead of 50 s

  Serial.println("BOARD 2 - ENERGY SAVING MODULE");
}

// =====================================================
// 15. MAIN LOOP:  listen -> decide -> act -> report
// =====================================================

void loop() {
  maintainNetwork();          // WiFi / MQTT + receive Board 1 data and phone commands

  unsigned long now = millis();

  checkSensorData();          // is Board 1 still sending?
  updateLighting();           // occupancy + light level -> LED
  updateHVAC();               // temp + hum + crowd -> HVAC level
  updateFan(now);             // HVAC level -> fan speed

  // every 2 s: start a new round of MQTT messages
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    publishStep = 1;
    printStatus();
  }
  publishNextMessage();

  delay(20);
}
