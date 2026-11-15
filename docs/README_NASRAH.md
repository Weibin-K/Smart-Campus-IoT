# Nasrah: crowd board and Crowd Management script

You own **W20 and W23**, and the crowd part of the data contract (W01).

## W20: Crowd script v3
1. In the Crowd Management Apps Script, run `crowdSetupHistory` > Review permissions > Allow.
2. **Deploy > Manage deployments > New version > Deploy.**

Done when the Timestamp column shows today and a **Crowd History** tab appears.

## W23: names and status values
- One name per place across all tabs (for example Common Study Space B).
- Booking status is written `VACANT_PENDING_RELEASE` (no dashes or spaces).
- Every `Facility ID` must match `facilities.csv` and the Facilities tab exactly, or the map cannot find it.

## What the map reads from your tab
The live API (`apps-script/LiveApi.gs`, Weibin) reads the newest row per facility from the **Crowd Management**
tab by header name, so your 15-column layout stays as it is. It looks for these headers:

| Map field | Header it looks for |
|---|---|
| facility | Facility ID |
| count | Current Occupancy |
| capacity | Max Occupancy |
| % | Occupancy (%) |
| status | Crowd Status |
| booking | Booking Status, Booking End Time |
| source | Data Source |
| time | Timestamp |

If you rename a header, tell Weibin so the list in `LIVE_COLUMNS` is updated.

## Crowd board
`arduino/crowd_radar_counter/` is the doorway people counter (LD2450 radar, Uno WiFi Rev2).
It still uploads to ThingSpeak. To send to the group sheet instead, post this to the Apps Script URL:
```json
{"api_key":"...","type":"crowd","facility_id":"FCA","current_occupancy":42,"capacity":60}
```
`arduino/examples/live_post_esp32/` has a ready-made `postCrowd()` for an ESP32.

## Crowd level for the Energy Saving board
Board 2 uses a crowd level 0 to 3 for the HVAC decision. It listens on `sitg7/room1/crowd`.
Right now that number is typed into Board 1's Serial Monitor. The group still has to agree the people-count
bands for NONE / LOW / MEDIUM / HIGH and who publishes the level.
