
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <DHT.h>

// =====================================================
// 1. WIFI CONFIGURATION
// =====================================================

char ssid[] = "nus";
char pass[] = "nusss189";

const char BROKER[] = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char CLIENT_ID[] = "sitg7-energy-uno02";

// =====================================================
// 2. MQTT COMMAND TOPICS
// =====================================================

const char TOPIC_CMD_MODE[] =
  "sitg7/room1/cmd/mode";

const char TOPIC_CMD_SCENARIO[] =
  "sitg7/room1/cmd/scenario";

const char TOPIC_CMD_CROWD[] =
  "sitg7/room1/cmd/crowd";

// =====================================================
// 3. MQTT OUTPUT TOPICS
// Use an energy-specific namespace to avoid
// conflicting with other group devices.
// =====================================================

const char TOPIC_TEMP[] =
  "sitg7/room1/energy/temp";

const char TOPIC_HUM[] =
  "sitg7/room1/energy/hum";

const char TOPIC_ACTUAL_TEMP[] =
  "sitg7/room1/energy/actual_temp";

const char TOPIC_ACTUAL_HUM[] =
  "sitg7/room1/energy/actual_hum";

const char TOPIC_CROWD[] =
  "sitg7/room1/energy/crowd";

const char TOPIC_HVAC[] =
  "sitg7/room1/energy/hvac";

const char TOPIC_HVAC_DETAIL[] =
  "sitg7/room1/energy/hvac_detail";

const char TOPIC_LIGHTING[] =
  "sitg7/room1/energy/lighting";

const char TOPIC_MODE[] =
  "sitg7/room1/energy/mode";

const char TOPIC_SCENARIO[] =
  "sitg7/room1/energy/scenario";

const char TOPIC_AMBIENT[] =
  "sitg7/room1/energy/ambient";

const char TOPIC_DHT_STATUS[] =
  "sitg7/room1/energy/dht_status";

const char TOPIC_OCCUPANCY[] =
  "sitg7/room1/energy/occupancy";

// =====================================================
// 4. MQTT CLIENT
// =====================================================

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// =====================================================
// 5. HARDWARE PIN ALLOCATION
// =====================================================

const int PIR_PIN = 2;
const int DHT_PIN = 3;
const int LED_PIN = 5;
const int LDR_PIN = A0;

#define DHTTYPE DHT11

DHT dht(DHT_PIN, DHTTYPE);

// =====================================================
// 6. TIMING CONFIGURATION
// =====================================================

// PIR hardware delay is approximately 10 seconds.
// Software timeout is 3 seconds for testing.

const unsigned long VACANCY_TIMEOUT = 3000UL;

const unsigned long PIR_WARMUP_MS = 60000UL;

const unsigned long DHT_INTERVAL = 2000UL;

const unsigned long MQTT_INTERVAL = 2000UL;

const unsigned long STATUS_INTERVAL = 2000UL;

const unsigned long WIFI_RETRY_INTERVAL = 15000UL;

const unsigned long MQTT_RETRY_INTERVAL = 5000UL;

// =====================================================
// 7. LIGHTING THRESHOLDS
// Previously calibrated using actual LDR readings.
// =====================================================

const int DARK_THRESHOLD = 8;
const int BRIGHT_THRESHOLD = 16;

bool occupied = false;
bool isDark = false;
bool lightingOn = false;

bool pirReady = false;

unsigned long pirWarmupStart = 0;
unsigned long lastMotionTime = 0;

// =====================================================
// 8. DHT11 SENSOR VARIABLES
// =====================================================

float actualTemperature = NAN;
float actualHumidity = NAN;

bool dhtValid = false;

unsigned long lastDHTReadTime = 0;

// =====================================================
// 9. HVAC THRESHOLDS
// PROVISIONAL: Revisit after experimentation.
// =====================================================

const float TEMP_HIGH_THRESHOLD = 31.0;
const float HUM_HIGH_THRESHOLD = 75.0;

