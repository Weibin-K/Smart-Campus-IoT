# Arduino boards

All boards are Arduino Uno WiFi Rev2 (Boards Manager: "Arduino megaAVR Boards").
In the Arduino IDE, open the `.ino` inside its folder (the folder and file name must match).

| Folder | Board | Status |
|---|---|---|
| `Board1_Sensor_Node/` | Board 1: senses and reports | current |
| `Board2_Energy_Saving/` | Board 2: decides and drives the LED and fan | current |
| `crowd_radar_counter/` | Doorway people counter (LD2450 radar) | current, still sends to ThingSpeak |
| `examples/live_post_esp32/` | Example: post crowd and energy readings from an ESP32 | reference |
| `archive/` | Older single-board and Energy Saving sketches | do not upload |

## Pins
| Board 1 pin | Part | | Board 2 pin | Part |
|---|---|---|---|---|
| A0 | DHT11 | | D5 | LED (through a resistor) |
| A1 | Sound sensor | | D9 | Fan signal (PWM), through a transistor or fan module |
| A2 | Light sensor (LDR divider) | | built-in LED | on while connected to the broker |
| D2 | PIR | | | |
| D4 | Button (location menu) | | | |
| SDA / SCL | RGB LCD | | | |

## Libraries (Library Manager)
| Library | Board 1 | Board 2 |
|---|---|---|
| WiFiNINA | yes | yes |
| ArduinoMqttClient | yes | yes |
| DFRobot_RGBLCD1602 | yes | |
| dht (the lab `dht.h` library) | yes | |

## How the two boards talk (broker.hivemq.com, port 1883)
```
Board 1 -> Board 2 :  sitg7/room1/  temp, hum, occupancy, ldr, crowd
Board 2 -> Board 1 :  sitg7/room1/energy/  hvac, lighting, ambient, fan
Phone   -> Board 2 :  sitg7/room1/cmd/  mode, scenario, crowd
```
- Board 2 switches the LED on only when the room is occupied and dark, and sets the fan from the HVAC level.
- LIVE mode: crowd level comes from Board 1 (typed in its Serial Monitor). SIMULATION mode: from the phone.
- If Board 2 hears nothing from Board 1 for 20 s, it switches the LED and fan off.
- If Board 1 hears nothing from Board 2 for 10 s, its LCD shows `Board 2: no link`.

## Before uploading
Fill in the hotspot password in both sketches and the API key in Board 1 (search for `YOUR_`).
Each board needs its own MQTT client ID, and only one copy of each sketch should run at a time.
