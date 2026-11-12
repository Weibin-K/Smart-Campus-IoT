# SIT Smart Campus map: website integration

The campus map with **directions and live crowd, energy and environment data**, ready for GitHub Pages.
It reads the group's Google Apps Script every 15 s and shows each facility's live count, crowd colour,
temperature, humidity, comfort, PIR light, fan % and HVAC level.

```
Arduino / ESP32 boards --POST--> Apps Script --> Google Sheet
                                     |
                         GET ?action=latest (JSON)
                                     v
                         this website (fetch every 15 s)
```
Picture for the report: `docs/architecture.svg`.

## Who does what
| File | Owner | Tracker tasks |
|---|---|---|
| `README_MEMBER1.md` | Member 1 (website side) | W03, W04, W07, W08, W10, W15 |
| `README_MEMBER2.md` | Member 2 (data side) | W02, W05, W06, W09, W11, W12 |
| `TESTING.md` | Both | W01, W11, W13, W14, W16 |

## Try it in 30 seconds
1. Unzip, then double-click `index.html`. The map works offline.
2. To see live-looking data without any boards, open `index.html?mock=1`
   (add `?mock=1` to the end of the address). Tap **How busy** or **Food**.

## Files
| Path | What it is |
|---|---|
| `index.html` | The whole app (map, search, directions, crowd, live data) |
| `leaflet.js` | Map library, kept local so the page loads fast and works offline |
| `data/sit_floors.js`, `sit_extras.js`, `sit_graph.js` | Floor plans, roads and MRT, walking network |
| `data/mock_live.js` | Fake readings for `?mock=1` (testing and demo backup) |
| `apps-script/LiveApi.gs` | Member 2: adds the combined live API to the Apps Script |
| `arduino/live_post_esp32/live_post_esp32.ino` | Example ESP32 code that posts crowd and energy readings |
| `facilities.csv` | Facility IDs and which map spot each one is |
| `docs/architecture.svg` | Data flow diagram for the report |

## The data contract (W01)
One record per facility from `?action=latest`. Every field is optional; the map shows what it gets.

| Field | Example | Map shows it as | Comes from |
|---|---|---|---|
| `facility_id` | `"FCA"` | which spot (see `facilities.csv`) | Facilities tab |
| `current_occupancy` | `42` | "42 of 60 inside", label on the marker | Crowd Management |
| `capacity` | `60` | used for the % and the colour | Crowd Management or Facilities tab |
| `occupancy_pct` | `70` | colour if there's no count | Crowd Management |
| `crowd_status` | `"BUSY"` | colour if there's no count or % | Crowd Management |
| `light_on` | `true` | Lights (PIR) ON / OFF | Energy Saving |
| `fan_pct` | `70` | Fan speed 70% with a bar | Energy Saving |
| `hvac` | `"HIGH"` | HVAC level OFF / LOW / MEDIUM / HIGH | Energy Saving or Environment |
| `temperature`, `humidity` | `26.4`, `63` | tiles in the place panel | Environment |
| `comfort` | `"COMFY"` | COOL / COMFY / WARM / HOT / HUMID colour | Environment |
| `time`, `ts` | `"11/11/26 14:03:12"` | "Live · updated 14:03:12" | Apps Script |

Crowd colours: **Quiet** under 35% full (green), **Moderate** under 65% (yellow), **Busy** under 85% (orange), **Packed** (red).