// =====================================================
// 10. PHONE CONTROL VARIABLES
// =====================================================

bool simulationMode = false;  // Default: LIVE

String selectedScenario = "NORMAL";

int crowdLevel = 0;  // 0=None, 1=Low, 2=Medium, 3=High

// =====================================================
// 11. EFFECTIVE HVAC VARIABLES
// =====================================================

float effectiveTemperature = NAN;
float effectiveHumidity = NAN;

bool effectiveDataValid = false;

String environmentCategory = "UNKNOWN";

String hvacDetailed = "SENSOR_FAULT";

String hvacStandard = "SENSOR_FAULT";

// =====================================================
// 12. COMMUNICATION TIMERS
// =====================================================

unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

unsigned long lastMQTTPublish = 0;
unsigned long lastStatusPrint = 0;

// =====================================================
// 13. WIFI CONNECTION
// =====================================================

void startWiFi() {

  Serial.println("Starting WiFi connection...");

  WiFi.begin(ssid, pass);

  lastWiFiAttempt = millis();
}

// =====================================================
// 14. MQTT COMMAND SUBSCRIPTION
// =====================================================


void subscribeCommands() {

  Serial.println("Subscribing to command topics...");

  // Subscribe to all commands under this group topic
  mqttClient.subscribe("sitg7/room1/cmd/#");

  Serial.println("Command wildcard subscription registered!");
}


// =====================================================
// 15. PROCESS INCOMING MQTT MESSAGES
// =====================================================


void onMqttMessage(int messageSize) {

  (void)messageSize;

  String topic = mqttClient.messageTopic();

  char payload[48];
  int index = 0;

  while (mqttClient.available()) {

    char c = (char)mqttClient.read();

    if (index < sizeof(payload) - 1) {
      payload[index++] = c;
    }
  }

  payload[index] = '\0';

  String value = String(payload);

  topic.trim();
  value.trim();
  value.toUpperCase();

  // Display EVERY incoming MQTT message
  Serial.println();
  Serial.println("========== MQTT RECEIVED ==========");

  Serial.print("Actual topic: ");
  Serial.println(topic);

  Serial.print("Actual payload: ");
  Serial.println(value);

  // ---------------------------------
  // OPERATING MODE
  // ---------------------------------

  if (topic.endsWith("/cmd/mode")) {

    if (value == "LIVE") {

      simulationMode = false;
      Serial.println("Mode updated: LIVE");

    }

    else if (value == "SIMULATION") {

      simulationMode = true;
      Serial.println("Mode updated: SIMULATION");

    }

    else {

      Serial.println("ERROR: Invalid mode");

    }
  }

  // ---------------------------------
  // ENVIRONMENTAL SCENARIO
  // ---------------------------------

  else if (topic.endsWith("/cmd/scenario")) {

    if (value == "NORMAL" ||
        value == "HOT" ||
        value == "HUMID" ||
        value == "HOT_HUMID") {

      selectedScenario = value;

      Serial.print("Scenario updated: ");
      Serial.println(selectedScenario);

    }

    else {

      Serial.println("ERROR: Invalid scenario payload");

    }
  }

  // ---------------------------------
  // CROWD LEVEL
  // ---------------------------------

  else if (topic.endsWith("/cmd/crowd")) {

    if (value == "0" ||
        value == "1" ||
        value == "2" ||
        value == "3") {

      crowdLevel = value.toInt();

      Serial.print("Crowd level updated: ");
      Serial.println(crowdLevel);

    }

    else {

      Serial.println("ERROR: Invalid crowd payload");

    }
  }

  else {

    Serial.println("ERROR: Unrecognised command topic");

  }

  Serial.println("===================================");
}


// =====================================================
// 16. MAINTAIN WIFI AND MQTT CONNECTION
// =====================================================

