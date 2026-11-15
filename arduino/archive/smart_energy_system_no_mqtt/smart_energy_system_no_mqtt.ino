
#include <DHT.h>

// =================================================
// 1. PIN CONFIGURATION
// =================================================

const int PIR_PIN = 2;
const int DHT_PIN = 3;
const int LED_PIN = 5;
const int LDR_PIN = A0;

#define DHTTYPE DHT11

DHT dht(DHT_PIN, DHTTYPE);

// =================================================
// 2. LIGHTING CONFIGURATION
// =================================================

// Vacancy timer: 3 seconds for testing
const unsigned long VACANCY_TIMEOUT = 3000UL;

// Your experimentally calibrated LDR thresholds
const int DARK_THRESHOLD = 12;
const int BRIGHT_THRESHOLD = 30;

// =================================================
// 3. HVAC CONFIGURATION
// =================================================

// Provisional thresholds; subject to validation
const float TEMP_HIGH_THRESHOLD = 31.0;
const float HUM_HIGH_THRESHOLD = 75.0;

// Manually assigned crowd level:
// 0 = None
// 1 = Low
// 2 = Medium
// 3 = High

int crowdLevel = 2;

// =================================================
// 4. SENSOR TIMING
// =================================================

const unsigned long PIR_WARMUP = 60000UL;
const unsigned long DHT_INTERVAL = 2000UL;
const unsigned long STATUS_INTERVAL = 2000UL;

// =================================================
// 5. SYSTEM VARIABLES
// =================================================

// Occupancy and lighting
bool occupied = false;
bool pirReady = false;
bool isDark = false;
bool lightingOn = false;

unsigned long lastMotionTime = 0;
unsigned long lastDHTReadTime = 0;
unsigned long lastStatusTime = 0;

// DHT11 readings
float temperature = NAN;
float humidity = NAN;

bool dhtValid = false;

// HVAC
String environmentCategory = "UNKNOWN";
String hvacDetailed = "SENSOR_FAULT";
String hvacStandard = "SENSOR_FAULT";

// =================================================
// 6. SETUP
// =================================================

void setup() {

  Serial.begin(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  dht.begin();

  Serial.println();
  Serial.println("====================================");
  Serial.println(" SMART CAMPUS ENERGY SYSTEM");
  Serial.println(" STANDALONE SENSOR TEST");
  Serial.println("====================================");

  Serial.println("PIR initialising...");
  Serial.println("DHT11 initialising...");

  // Allow the PIR to stabilise
  delay(PIR_WARMUP);

  pirReady = true;

  // Initialise sensor states
  int initialPIR = digitalRead(PIR_PIN);
  int initialLDR = analogRead(LDR_PIN);

  occupied = (initialPIR == HIGH);

  lastMotionTime = millis();

  isDark = (initialLDR <= DARK_THRESHOLD);

  Serial.println("PIR Ready!");
  Serial.println("LDR Ready!");
  Serial.println("DHT11 Ready!");

  Serial.println("------------------------------------");

  Serial.println("SYSTEM READY");
  Serial.println("No MQTT / No WiFi");
  Serial.println("------------------------------------");
}

// =================================================
// 7. PIR OCCUPANCY DETECTION
// =================================================

void updateOccupancy() {

  int pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH) {

    lastMotionTime = millis();

    if (!occupied) {

      occupied = true;

      Serial.println("ROOM OCCUPIED");

    }
  }

  else if (occupied &&
           millis() - lastMotionTime >= VACANCY_TIMEOUT) {

    occupied = false;

    Serial.println("ROOM VACANT");

  }
}

// =================================================
// 8. LDR BRIGHTNESS DETECTION
// =================================================

int updateBrightness() {

  int lightValue = analogRead(LDR_PIN);

  // Hysteresis to prevent rapid switching

  if (isDark && lightValue >= BRIGHT_THRESHOLD) {

    isDark = false;

    Serial.println("AMBIENT: BRIGHT");

  }

  else if (!isDark &&
           lightValue <= DARK_THRESHOLD) {

    isDark = true;

    Serial.println("AMBIENT: DARK");

  }

  return lightValue;
}

// =================================================
// 9. SMART LIGHTING DECISION
// =================================================

void updateLighting() {

  bool newLightingState = occupied && isDark;

  if (newLightingState != lightingOn) {

    lightingOn = newLightingState;

    if (lightingOn) {

      Serial.println("LIGHTING: ON");

    }

    else {

      Serial.println("LIGHTING: OFF");

    }
  }

  digitalWrite(
    LED_PIN,
    lightingOn ? HIGH : LOW
  );
}

// =================================================
// 10. DHT11 TEMPERATURE AND HUMIDITY
// =================================================

