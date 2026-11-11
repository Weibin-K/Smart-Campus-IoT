/*
  SIT Smart Campus - send crowd and energy readings to the group's Apps Script (ESP32)
  -----------------------------------------------------------------------------------
  For the crowd board, the PIR lighting board and the fan/HVAC board. Copy postCrowd() or
  postEnergy() into your own sketch, or run this one as it is to test the website end to end.

  The Apps Script (with LiveApi.gs added) takes:
    {"api_key":"...","type":"crowd","facility_id":"FCA","current_occupancy":42,"capacity":60}
    {"api_key":"...","type":"energy","facility_id":"FCA","light_on":true,"fan_pct":70,"hvac":"HIGH"}
  Send only the parts your board knows. The map reads them back through ?action=latest.

  This demo: PIR on GPIO 27 switches the light (LED on GPIO 2) and is sent as light_on.
  Type in the Serial Monitor (115200 baud, "Newline"):
    c 12     -> post a crowd count of 12
    f 70     -> post fan 70 %
    h HIGH   -> post HVAC level (OFF, LOW, MEDIUM, HIGH)

  Board: any ESP32 (Arduino IDE: Tools > Board > ESP32 Dev Module). No extra libraries.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---------- change these ----------
const char WIFI_SSID[] = "your-wifi-name";
const char WIFI_PASS[] = "your-wifi-password";
const char SCRIPT_URL[] = "https://script.google.com/macros/s/AKfycbzlVsZVIWkLt818jMaaC5FyiXxicgMHUnufDmqna2tomFhcsaretr3Tu6svYqhBWqiJOQ/exec";
const char API_KEY[] = "YOUR_ENV_API_KEY";     // the ENV_API_KEY script property
const char FACILITY_ID[] = "FCA";              // must be in the Facilities tab
const int CAPACITY = 60;                       // people the place holds (0 = use the Facilities tab)
// ----------------------------------

const int PIR_PIN = 27;
const int LIGHT_PIN = 2;
const unsigned long LIGHT_HOLD_MS = 30000;     // light stays on 30 s after the last movement
const unsigned long MIN_GAP_MS = 15000;        // at most one post per 15 s per type

bool lightOn = false;
unsigned long lastMotion = 0;
unsigned long lastEnergyPost = 0;
bool energyDirty = true;
int fanPct = 0;
String hvac = "OFF";

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(" connected, IP ");
  Serial.println(WiFi.localIP());
}

// POST one JSON body to the Apps Script and print its answer
bool postJson(const String &body) {
  connectWiFi();
  WiFiClientSecure client;
  client.setInsecure();                                  // skip certificate checks (fine for a class demo)
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Apps Script answers through a redirect
  http.setTimeout(15000);
  http.begin(client, SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String reply = http.getString();
  http.end();
  Serial.print("POST ");
  Serial.print(code);
  Serial.print(" ");
  Serial.println(reply);                                 // {"success":true,...} when it worked
  return code == 200 && reply.indexOf("\"success\":true") >= 0;
}

bool postCrowd(int peopleInside) {
  String b = String("{\"api_key\":\"") + API_KEY + "\",\"type\":\"crowd\",\"facility_id\":\"" + FACILITY_ID +
             "\",\"current_occupancy\":" + peopleInside;
  if (CAPACITY > 0) {
    b += ",\"capacity\":";
    b += CAPACITY;
  }
  b += "}";
  return postJson(b);
}

bool postEnergy(bool light, int fan, const String &hvacLevel) {
  String b = String("{\"api_key\":\"") + API_KEY + "\",\"type\":\"energy\",\"facility_id\":\"" + FACILITY_ID +
             "\",\"light_on\":" + (light ? "true" : "false") +
             ",\"fan_pct\":" + fan + ",\"hvac\":\"" + hvacLevel + "\"}";
  return postJson(b);
}

void readSerial() {
  if (!Serial.available()) {
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.startsWith("c ")) {
    postCrowd(line.substring(2).toInt());
  } else if (line.startsWith("f ")) {
    fanPct = constrain(line.substring(2).toInt(), 0, 100);
    energyDirty = true;
  } else if (line.startsWith("h ")) {
    String h = line.substring(2);
    h.toUpperCase();
    if (h == "OFF" || h == "LOW" || h == "MEDIUM" || h == "HIGH") {
      hvac = h;
      energyDirty = true;
    } else {
      Serial.println("HVAC must be OFF, LOW, MEDIUM or HIGH");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  connectWiFi();
  Serial.println("Ready. Try: c 12   f 70   h HIGH");
}

void loop() {
  unsigned long now = millis();
  readSerial();

  if (digitalRead(PIR_PIN) == HIGH) {
    lastMotion = now;
    if (!lightOn) {
      lightOn = true;
      energyDirty = true;
    }
  } else if (lightOn && now - lastMotion > LIGHT_HOLD_MS) {
    lightOn = false;
    energyDirty = true;
  }
  digitalWrite(LIGHT_PIN, lightOn ? HIGH : LOW);

  // post when something changed (but not more than every 15 s), and every 5 min anyway
  bool due = energyDirty && now - lastEnergyPost > MIN_GAP_MS;
  if (due || now - lastEnergyPost > 300000) {
    if (postEnergy(lightOn, fanPct, hvac)) {
      energyDirty = false;
    }
    lastEnergyPost = now;
  }
}
