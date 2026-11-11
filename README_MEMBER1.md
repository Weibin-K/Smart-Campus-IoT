# Member 1: website side (hosting, map, panel, legend, report)

You own W03, W04, W07, W08, W10 and W15. The map already does all of this; your job is to host it,
check every facility, tune the look, and write it up.

## W03: put it online (GitHub Pages)
1. GitHub > the group repo (e.g. `Smart-Campus-IoT`) > **Add file > Upload files**.
   Drag in **everything inside** the unzipped folder (`index.html`, `leaflet.js`, `data/`, ...). Commit.
2. Repo **Settings > Pages > Source: Deploy from a branch > Branch: main, / (root) > Save**.
3. After about a minute the link appears at the top of that page, like
   `https://<username>.github.io/Smart-Campus-IoT/`. Open it on your phone too.

https matters: phones only share GPS (the locate button) on an https site, which GitHub Pages gives you.

## W04: markers for every facility
Each facility ID already has its spot. `facilities.csv` lists them: ID, name, building, level, and `map_key`.
In `index.html` they're in `CROWD_SPOTS` (search for it). One line per spot:
```javascript
    {key: "e4-food-court", fid: "FCA", type: "food", lv: "1", name: "E4 - Food Court", title: "E4 Food Court"},
```
- `fid` is the facility ID the Apps Script sends. It must match the Facilities tab exactly.
- `name` must match the room's name in the floor plan; `lv` is its level.
- To add a facility (e.g. `DR1` or `STB`), copy a line, change `key`, `fid`, `type`, `lv` and `name`.
  Search the room's name on the map first to get its exact spelling and level.

Check: open the map, press F12 > Console, and run:
```javascript
crowdSpots().filter(s => s.facility)
```
Every facility ID in the Facilities tab should be in that list.

## W07: markers coloured by crowd
Built in. With live data (or `?mock=1`):
- Tap **How busy**: every spot, quietest first, coloured green / yellow / orange / red.
- Rooms are filled in their crowd colour, and marker labels read like **"E4 Food Court · Packed 55/60"**.
Done when the colours change as `pushLive(...)` or the boards change the count (see `TESTING.md`).

## W08: facility detail panel
Tap a marker or a place in a category. The panel shows, top to bottom:
1. **Busy now** with the live count and its age
2. **Popular times** by hour
3. **People inside today**: count of capacity, %, and a line chart of readings since the page opened
4. **Temperature and humidity**, with the comfort label (Cool / Comfy / Warm / Hot / Humid) and its colour
5. **Lights, fan and HVAC**: Lights ON/OFF, fan % with a bar, HVAC level with a bar
6. Under each part: **"Live · updated hh:mm:ss"**, or a red warning when it's over 2 min old

The code is `peopleSection`, `envSection` and `roomSection` in `index.html`.

## W10: legend, colours and phone layout
- Crowd legend: top of the How busy list. Comfort legend: under the temperature tiles.
- Colours are CSS variables at the top of `index.html`: `--c0` to `--c3` (crowd), `--tcool` and `--thum`
  (comfort). There's a light and a dark set; change both.
- Comfort thresholds match the Apps Script Settings tab (`COMFY_LOW = 23.5`, `COMFY_HIGH = 26.5`, `TEMP_HIGH = 28`,
  `HUM_HIGH = 70`). If the group changes them in the sheet, the script's `comfort` label wins anyway.
- Phone: the panel becomes a bottom sheet. Check on a real phone, and in Chrome with F12 > device toolbar > iPhone 12.

## W15: screenshots and report section
- Diagram: `docs/architecture.svg` (opens in any browser; insert into Word or Slides).
- Screenshots to take (use `?mock=1` if a board is down): How busy list; E4 Food Court panel with all sections;
  a phone screenshot; the red "offline" warning (stop the API or wait 2 min after a board is unplugged).
- Results table: copy the W11 and W13 tables from `TESTING.md`.

Suggested report paragraph:
> The website is a single-page Leaflet map hosted on GitHub Pages. Every 15 s it requests
> `?action=latest` from the group's Google Apps Script, which returns the newest crowd, energy and
> environment readings for each facility as JSON. The map colours each facility by occupancy
> (Quiet under 35%, Moderate under 65%, Busy under 85%, Packed above) and shows its live count,
> temperature, humidity, comfort level, PIR lighting state, fan speed and HVAC level. If a reading is
> more than 2 minutes old, the page warns that the sensor may be offline.
