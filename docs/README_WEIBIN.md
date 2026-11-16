# Weibin: boards, Apps Script API and hosting

You own **W02, W03, W04, W21, W11, W15** and the two-board tasks **W24, W25, W26**.
Pins, libraries and MQTT topics for the boards are in `arduino/README.md`.

## W02 and W15: GitHub Pages
1. GitHub Desktop > Fetch / Pull, copy the new files in, Commit to main, Push origin.
2. Repo **Settings > Pages > Source: Deploy from a branch > main / (root)**.
3. Hard refresh (Ctrl+F5) https://weibin-k.github.io/Smart-Campus-IoT/ and check it on a phone.

`index.html`, `leaflet.js` and `data/` must stay at the top level, or the link breaks.

## W04: live JSON API
### 1. Add the file
1. Group sheet > **Extensions > Apps Script** (the **Environment Logger** project, the one with `Code.gs`).
2. **Files > + > Script**, name it `LiveApi`, paste all of `apps-script/LiveApi.gs`, save.

### 2. Two edits in `Code.gs`
In **`doGet`**, replace the whole `if (action === "latest") { ... }` block with:
```javascript
  if (action === "latest") {
    return json(liveLatest(e.parameter.facility));
  }
```
In **`doPost`**, add this line straight after `const p = JSON.parse(e.postData.contents);`:
```javascript
    if (p.type === "crowd" || p.type === "energy") { return json(livePost(p)); }
```

### 3. Point it at the right tabs
At the top of `LiveApi.gs`: `LIVE_TAB_CROWD`, `LIVE_TAB_ENERGY`, `LIVE_TAB_ENV` are the tab names, and
`LIVE_COLUMNS` lists the header names it looks for. Run **`testColumns`** to see what it found.
Fill in the **Capacity** column of the Facilities tab for every place with a counter.

### 4. Test, then redeploy
1. Run **`testLive`**, then **`checkW04`**.
2. **Deploy > Manage deployments > pencil > Version: New version > Deploy.** Access must be **Anyone**.
3. Open the `?action=latest` link (it is in the tracker, Setup Guide tab). Done when FCA, FCB, LIB, STB, GYM and DR1 to DR3 show.
4. Run **`clearLiveTest`** to remove the test values.

## W21: add DR2 and DR3
Add the rows to `DEFAULT_FACILITIES` and run `setupAll`, or type them into the Facilities tab.
Tell Weibin so the two spots are added to `CROWD_SPOTS` too.

## W24 and W25: the two boards
| Board | Sketch | Does |
|---|---|---|
| Board 1, sensor node | `arduino/Board1_Sensor_Node/` | Senses (DHT11, sound, PIR, light), shows the LCD, posts to the Google Sheet |
| Board 2, Energy Saving | `arduino/Board2_Energy_Saving/` | Listens to Board 1, decides, drives the LED and fan, reports back |

Before uploading, fill in the hotspot password in both sketches and the API key in Board 1.
They are left blank in this repo on purpose because the repo is public.

Done when Board 1's LCD page 2 shows `LED:... FAN:...%` instead of `Board 2: no link`.

## W26: Light Status and Fan Speed columns
`apps-script/Code.gs` is the Environment Logger with two new columns at the end of the Environment tab:
**Light Status** (ON / OFF) and **Fan Speed (%)**. They come from Board 2 and are sent by Board 1.
1. Paste `apps-script/Code.gs` over the old `Code.gs`, then redo the two small `doGet` / `doPost` edits from W04.
2. Paste `apps-script/LiveApi.gs` over the old `LiveApi`.
3. Run **`testPost`**. The two headers appear by themselves and a TEST row shows ON and 40.
4. **Deploy > Manage deployments > New version.**

Old rows keep working; the two cells are blank when Board 1 has no link to Board 2.

## W11: Board 2 listens for website commands
**Not built yet.** The website will publish `sitg7/<ID>/cmd/light`, `/brightness`, `/hvac` and `/mode` (Weibin, W10).
Board 2 has to subscribe, switch to MANUAL when a command arrives, and go back to AUTO on a `mode` command.

Two things to agree with the group first:
- Board 2 already uses `sitg7/room1/cmd/mode` for LIVE / SIMULATION. The website wants `mode` to mean AUTO / MANUAL.
- The boards use `room1` in their topics; the website uses the facility ID (for example `LIB`).
