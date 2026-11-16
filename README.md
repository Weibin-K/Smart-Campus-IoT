# SIT Smart Campus IoT (Group 7)

A campus map with **directions and live crowd, environment and energy data**, hosted on GitHub Pages:
https://weibin-k.github.io/Smart-Campus-IoT/

```
Crowd board (radar) ------------------POST--> Apps Script --> Google Sheet
Board 1 sensor node ------------------POST-->      |
   ^  |  MQTT (HiveMQ)                  GET ?action=latest (JSON)
   |  v                                            v
Board 2 Energy Saving (LED + fan)       Campus map website (fetch every 15 s)
   ^                                               |
   +------------- MQTT commands (room controls) ---+
```
Picture for the report: `docs/architecture.svg`.

## Who does what
| Person | Role | Tracker tasks | Guide |
|---|---|---|---|
| **Member 1** | Website front end: hosting, map, panels, legend, report | W03, W04, W07, W08, W10, W15 | `README_MEMBER1.md` |
| **Member 2** | Website integration: live API, data loading, MQTT commands | W02, W05, W06, W09, W11, W12 | `README_MEMBER2.md` |
| **Weibin** | Boards 1 and 2, Apps Script API, GitHub Pages hosting | W02, W03, W04, W11, W15, W21 | - |
| **Nasrah** | Crowd board and Crowd Management script | W20, W23 | - |
| **All** | Data contract, testing, bug fixing, demo | W01, W14, W16, W17, W19 | `TESTING.md` |

The task list with dates and status is `docs/Website_Integration_Tracker.xlsx`.

## Status
**Board 1 (Sensor Node):** ✅ Connected to MQTT  
**Board 2 (Energy Saving):** ✅ Connected to MQTT  
**Website:** 🟡 In progress (W26, W27 next)

## Try it in 30 seconds
1. Download the repo, then double-click `index.html`. The map works offline.
2. To see live-looking data without any boards, open `index.html?mock=1`. Tap **How busy** or **Food**.

## Folders
| Path | What it is | Owner |
|---|---|---|
| `index.html`, `leaflet.js`, `data/` | The website. These must stay at the top level for GitHub Pages | Weibin |
| `data/mock_live.js` | Fake readings for `?mock=1` (testing and demo backup) | Weibin |
| `facilities.csv` | Facility IDs and which map spot each one is | All |
| `apps-script/LiveApi.gs` | The combined live API added to the Environment Logger script | Weibin |
| `arduino/Board1_Sensor_Node/` | Board 1: DHT11, sound, PIR, light sensor, button, LCD, Google Sheet | Weibin |
| `arduino/Board2_Energy_Saving/` | Board 2: decides and drives the LED and fan | Weibin |
| `arduino/crowd_radar_counter/` | Doorway people counter (LD2450 radar) | Nasrah |
| `arduino/examples/` | Example ESP32 code that posts crowd and energy readings | reference |
| `arduino/archive/` | Older sketches, kept for reference only. Do not upload these | reference |
| `arduino/README.md` | Pins, libraries and MQTT topics for the boards | Weibin |
| `docs/` | Role guides, testing checklist, tracker, architecture diagram | All |
| `docs/energy-saving/` | Energy Saving handover PDFs and circuit picture | reference |

## The data contract (W01)
One record per facility from `?action=latest`. Every field is optional; the map shows what it gets.

| Field | Example | Map shows it as | Comes from |
|---|---|---|---|
| `facility_id` | `"FCA"` | which spot (see `facilities.csv`) | Facilities tab |
| `current_occupancy` | `42` | "42 of 60 inside", label on the marker | Crowd Management (Nasrah) |
| `capacity` | `60` | used for the % and the colour | Crowd Management or Facilities tab |
| `occupancy_pct` | `70` | colour if there's no count | Crowd Management |
| `crowd_status` | `"BUSY"` | colour if there's no count or % | Crowd Management |
| `temperature`, `humidity` | `26.4`, `63` | tiles in the place panel | Board 1, Environment tab |
| `comfort` | `"COMFY"` | COOL / COMFY / WARM / HOT / HUMID colour | Environment (Apps Script) |
| `hvac` | `"HIGH"` | HVAC level OFF / LOW / MEDIUM / HIGH | decided by Board 2, logged by Board 1 |
| `fan_pct` | `70` | Fan speed 70% with a bar | decided by Board 2, logged by Board 1 |
| `light_on` | `true` | Lights ON / OFF | decided by Board 2, logged by Board 1 |
| `brightness`, `mode` | `75`, `"MANUAL"` | room controls | Board 2 (after W11) |
| `time`, `ts` | `"11/11/26 14:03:12"` | "Live, updated 14:03:12" | Apps Script |

Crowd colours: **Quiet** under 35% full (green), **Moderate** under 65% (yellow), **Busy** under 85% (orange), **Packed** (red).

## MQTT topics (broker.hivemq.com)
| Topic | From | To |
|---|---|---|
| `sitg7/room1/temp`, `hum`, `sound`, `crowd`, `location`, `occupancy`, `ldr` | Board 1 | Board 2, phone app |
| `sitg7/room1/energy/hvac`, `lighting`, `ambient`, `fan` (and the other `energy/` topics) | Board 2 | Board 1, Energy dashboard |
| `sitg7/room1/cmd/mode`, `scenario`, `crowd` | phone test panel | Board 2 |
| `sitg7/<ID>/cmd/light`, `brightness`, `hvac`, `mode` | website room controls (W10) | Board 2 (W11, not built yet) |