void maintainNetwork(unsigned long now) {

  // Check WiFi

  if (WiFi.status() != WL_CONNECTED) {

    if (mqttClient.connected()) {
      mqttClient.stop();
    }

    if (now - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {

      startWiFi();

    }

    return;
  }

  // Check MQTT

  if (!mqttClient.connected()) {

    if (now - lastMQTTAttempt >= MQTT_RETRY_INTERVAL) {

      lastMQTTAttempt = now;

      Serial.println("Connecting to HiveMQ...");

      if (mqttClient.connect(BROKER, MQTT_PORT)) {

        Serial.println("MQTT connected!");

        subscribeCommands();

      }

      else {

        Serial.print("MQTT connection error: ");

        Serial.println(mqttClient.connectError());

      }
    }
  }

  else {

    mqttClient.poll();

  }
}

// =====================================================
// 17. READ DHT11 SENSOR
// =====================================================

void readDHT(unsigned long now) {

  if (now - lastDHTReadTime >= DHT_INTERVAL) {

    lastDHTReadTime = now;

    float h = dht.readHumidity();

    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {

      if (dhtValid) {
        Serial.println("DHT11 sensor fault detected!");
      }

      dhtValid = false;

    }

    else {

      if (!dhtValid) {
        Serial.println("DHT11 readings available.");
      }

      actualTemperature = t;

      actualHumidity = h;

      dhtValid = true;

    }
  }
}

// =====================================================
// 18. GET SIMULATED ENVIRONMENTAL READINGS
// =====================================================

void getSimulationReadings() {

  if (selectedScenario == "NORMAL") {

    effectiveTemperature = 25.0;
    effectiveHumidity = 55.0;

  }

  else if (selectedScenario == "HOT") {

    effectiveTemperature = 33.0;
    effectiveHumidity = 55.0;

  }

  else if (selectedScenario == "HUMID") {

    effectiveTemperature = 25.0;
    effectiveHumidity = 85.0;

  }

  else if (selectedScenario == "HOT_HUMID") {

    effectiveTemperature = 33.0;
    effectiveHumidity = 85.0;

  }
}

// =====================================================
// 19. HVAC DECISION LOGIC
// Based on the project decision table.
// =====================================================

String calculateHVAC(float temp, float hum, int crowd) {

  bool temperatureHigh =
    (temp >= TEMP_HIGH_THRESHOLD);

  bool humidityHigh =
    (hum >= HUM_HIGH_THRESHOLD);

  bool environmentHigh =
    temperatureHigh || humidityHigh;

  // No occupants: switch HVAC OFF

  if (crowd == 0) {

    return "OFF";

  }

  // Normal environmental conditions

  if (!environmentHigh) {

    if (crowd == 1) return "LOW";

    if (crowd == 2) return "LOW-MEDIUM";

    if (crowd == 3) return "MEDIUM";

  }

  // Special case from project decision table:
  // High crowd + high temperature + normal humidity

  if (crowd == 3 &&
      temperatureHigh &&
      !humidityHigh) {

    return "MEDIUM-HIGH";

  }

  // High environmental demand

  if (crowd == 1) return "MEDIUM";

  if (crowd == 2) return "HIGH";

  if (crowd == 3) return "HIGH";

  return "OFF";
}

// =====================================================
// 20. UPDATE EFFECTIVE READINGS AND HVAC
// =====================================================

void updateHVAC() {

  // Select actual or simulated readings

  if (simulationMode) {

    getSimulationReadings();

    effectiveDataValid = true;

  }

  else if (dhtValid) {

    effectiveTemperature = actualTemperature;

    effectiveHumidity = actualHumidity;

    effectiveDataValid = true;

  }

  else {

    effectiveDataValid = false;

    environmentCategory = "UNKNOWN";

    hvacDetailed = "SENSOR_FAULT";

    hvacStandard = "SENSOR_FAULT";

    return;
  }

  // Determine environmental category

  bool temperatureHigh =
    effectiveTemperature >= TEMP_HIGH_THRESHOLD;

  bool humidityHigh =
    effectiveHumidity >= HUM_HIGH_THRESHOLD;

  if (temperatureHigh || humidityHigh) {

    environmentCategory = "HIGH";

  }

  else {

    environmentCategory = "NORMAL";

  }

  // Calculate detailed HVAC setting

  hvacDetailed = calculateHVAC(
    effectiveTemperature,
    effectiveHumidity,
    crowdLevel
  );

  // Simplify intermediate settings for the
  // existing four-level MQTT dashboard interface.

  if (hvacDetailed == "LOW-MEDIUM") {

    hvacStandard = "MEDIUM";

  }

  else if (hvacDetailed == "MEDIUM-HIGH") {

    hvacStandard = "HIGH";

  }

  else {

    hvacStandard = hvacDetailed;

  }
}

// =====================================================
// 21. SMART LIGHTING SYSTEM
// =====================================================

void updateLighting(unsigned long now) {

  // PIR warm-up

  if (!pirReady &&
      now - pirWarmupStart >= PIR_WARMUP_MS) {

    pirReady = true;

    Serial.println("PIR sensor ready!");

    if (digitalRead(PIR_PIN) == HIGH) {

      occupied = true;

      lastMotionTime = now;

    }
  }

  // Occupancy detection

  if (pirReady) {

    int pirState = digitalRead(PIR_PIN);

    if (pirState == HIGH) {

      lastMotionTime = now;

      if (!occupied) {

        occupied = true;

        Serial.println("ROOM OCCUPIED");

      }
    }

    // Vacancy timer

    else if (occupied &&
             now - lastMotionTime >= VACANCY_TIMEOUT) {

      occupied = false;

      Serial.println("ROOM VACANT");

    }
  }

  // Read LDR

  int lightValue = analogRead(LDR_PIN);

  // Brightness hysteresis

  if (isDark && lightValue >= BRIGHT_THRESHOLD) {

    isDark = false;

    Serial.println("AMBIENT: BRIGHT");

  }

  else if (!isDark &&
           lightValue <= DARK_THRESHOLD) {

    isDark = true;

    Serial.println("AMBIENT: DARK");

  }

  // Lighting decision

  bool newLightingState = occupied && isDark;

  if (newLightingState != lightingOn) {

    lightingOn = newLightingState;

    Serial.print("LIGHTING: ");

    Serial.println(lightingOn ? "ON" : "OFF");

  }

  digitalWrite(
    LED_PIN,
    lightingOn ? HIGH : LOW
  );
}

// =====================================================
// 22. MQTT PUBLISHING FUNCTIONS
// =====================================================

void publishText(const char* topic, String value) {

  if (!mqttClient.connected()) return;

  mqttClient.beginMessage(topic);

  mqttClient.print(value);

  mqttClient.endMessage();
}

void publishNumber(const char* topic, float value) {

  if (!mqttClient.connected()) return;

  mqttClient.beginMessage(topic);

  mqttClient.print(value, 1);

  mqttClient.endMessage();
}

// =====================================================
// 23. PUBLISH SYSTEM INFORMATION
// =====================================================

void publishTelemetry() {

  if (!mqttClient.connected()) return;

  // Actual DHT11 readings

  if (dhtValid) {

    publishNumber(TOPIC_ACTUAL_TEMP, actualTemperature);

    publishNumber(TOPIC_ACTUAL_HUM, actualHumidity);

  }

  else {

    publishText(TOPIC_ACTUAL_TEMP, "ERROR");

    publishText(TOPIC_ACTUAL_HUM, "ERROR");

  }

  // Effective HVAC readings

  if (effectiveDataValid) {

    publishNumber(TOPIC_TEMP, effectiveTemperature);

    publishNumber(TOPIC_HUM, effectiveHumidity);

  }

  else {

    publishText(TOPIC_TEMP, "ERROR");

    publishText(TOPIC_HUM, "ERROR");

  }

  // Other system information

  publishText(TOPIC_CROWD, String(crowdLevel));

  publishText(TOPIC_HVAC, hvacStandard);

  publishText(TOPIC_HVAC_DETAIL, hvacDetailed);

  publishText(TOPIC_LIGHTING, lightingOn ? "ON" : "OFF");

  publishText(TOPIC_MODE,
              simulationMode ? "SIMULATION" : "LIVE");

  publishText(TOPIC_SCENARIO, selectedScenario);

  publishText(TOPIC_AMBIENT, isDark ? "DARK" : "BRIGHT");

  publishText(TOPIC_DHT_STATUS, dhtValid ? "NORMAL" : "FAULT");

  publishText(TOPIC_OCCUPANCY, occupied ? "OCCUPIED" : "VACANT");
}

// =====================================================
// 24. SERIAL MONITOR STATUS
// =====================================================

void printStatus() {

  Serial.println();
  Serial.println("========== ENERGY STATUS ==========");

  Serial.print("Mode: ");

  Serial.println(simulationMode ? "SIMULATION" : "LIVE");

  Serial.print("Scenario: ");

  Serial.println(selectedScenario);

  Serial.print("Crowd Level: ");

  Serial.println(crowdLevel);

  Serial.println("-----------------------------------");

  Serial.print("Occupancy: ");

  Serial.println(occupied ? "OCCUPIED" : "VACANT");

  Serial.print("Ambient: ");

  Serial.println(isDark ? "DARK" : "BRIGHT");

  Serial.print("Lighting: ");

  Serial.println(lightingOn ? "ON" : "OFF");

  Serial.println("-----------------------------------");

  if (dhtValid) {

    Serial.print("Actual Temperature: ");

    Serial.println(actualTemperature);

    Serial.print("Actual Humidity: ");

    Serial.println(actualHumidity);

  }

  else {

    Serial.println("Actual DHT11: FAULT");

  }

  if (effectiveDataValid) {

    Serial.print("HVAC Temperature: ");

    Serial.println(effectiveTemperature);

    Serial.print("HVAC Humidity: ");

    Serial.println(effectiveHumidity);

    Serial.print("Environment: ");

    Serial.println(environmentCategory);

  }

  else {

    Serial.println("HVAC input unavailable");

  }

  Serial.print("HVAC Detailed Setting: ");

  Serial.println(hvacDetailed);

  Serial.print("HVAC Standard Setting: ");

  Serial.println(hvacStandard);

  Serial.println("===================================");
}

// =====================================================
// 25. SETUP
// =====================================================

void setup() {

  Serial.begin(9600);

  pinMode(PIR_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  dht.begin();

  // Initialise MQTT

  mqttClient.setId(CLIENT_ID);

  mqttClient.setConnectionTimeout(5000);

  mqttClient.onMessage(onMqttMessage);

  // Initialise sensor states

  isDark = (analogRead(LDR_PIN) <= DARK_THRESHOLD);

  pirWarmupStart = millis();

  Serial.println("==============================");

  Serial.println(" SMART CAMPUS ENERGY SYSTEM");

  Serial.println("==============================");

  Serial.println("Lighting system initialising...");

  Serial.println("DHT11 initialising...");

  Serial.println("Default mode: LIVE");

  Serial.println("Default crowd: NONE");

  // Start WiFi without blocking local operation

  startWiFi();

  Serial.println("System initialised!");

  Serial.println("PIR warming up...");

}

// =====================================================
// 26. MAIN LOOP
// =====================================================

void loop() {

  unsigned long currentTime = millis();

  // Maintain MQTT communication

  maintainNetwork(currentTime);

  // Read DHT11

  readDHT(currentTime);

  // Maintain lighting locally

  updateLighting(currentTime);

  // Calculate HVAC

  updateHVAC();

  // Publish telemetry every 2 seconds

  if (currentTime - lastMQTTPublish >= MQTT_INTERVAL) {

    lastMQTTPublish = currentTime;

    if (mqttClient.connected()) {

      publishTelemetry();

    }
  }

  // Print system status every 2 seconds

  if (currentTime - lastStatusPrint >= STATUS_INTERVAL) {

    lastStatusPrint = currentTime;

    printStatus();

  }
}
