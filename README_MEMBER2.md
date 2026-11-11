# Member 2: data side (Apps Script API and live loading)

You own W02, W05, W06, W09, W11 and the stretch task W12.
Most of the code is written; your job is to install it, connect the tabs, and prove it works.

## W02 + W05: one JSON with crowd, energy and environment

### 1. Add the new file
1. Open the group sheet > **Extensions > Apps Script** (the **Environment Logger** project, the one with `Code.gs`).
2. **Files > + > Script**, name it `LiveApi`, paste all of `apps-script/LiveApi.gs`, save.

`LiveApi.gs` uses `SHEET_ID`, `getFacilities()` and `json()` from `Code.gs`, so it must be in the same project.

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
This lets the crowd, PIR and fan boards post to the **same URL**. Old environment posts work as before.

### 3. Point it at the right tabs
At the top of `LiveApi.gs`:
- `LIVE_TAB_CROWD` and `LIVE_TAB_ENERGY` are the tab names (`Crowd Management`, `Energy Saving`).
- If the crowd data lives in a **different spreadsheet**, put that sheet's ID in `LIVE_CROWD_SHEET_ID`.
- `LIVE_COLUMNS` lists the header names it looks for. Run **`testColumns`** (pick it in the dropdown, then Run).
  The log shows the newest row per facility. If a field is missing, put your real header text first in that list.
- Fill in the **Capacity** column of the Facilities tab for every place with a counter, so the map can work out % full.

### 4. Test, then redeploy
1. Run **`testLive`**. The log should show 6 lines of `{"success":true,...}`, then JSON with FCA, LIB and E2L5A.
2. **Deploy > Manage deployments > pencil > Version: New version > Deploy.** The URL stays the same.
3. Check that **Who has access** is **Anyone**. Otherwise the website gets a Google login page instead of data.
4. Open this in a browser:
```
https://script.google.com/macros/s/AKfycbzlVsZVIWkLt818jMaaC5FyiXxicgMHUnufDmqna2tomFhcsaretr3Tu6svYqhBWqiJOQ/exec?action=latest
```
You should see `{"count":...,"facilities":[{"facility_id":"E2L5A",...`. **W02 and W05 are done.**
5. Run **`clearLiveTest`** to remove the test values, and delete the `TEST` rows in the two log tabs.

### What the boards send
Send only the parts a board knows. Same `api_key` as the environment board (`ENV_API_KEY`).
```json
{"api_key":"...","type":"crowd","facility_id":"FCA","current_occupancy":42,"capacity":60}
```
```json
{"api_key":"...","type":"energy","facility_id":"FCA","light_on":true,"fan_pct":70,"hvac":"HIGH"}
```
`arduino/live_post_esp32/live_post_esp32.ino` has ready-made `postCrowd()` and `postEnergy()` functions for the board owners.
If a board already logs to the Crowd Management or Energy Saving tab, it doesn't need to change: the API reads the newest row there too.

## W06 + W09: the website fetches it
Already built into `index.html`. To check or change it, search the file for `LIVE_API`:
```javascript
  var LIVE_API = "https://script.google.com/macros/s/.../exec";
  var LIVE_EVERY = 15e3;          // fetch every 15 s
  var LIVE_STALE = 2 * 60e3;      // older than 2 min: warn that the data or the sensor is offline
```
What it does:
- Fetches `?action=latest` every 15 s, without reloading the page, and pauses while the tab is hidden.
- Each place panel shows **"Live · updated hh:mm:ss"**.
- A facility with no new reading for 2 min turns red: **"No new reading for 5 min. The board may be offline."**
- If the API stops answering, the How busy list says **"Offline: no answer since 14:03:12. Showing the last values."**
- Crowd readings count for 15 min. After that the map falls back to the usual-for-this-hour estimate.

Console checks (press F12 > Console on the map):
```javascript
liveStatus()
```
```javascript
setLiveApi("https://script.google.com/macros/s/ANOTHER_DEPLOYMENT/exec")
```

## W11: test with simulated data
Three ways, from no hardware to real posts:
1. **No server at all:** open `index.html?mock=1`. Uses `data/mock_live.js`; counts move a little every 15 s.
2. **Push one reading into the open map** (console):
```javascript
pushLive([{facility_id: "FCA", current_occupancy: 58, capacity: 60, light_on: true, fan_pct: 90, hvac: "HIGH", temperature: 29.5, humidity: 70}])
```
3. **Through the real API:** run `testLive` in Apps Script, wait 15 s, and the map updates by itself.

Fill the W11 table in `TESTING.md` as you go.

## W12 (stretch): MQTT for 2 s updates
Only if W01 to W11 are done. Add the MQTT.js library to `index.html`, connect to
`wss://broker.hivemq.com:8884/mqtt`, subscribe to `sitg7/#`, and call `pushLive([...])` with each message
converted to the record shape above. Keep the 15 s Apps Script fetch as the backup.
