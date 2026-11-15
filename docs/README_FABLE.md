# Fable: website front end (room panel, colours, controls, legend, report)

You own **W05, W07, W08, W09, W13 and W18**. Most of this is already built in `index.html`; your job is to
check every facility, tune the look, and write it up. Argon edits the data loader in the same file, so
pull before you start and push when you finish a task.

## W05: room click, zoom and sensor badge
Tapping a gym, food court or study area zooms to it and opens its panel. The badge at the top reads
**"Sensor connected (live)"**, **"Sensor offline, last reading hh:mm:ss"** or **"No sensor, showing estimate"**.

Each facility ID has its spot. `facilities.csv` lists them, and in `index.html` they are in `CROWD_SPOTS`:
```javascript
    {key: "e4-food-court", fid: "FCA", type: "food", lv: "1", name: "E4 - Food Court", title: "E4 Food Court"},
```
- `fid` is the facility ID the Apps Script sends. It must match the Facilities tab exactly.
- `name` must match the room's name in the floor plan; `lv` is its level.
- To add a facility (for example `DR2` or `DR3`, see W21), copy a line and change `key`, `fid`, `type`, `lv` and `name`.

Check in the browser console (F12 > Console):
```javascript
crowdSpots().filter(s => s.facility)
```
Every facility ID in the Facilities tab should be in that list.

## W07: crowd heatmap in the room
Built in. With live data (or `?mock=1`):
- Tap **How busy**: every spot, quietest first, coloured green / yellow / orange / red.
- Rooms are filled in their crowd colour, and marker labels read like **"E4 Food Court · Packed 55/60"**.

Done when the colours change as `pushLive(...)` or the boards change the count (see `TESTING.md`).

## W08: room data panel
The panel shows, top to bottom:
1. **Busy now** with the live count and its age
2. **Popular times** by hour
3. **People inside today**: count of capacity, %, and a line chart of readings since the page opened
4. **Temperature and humidity**, with the comfort label and its colour
5. **Lights, fan and HVAC**: Lights ON/OFF, fan % with a bar, HVAC level with a bar
6. Under each part: **"Live, updated hh:mm:ss"**, or a red warning when it is over 2 min old

The code is `peopleSection`, `envSection` and `roomSection` in `index.html`.
Lights, fan and HVAC are decided by Board 2 (Energy Saving) and reach the sheet through Board 1.

## W09: student control panel
The **Room controls** section is built: Lights On/Off, Brightness 25/50/75/100%, HVAC Off/Low/Med/High and Auto,
with a **Manual mode / Auto mode** badge. Controls are only active when the room's sensor is live.

The buttons call `sendControl(...)`, which hands the command to `window.sendCommand`. Until Argon finishes
**W10**, the panel says "Not sent: the command link (MQTT) isn't connected on this page yet". That is expected.

## W13: legend, colours and phone layout
- Crowd legend: top of the How busy list. Comfort legend: under the temperature tiles.
- Colours are CSS variables at the top of `index.html`: `--c0` to `--c3` (crowd), `--tcool` and `--thum` (comfort).
  There is a light and a dark set; change both.
- Phone: the panel becomes a bottom sheet. Check on a real phone, and in Chrome with F12 > device toolbar.

## W18: screenshots and report section
- Diagram: `docs/architecture.svg` (opens in any browser; insert into Word or Slides).
- Screenshots (use `?mock=1` if a board is down): How busy list; E4 Food Court panel with all sections;
  the Room controls; a phone screenshot; the red "offline" warning.
- Results table: copy the W14, W16 and W27 tables from `docs/TESTING.md`.

Suggested report paragraph:
> The website is a single-page Leaflet map hosted on GitHub Pages. Every 15 s it requests
> `?action=latest` from the group's Google Apps Script, which returns the newest crowd, energy and
> environment readings for each facility as JSON. The map colours each facility by occupancy
> (Quiet under 35%, Moderate under 65%, Busy under 85%, Packed above) and shows its live count,
> temperature, humidity, comfort level, lighting state, fan speed and HVAC level. If a reading is
> more than 2 minutes old, the page warns that the sensor may be offline.