void readDHT() {

  unsigned long currentTime = millis();

  if (currentTime - lastDHTReadTime >= DHT_INTERVAL) {

    lastDHTReadTime = currentTime;

    float newHumidity = dht.readHumidity();
    float newTemperature = dht.readTemperature();

    // Sensor fault detection

    if (isnan(newHumidity) || isnan(newTemperature)) {

      if (dhtValid) {
        Serial.println("DHT11 FAULT DETECTED!");
      }

      dhtValid = false;

    }

    else {

      if (!dhtValid) {
        Serial.println("DHT11 READINGS RESTORED");
      }

      temperature = newTemperature;
      humidity = newHumidity;

      dhtValid = true;

    }
  }
}

// =================================================
// 11. HVAC DECISION LOGIC
// =================================================

String calculateHVAC() {

  if (!dhtValid) {

    return "SENSOR_FAULT";

  }

  bool temperatureHigh =
    temperature >= TEMP_HIGH_THRESHOLD;

  bool humidityHigh =
    humidity >= HUM_HIGH_THRESHOLD;

  bool environmentHigh =
    temperatureHigh || humidityHigh;

  // No occupants

  if (crowdLevel == 0) {

    return "OFF";

  }

  // Normal environmental conditions

  if (!environmentHigh) {

    if (crowdLevel == 1) return "LOW";

    if (crowdLevel == 2) return "LOW-MEDIUM";

    if (crowdLevel == 3) return "MEDIUM";

  }

  // Special case:
  // High crowd, high temperature,
  // normal humidity

  if (crowdLevel == 3 &&
      temperatureHigh &&
      !humidityHigh) {

    return "MEDIUM-HIGH";

  }

  // High environmental demand

  if (crowdLevel == 1) return "MEDIUM";

  if (crowdLevel == 2) return "HIGH";

  if (crowdLevel == 3) return "HIGH";

  return "OFF";
}

// =================================================
// 12. ENVIRONMENT CLASSIFICATION
// =================================================

void updateHVAC() {

  if (!dhtValid) {

    environmentCategory = "UNKNOWN";

    hvacDetailed = "SENSOR_FAULT";

    hvacStandard = "SENSOR_FAULT";

    return;
  }

  bool temperatureHigh =
    temperature >= TEMP_HIGH_THRESHOLD;

  bool humidityHigh =
    humidity >= HUM_HIGH_THRESHOLD;

  if (temperatureHigh || humidityHigh) {

    environmentCategory = "HIGH";

  }

  else {

    environmentCategory = "NORMAL";

  }

  hvacDetailed = calculateHVAC();

  // Convert intermediate settings to
  // the standard four-level output.

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

// =================================================
// 13. DISPLAY ALL SENSOR READINGS
// =================================================

void printStatus(int lightValue) {

  Serial.println();
  Serial.println("========== SENSOR STATUS ==========");

  // PIR
  Serial.print("PIR: ");

  if (!pirReady) {

    Serial.println("WARMING UP");

  }

  else {

    Serial.println(
      digitalRead(PIR_PIN) == HIGH ? "MOTION" : "NO MOTION"
    );

  }

  Serial.print("Occupancy: ");

  Serial.println(occupied ? "OCCUPIED" : "VACANT");

  // LDR
  Serial.print("LDR Raw Reading: ");

  Serial.println(lightValue);

  Serial.print("Ambient: ");

  Serial.println(isDark ? "DARK" : "BRIGHT");

  // LED
  Serial.print("Lighting: ");

  Serial.println(lightingOn ? "ON" : "OFF");

  Serial.println("------------------------------------");

  // DHT11
  if (dhtValid) {

    Serial.print("Temperature: ");

    Serial.print(temperature, 1);

    Serial.println(" C");

    Serial.print("Humidity: ");

    Serial.print(humidity, 1);

    Serial.println(" %");

    Serial.println("DHT11 Status: NORMAL");

  }

  else {

    Serial.println("Temperature: ERROR");

    Serial.println("Humidity: ERROR");

    Serial.println("DHT11 Status: FAULT");

  }

  Serial.println("------------------------------------");

  // HVAC
  Serial.print("Crowd Level: ");

  Serial.println(crowdLevel);

  Serial.print("Environment: ");

  Serial.println(environmentCategory);

  Serial.print("HVAC Detailed: ");

  Serial.println(hvacDetailed);

  Serial.print("HVAC Standard: ");

  Serial.println(hvacStandard);

  Serial.println("====================================");
}

// =================================================
// 14. MAIN LOOP
// =================================================

void loop() {

  // 1. PIR
  updateOccupancy();

  // 2. LDR
  int lightValue = updateBrightness();

  // 3. LED
  updateLighting();

  // 4. DHT11
  readDHT();

  // 5. HVAC
  updateHVAC();

  // 6. Display all readings
  unsigned long currentTime = millis();

  if (currentTime - lastStatusTime >= STATUS_INTERVAL) {

    lastStatusTime = currentTime;

    printStatus(lightValue);

  }

  delay(50);
}
