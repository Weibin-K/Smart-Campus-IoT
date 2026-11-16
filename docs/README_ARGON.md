# Weibin: website integration (live data, freshness badge, MQTT commands)

You own **W06, W22, W10 and W12**. Weibin edits the panel and legend parts of `index.html`; you edit the
data loader and the command sender. Pull before you start and push when you finish a task.

## W06: the map reads the Apps Script API
Built into `index.html`. Search the file for `LIVE_API`:
```javascript
  var LIVE_API = "https://script.google.com/macros/s/.../exec";
  var LIVE_EVERY = 15e3;          // fetch every 15 s
  var LIVE_STALE = 2 * 60e3;      // older than 2 min: warn that the data or the sensor is offline
```
It fetches `?action=latest` every 15 s without reloading the page and pauses while the tab is hidden.
Done when a test post changes a room on the map (Weibin's `testLive`, or a real board).

Console checks (F12 > Console on the map):
```javascript
liveStatus()
```
```javascript
setLiveApi("https://script.google.com/macros/s/ANOTHER_DEPLOYMENT/exec")
```
```javascript
pushLive([{facility_id: "FCA", current_occupancy: 58, capacity: 60, light_on: true, fan_pct: 90, hvac: "HIGH", temperature: 29.5, humidity: 70}])
```

## W22: freshness badge (Live / Delayed / Outdated)
**Not built yet.** Today the page only knows live, offline (over 2 min) and no sensor.
The tracker asks for:
- under 2 min: **Live**
- older but today: **Delayed**
- not today: **Outdated data**
- nothing at all: **No sensor**
- **Simulated data** when the record's `source` is Simulation
- room controls disabled unless Live

The place to change is `sensorState(s)` and the badge text just below it in `index.html`. The API gives you
`crowd_ts`, `energy_ts` and `env_ts` in milliseconds.

## W10: send control commands over MQTT
**Not built yet.** Weibin's buttons already call `window.sendCommand(facilityId, name, value, topic)` with the
topic `sitg7/<ID>/cmd/light | brightness | hvac | mode`. You only need to define that function.
A starting point (add the MQTT.js library first):
```html
<script src="https://unpkg.com/mqtt/dist/mqtt.min.js"></script>
```
```javascript
var cmdClient = mqtt.connect("wss://broker.hivemq.com:8884/mqtt");
window.sendCommand = function (facilityId, name, value, topic) {
  if (!cmdClient.connected) { return false; }      // the panel then says "Not sent"
  cmdClient.publish(topic, String(value));
  return true;
};
```
Done when a button press shows up in the IoT MQTT Panel app on topic `sitg7/+/cmd/#`.
The board side is Weibin's **W11** (Board 2 listening for these topics).

## W12: live refresh and offline warning
Built in:
- If the API stops answering, the How busy list says "Offline: no answer since hh:mm:ss. Showing the last values."
- A facility with no new reading for 2 min turns red.
- Crowd readings count for 15 min. After that the map falls back to the usual-for-this-hour estimate.

Check it with test 7 in `docs/TESTING.md`.

## Test without hardware
1. **No server at all:** open `index.html?mock=1`. Uses `data/mock_live.js`; counts move a little every 15 s.
2. **Push one reading** with `pushLive([...])` as above.
3. **Through the real API:** ask Weibin to run `testLive` in Apps Script, wait 15 s, and the map updates by itself.
