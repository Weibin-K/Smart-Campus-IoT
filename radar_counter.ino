/*
  SIT Campus Wayfinder - doorway people counter with an HLK-LD2450 radar
  ----------------------------------------------------------------------
  Board:    Arduino UNO WiFi Rev2
  Radar:    HLK-LD2450 24 GHz mmWave, tracks up to 3 people (x, y in mm), 10 times a second
  Also:     DHT22 temperature and humidity sensor, reset button
  Sends:    to ThingSpeak every 20 s when the count changes (and every minute anyway)
              field 1 = people inside   <- the campus map reads this one
              field 2 = total walked in
              field 3 = total walked out
              field 4 = temperature (degrees C)
              field 5 = humidity (%)
  Same fields as the IR-beam counter, so the map works with either.

  How it counts:
    Mount the radar inside the room, above or beside the doorway, facing into the room.
    y = distance straight out from the radar. An invisible line runs across the room at LINE_MM.
    A person whose y goes from below the line to above it walked IN; above to below walked OUT.
    HYST_MM stops someone standing on the line from being counted again and again.

  Libraries (Arduino IDE > Tools > Manage Libraries):
    WiFiNINA                 by Arduino
    ThingSpeak               by MathWorks
    DHT sensor library       by Adafruit   (and Adafruit Unified Sensor)

  Wiring:
    LD2450 5V  -> 5V           LD2450 GND -> GND
    LD2450 TX  -> D0 (RX)      through a 3.3 V -> 5 V level shifter (the radar talks at 3.3 V)
    LD2450 RX  -> not needed   (the sketch only listens)
    DHT22 module: + -> 5V, - -> GND, out -> D2
    reset button: one leg -> D8, other leg -> GND
  D0/D1 are Serial1 on this board, separate from the USB Serial Monitor.
  Unplug the radar's TX wire while uploading if the upload fails.
*/

#include <WiFiNINA.h>
#include "ThingSpeak.h"
#include "DHT.h"

// ---------- change these ----------
const char WIFI_SSID[] = "your-wifi-name";
const char WIFI_PASS[] = "your-wifi-password";
const unsigned long CHANNEL_ID = 0;              // your ThingSpeak channel ID
const char WRITE_API_KEY[] = "YOUR_WRITE_KEY";   // ThingSpeak > channel > API Keys > Write API Key
const int LINE_MM = 1500;   // the counting line, mm out from the radar (set it about 1 m inside the door)
const int HYST_MM = 200;    // how far past the line someone must go to count
// ----------------------------------

const int RESET_BTN = 8;
const int DHT_PIN = 2;

const unsigned long SEND_EVERY_MS = 20000;   // ThingSpeak free plan: at most 1 update per 15 s
const unsigned long HEARTBEAT_MS = 60000;    // send at least every minute
const unsigned long CLIMATE_EVERY_MS = 30000;
const unsigned long LOST_MS = 600;           // a person not seen for this long has left the radar's view
const long MATCH_MM = 700;                   // a new position this close to a tracked person is the same person

WiFiClient client;
DHT dht(DHT_PIN, DHT22);

int peopleInside = 0;
long totalIn = 0;
long totalOut = 0;
float temperature = NAN;
float humidity = NAN;

unsigned long lastSend = 0;
unsigned long lastClimate = 0;
bool sentOnce = false;
int lastSentPeople = -1;

// one radar frame is 30 bytes: AA FF 03 00, 3 targets x 8 bytes, 55 CC
byte frame[30];
int frameLen = 0;

// the people being followed (the radar reports at most 3)
const int NEAR_SIDE = 0;    // between the radar and the line
const int FAR_SIDE = 1;     // beyond the line
const int ON_LINE = 2;      // inside the hysteresis band
struct Track {
  bool used;
  int x;
  int y;
  int side;                 // NEAR_SIDE or FAR_SIDE once known
  unsigned long seen;
};
Track tracks[3];


int sideOf(int y) {
  if (y < LINE_MM - HYST_MM) {
    return NEAR_SIDE;
  }
  if (y > LINE_MM + HYST_MM) {
    return FAR_SIDE;
  }
  return ON_LINE;
}

// the LD2450 sends each value low byte first; the top bit set means positive
int decode(byte lo, byte hi) {
  int magnitude = ((hi & 0x7F) << 8) | lo;
  if (hi & 0x80) {
    return magnitude;
  }
  return -magnitude;
}

void connectWiFi() {
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    delay(5000);
  }
  Serial.print("WiFi connected, IP ");
  Serial.println(WiFi.localIP());
}

void sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  ThingSpeak.setField(1, peopleInside);
  ThingSpeak.setField(2, totalIn);
  ThingSpeak.setField(3, totalOut);
  if (!isnan(temperature)) {
    ThingSpeak.setField(4, temperature);
  }
  if (!isnan(humidity)) {
    ThingSpeak.setField(5, humidity);
  }
  int code = ThingSpeak.writeFields(CHANNEL_ID, WRITE_API_KEY);
  if (code == 200) {
    lastSentPeople = peopleInside;
    Serial.print("Sent to ThingSpeak: ");
    Serial.print(peopleInside);
    Serial.println(" inside");
  } else {
    Serial.print("ThingSpeak error ");
    Serial.println(code);   // 404: wrong key, -401: sent too soon, -301: no connection
  }
  lastSend = millis();
  sentOnce = true;
}

// a tracked person moved to (x, y): count them if they crossed the line
void moveTrack(int t, int x, int y, unsigned long now) {
  int s = sideOf(y);
  if (s != ON_LINE) {
    if (tracks[t].side == NEAR_SIDE && s == FAR_SIDE) {
      peopleInside = peopleInside + 1;
      totalIn = totalIn + 1;
      Serial.print("IN   inside now: ");
      Serial.println(peopleInside);
    } else if (tracks[t].side == FAR_SIDE && s == NEAR_SIDE) {
      if (peopleInside > 0) {
        peopleInside = peopleInside - 1;
      }
      totalOut = totalOut + 1;
      Serial.print("OUT  inside now: ");
      Serial.println(peopleInside);
    }
    tracks[t].side = s;
  }
  tracks[t].x = x;
  tracks[t].y = y;
  tracks[t].seen = now;
}

// match this frame's people to the ones already followed (nearest first), start following new ones
void handleFrame(unsigned long now) {
  for (int i = 0; i < 3; i++) {
    int b = 4 + i * 8;
    int x = decode(frame[b], frame[b + 1]);
    int y = decode(frame[b + 2], frame[b + 3]);
    if (x == 0 && y == 0) {
      continue;                                // empty slot: nobody
    }
    int best = -1;
    long bestDist = MATCH_MM;
    for (int t = 0; t < 3; t++) {
      if (tracks[t].used && tracks[t].seen != now) {
        long dx = x - tracks[t].x;
        long dy = y - tracks[t].y;
        long d = abs(dx) + abs(dy);
        if (d < bestDist) {
          bestDist = d;
          best = t;
        }
      }
    }
    if (best >= 0) {
      moveTrack(best, x, y, now);
    } else {
      for (int t = 0; t < 3; t++) {
        if (!tracks[t].used) {                 // someone new: note which side they appeared on
          tracks[t].used = true;
          tracks[t].x = x;
          tracks[t].y = y;
          tracks[t].side = sideOf(y);
          tracks[t].seen = now;
          break;
        }
      }
    }
  }
  for (int t = 0; t < 3; t++) {
    if (tracks[t].used && now - tracks[t].seen > LOST_MS) {
      tracks[t].used = false;                  // out of view
    }
  }
}

// collect bytes from the radar until a whole frame (header ... tail) has arrived
void readRadar() {
  while (Serial1.available()) {
    byte c = Serial1.read();
    const byte header[4] = {0xAA, 0xFF, 0x03, 0x00};
    if (frameLen < 4) {
      if (c == header[frameLen]) {
        frame[frameLen] = c;
        frameLen = frameLen + 1;
      } else {
        frameLen = (c == 0xAA) ? 1 : 0;
        frame[0] = c;
      }
      continue;
    }
    frame[frameLen] = c;
    frameLen = frameLen + 1;
    if (frameLen == 30) {
      if (frame[28] == 0x55 && frame[29] == 0xCC) {
        handleFrame(millis());
      }
      frameLen = 0;
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial1.begin(256000);           // the LD2450's speed
  pinMode(RESET_BTN, INPUT_PULLUP);
  dht.begin();
  for (int t = 0; t < 3; t++) {
    tracks[t].used = false;
  }
  connectWiFi();
  ThingSpeak.begin(client);
  Serial.println("Radar counter ready");
}

void loop() {
  unsigned long now = millis();
  readRadar();

  if (digitalRead(RESET_BTN) == LOW) {
    peopleInside = 0;
    Serial.println("Count reset to 0");
    delay(300);
  }

  // frames that arrive during a DHT22 read or a ThingSpeak upload are dropped; walking through the radar's
  // view takes 1-2 s, so a crossing is still seen in the frames before and after
  if (now - lastClimate > CLIMATE_EVERY_MS) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
    }
    lastClimate = now;
  }

  bool changed = peopleInside != lastSentPeople;
  unsigned long sinceSend = now - lastSend;
  if (!sentOnce || (changed && sinceSend > SEND_EVERY_MS) || sinceSend > HEARTBEAT_MS) {
    sendToThingSpeak();
  }
}
