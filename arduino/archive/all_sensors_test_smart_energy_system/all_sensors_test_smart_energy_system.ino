
#include <DHT.h>

// ==========================================
// SMART CAMPUS ENERGY SYSTEM
// Arduino Uno WiFi Rev2
// ==========================================

// PIN ALLOCATION
const int PIR_PIN = 2;
const int DHT_PIN = 3;
const int LED_PIN = 5;
const int LDR_PIN = A0;

// SENSOR CONFIGURATION
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ==========================================
// LIGHTING CONFIGURATION
// ==========================================

// Vacancy timer: 3 seconds for testing
const unsigned long VACANCY_TIMEOUT = 3000UL;

// Calibrated LDR thresholds
const int DARK_THRESHOLD = 8;
const int BRIGHT_THRESHOLD = 16;

// ==========================================
// DHT11 CONFIGURATION
// ==========================================

const unsigned long DHT_INTERVAL = 2000UL;

float temperature = NAN;
float humidity = NAN;

bool dhtFault = false;

// ==========================================
// SYSTEM STATES
// ==========================================

bool occupied = false;
bool isDark = false;
bool lightingOn = false;

// Timing variables
unsigned long lastMotionTime = 0;
unsigned long lastDHTReadTime = 0;
unsigned long lastStatusTime = 0;

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  dht.begin();

  Serial.println("==============================");
  Serial.println(" SMART CAMPUS ENERGY SYSTEM");
  Serial.println("==============================");

  Serial.println("PIR warming up...");

  delay(60000);

  // Read initial sensor states
  int initialPIR = digitalRead(PIR_PIN);
  int initialLDR = analogRead(LDR_PIN);

  occupied = (initialPIR == HIGH);
  lastMotionTime = millis();

  isDark = (initialLDR <= DARK_THRESHOLD);

  Serial.println("PIR Ready!");
  Serial.println("LDR Ready!");
  Serial.println("DHT11 Ready!");
  Serial.println("------------------------------");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  unsigned long currentTime = millis();

  // ----------------------------------------
  // 1. READ PIR SENSOR
  // ----------------------------------------

  int pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH) {

    lastMotionTime = currentTime;

    if (!occupied) {

      occupied = true;

      Serial.println("ROOM OCCUPIED");
    }
  }

  // ----------------------------------------
  // 2. VACANCY TIMER
  // ----------------------------------------

  else if (occupied &&
           currentTime - lastMotionTime >= VACANCY_TIMEOUT) {

    occupied = false;

    Serial.println("ROOM VACANT");
  }

  // ----------------------------------------
  // 3. READ LDR SENSOR
  // ----------------------------------------

  int lightValue = analogRead(LDR_PIN);

  // Hysteresis
  if (isDark && lightValue >= BRIGHT_THRESHOLD) {

    isDark = false;

    Serial.println("AMBIENT: BRIGHT");
  }

  else if (!isDark && lightValue <= DARK_THRESHOLD) {

    isDark = true;

    Serial.println("AMBIENT: DARK");
  }

  // ----------------------------------------
  // 4. SMART LIGHTING DECISION
  // ----------------------------------------

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

  digitalWrite(LED_PIN, lightingOn ? HIGH : LOW);

  // ----------------------------------------
  // 5. READ DHT11 SENSOR
  // ----------------------------------------

  if (currentTime - lastDHTReadTime >= DHT_INTERVAL) {

    lastDHTReadTime = currentTime;

    float newHumidity = dht.readHumidity();
    float newTemperature = dht.readTemperature();

    // Check for invalid readings
    if (isnan(newHumidity) || isnan(newTemperature)) {

      if (!dhtFault) {
        Serial.println("DHT11 FAULT: Invalid reading!");
      }

      dhtFault = true;
    }

    else {

      if (dhtFault) {
        Serial.println("DHT11 readings restored.");
      }

      dhtFault = false;

      humidity = newHumidity;
      temperature = newTemperature;
    }
  }

  // ----------------------------------------
  // 6. SYSTEM STATUS
  // ----------------------------------------

  if (currentTime - lastStatusTime >= 2000UL) {

    lastStatusTime = currentTime;

    Serial.println();
    Serial.println("========== SYSTEM STATUS ==========");

    Serial.print("Occupancy: ");
    Serial.println(occupied ? "OCCUPIED" : "VACANT");

    Serial.print("Ambient: ");
    Serial.println(isDark ? "DARK" : "BRIGHT");

    Serial.print("LDR Reading: ");
    Serial.println(lightValue);

    Serial.print("Lighting: ");
    Serial.println(lightingOn ? "ON" : "OFF");

    Serial.println("-----------------------------------");

    if (!dhtFault) {

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      Serial.println("DHT11 Status: NORMAL");

    }

    else {

      Serial.println("Temperature: ERROR");
      Serial.println("Humidity: ERROR");
      Serial.println("DHT11 Status: FAULT");

    }

    Serial.println("===================================");
  }

  delay(50);
}
