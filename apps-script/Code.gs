// ================== CONFIG ==================
const SHEET_ID = "16rqjHiJTun-19WOMrPod_BANjgVlpPI7YLM4kYTGwSY";

const TAB_DATA = "Environment";
const TAB_SETTINGS = "Settings";
const TAB_FACILITIES = "Facilities";
const TAB_SUMMARY = "Daily Summary";
const TAB_ALERTS = "Alerts";

const HEADERS = ["Timestamp","Facility ID","Facility Name","Category",
                 "Temperature (°C)","Humidity (%)","Sound Level (raw)",
                 "Temp Status","Humidity Status","Noise Status","Comfort Status",
                 "HVAC Setting","Data Source",
                 "Light Status","Fan Speed (%)","Occupancy Level","Light Level (raw)","Ambient","Lighting","Fan Speed (%)"];      // W26: Board 1 new fields

const SETTINGS_HEADERS = ["Parameter","Value","Unit","Reason"];
const DEFAULT_SETTINGS = [
  ["TEMP_HIGH",  28,   "°C",  "75th percentile of indoor readings"],
  ["TEMP_LOW",   22,   "°C",  "Below comfort range"],
  ["COMFY_LOW",  23.5, "°C",  "Start of comfort zone (green on LCD)"],
  ["COMFY_HIGH", 26.5, "°C",  "End of comfort zone (green on LCD)"],
  ["HUM_HIGH",   70,   "%",   "75th percentile indoor"],
  ["SOUND_MED",  300,  "raw", "Quiet vs talking test"],
  ["SOUND_HIGH", 600,  "raw", "Busy food court test"]
];

const FACILITY_HEADERS = ["Facility ID","Facility Name","Category","Building","Level","Capacity","Notes"];
const DEFAULT_FACILITIES = [
  ["GYM",  "Gym 1",                 "Gym",         "E5", "5",   20, ""],
  ["GYM2", "Gym 2",                 "Gym",         "E5", "6",   "", ""],
  ["GYM3", "Outdoor Gym",           "Gym",         "E5", "7",   "", "Outdoor"],
  ["LIB",  "Learning Commons",      "Library",     "W1", "5",   40, ""],
  ["LIB2", "Book Collection",       "Library",     "W1", "5",   "", ""],
  ["LIB3", "Library Amphitheatre",  "Library",     "W1", "3-5", "", ""],
  ["LIB4", "Lounge Library",        "Library",     "E1", "3",   "", ""],
  ["FCA",  "E4 Food Court",         "Food court",  "E4", "1",   60, ""],
  ["FCB",  "W3 Food Court",         "Food court",  "W3", "2",   60, ""],
  ["FCC",  "Takeaway Kiosk",        "Food court",  "E1", "1",   "", ""],
  ["DR1",  "Discussion Room",       "Study space", "",   "",    8,  "Already used in crowd sheet"],
  ["DR2",  "Discussion Room 2",     "Study space", "",   "",    8,  "Crowd sheet"],
  ["DR3",  "Discussion Room 3",     "Study space", "",   "",    8,  "Crowd sheet"],
  ["STB",  "Common Study Space B",  "Study space", "",   "",    30, "Already used in crowd sheet"],
  ["E2L2M","E2 L2M Study Space",    "Study space", "E2", "2M",  "", "Project Hub"],
  ["E2L5A","E2 L5 Study Space 1",   "Study space", "E2", "5",   "", "Discovery Hub 2"],
  ["E2L5B","E2 L5 Study Space 2",   "Study space", "E2", "5",   "", "Discovery Hub 3"],
  ["E2L7", "E2 L7 Study Space",     "Study space", "E2", "7",   "", ""],
  ["E2L9", "E2 L9 Study Space",     "Study space", "E2", "9",   "", ""],
  ["E6L6", "E6 L6 Study Space",     "Study space", "E6", "6",   "", ""],
  ["E6L7", "E6 L7 Study Space",     "Study space", "E6", "7",   "", "Enclosed"],
  ["E6L8", "E6 L8 Study Space",     "Study space", "E6", "8",   "", ""],
  ["E6L9", "E6 L9 Study Space",     "Study space", "E6", "9",   "", "Enclosed"],
  ["W1L5", "W1 L5 Project Hub",     "Study space", "W1", "5",   "", ""],
  ["W1L6", "W1 L6 Project Hub",     "Study space", "W1", "6",   "", ""],
  ["CLS",  "Classroom",             "Classroom",   "",   "",    "", ""],
  ["OUT",  "Outdoor Area",          "Outdoor",     "",   "",    "", "Use for rain/outdoor tests"]
];

const SUMMARY_HEADERS = ["Date","Facility ID","Facility Name","Readings",
                         "Avg Temp","Min Temp","Max Temp","Avg Humidity","Max Humidity",
                         "Avg Sound","% Time Comfy"];
const ALERT_HEADERS = ["Timestamp","Facility ID","Facility Name","Type","Change","Value"];

// ================== SETUP ==================
function ss() { return SpreadsheetApp.openById(SHEET_ID); }

function ensureTab(name, headers, defaultRows, strictHeaders) {
  let sh = ss().getSheetByName(name);
  if (!sh) { sh = ss().insertSheet(name); }
  const first = sh.getRange(1, 1, 1, headers.length).getValues()[0];
  const empty = first.every(v => String(v).trim() === "");
  if (empty) {
    sh.getRange(1, 1, 1, headers.length).setValues([headers]);
    sh.setFrozenRows(1);
    if (defaultRows && defaultRows.length > 0) {
      sh.getRange(2, 1, defaultRows.length, headers.length).setValues(defaultRows);
    }
  } else {
    // NEW: fill in headers that are still blank (e.g. new columns added at the end), keep old data
    for (let i = 0; i < headers.length; i++) {
      const have = String(first[i]).trim();
      if (have === "") { sh.getRange(1, i + 1).setValue(headers[i]); }
      else if (strictHeaders && have !== headers[i]) {
        throw new Error("Tab '" + name + "' has different headers. Clear row 1 (or rename the old tab) and run setupAll again.");
      }
    }
  }
  return sh;
}

// adds any Settings rows that are missing (e.g. COMFY_LOW) without touching existing ones
function addMissingSettings() {
  const sh = ensureTab(TAB_SETTINGS, SETTINGS_HEADERS, DEFAULT_SETTINGS, false);
  const names = sh.getDataRange().getValues().map(r => String(r[0]).trim());
  for (let i = 0; i < DEFAULT_SETTINGS.length; i++) {
    if (names.indexOf(DEFAULT_SETTINGS[i][0]) === -1) { sh.appendRow(DEFAULT_SETTINGS[i]); }
  }
}

// adds any Facilities rows that are missing (e.g. CLS, OUT) without touching existing ones
function addMissingFacilities() {
  const sh = ensureTab(TAB_FACILITIES, FACILITY_HEADERS, DEFAULT_FACILITIES, false);
  const ids = sh.getDataRange().getValues().map(r => String(r[0]).trim().toUpperCase());
  for (let i = 0; i < DEFAULT_FACILITIES.length; i++) {
    if (ids.indexOf(DEFAULT_FACILITIES[i][0]) === -1) { sh.appendRow(DEFAULT_FACILITIES[i]); }
  }
}

function setupAll() {
  ensureTab(TAB_DATA, HEADERS, null, true);
  addMissingSettings();
  addMissingFacilities();
  ensureTab(TAB_SUMMARY, SUMMARY_HEADERS, null, true);
  ensureTab(TAB_ALERTS, ALERT_HEADERS, null, true);
  return "All tabs ready";
}

// ================== READ CONFIG ==================
function getSettings() {
  const s = {};
  for (let i = 0; i < DEFAULT_SETTINGS.length; i++) { s[DEFAULT_SETTINGS[i][0]] = DEFAULT_SETTINGS[i][1]; }
  const sh = ss().getSheetByName(TAB_SETTINGS);
  if (!sh) return s;
  const rows = sh.getDataRange().getValues();
  for (let i = 1; i < rows.length; i++) {
    const name = String(rows[i][0]).trim();
    const value = Number(rows[i][1]);
    if (name in s && rows[i][1] !== "" && isFinite(value)) { s[name] = value; }
  }
  return s;
}

function getFacilities() {
  const map = {};
  const sh = ss().getSheetByName(TAB_FACILITIES);
  const rows = sh ? sh.getDataRange().getValues().slice(1) : DEFAULT_FACILITIES;
  for (let i = 0; i < rows.length; i++) {
    const id = String(rows[i][0]).trim().toUpperCase();
    if (id) { map[id] = { name: String(rows[i][1]), category: String(rows[i][2]), capacity: rows[i][5] }; }
  }
  return map;
}

// ================== STATUS LOGIC ==================
function tempStatus(t, c)  { return t > c.TEMP_HIGH ? "HIGH" : (t < c.TEMP_LOW ? "LOW" : "NORMAL"); }
function humStatus(h, c)   { return h > c.HUM_HIGH ? "HIGH" : "NORMAL"; }
function noiseStatus(s, c) { return s >= c.SOUND_HIGH ? "LOUD" : (s >= c.SOUND_MED ? "MODERATE" : "QUIET"); }

// Map raw sound level to category (Board 1 now sends raw, Apps Script maps it)
function soundLevel(raw) {
  if (raw < 10)  return "SILENCE";
  if (raw < 20)  return "QUIET";
  if (raw < 30)  return "MODERATE";
  if (raw < 40)  return "NOISY";
  if (raw < 50)  return "LOUD";
  return "VERY LOUD";
}

function comfortStatus(t, h, c) {
  if (t > c.TEMP_HIGH) return "HOT";
  if (t > c.COMFY_HIGH) return "WARM";
  if (t < c.TEMP_LOW) return "COLD";
  if (t < c.COMFY_LOW) return "COOL";
  if (h > c.HUM_HIGH) return "HUMID";
  return "COMFY";
}

// Raw LDR value from Board 1 (0-1023), or blank when not sent
function lightLevelCell_(v) {
  const n = Number(v);
  return (v !== undefined && v !== null && v !== "" && isFinite(n) && n >= 0 && n <= 1023) ? Math.round(n) : "";
}
// Occupancy from Board 1: OCCUPIED or VACANT, or blank when not sent
function occupancyCell_(v) {
  const s = String(v == null ? "" : v).trim().toUpperCase();
  return (s === "OCCUPIED" || s === "VACANT") ? s : "";
}
// Ambient from Board 2: DARK or BRIGHT, or blank when not sent
function ambientCell_(v) {
  const s = String(v == null ? "" : v).trim().toUpperCase();
  return (s === "DARK" || s === "BRIGHT") ? s : "";
}

// ================== RECEIVE DATA (Arduino) ==================
function doPost(e) {
  try {
    const p = JSON.parse(e.postData.contents);

    if (p.type === "crowd" || p.type === "energy") { return json(livePost(p)); }
    if (p.type === "batch") { return json(livePostBatch(p)); }  // batch of 4 readings

    const secret = PropertiesService.getScriptProperties().getProperty("ENV_API_KEY");
    if (!secret || p.api_key !== secret) throw new Error("Invalid API key");

    const id = String(p.facility_id || "").trim().toUpperCase();
    const facilities = getFacilities();
    if (!(id in facilities)) throw new Error("Invalid facility ID: " + id + " (add it to the Facilities tab)");

    const t = Number(p.temperature), h = Number(p.humidity), snd = Number(p.sound_level);
    if (!isFinite(t) || t < -10 || t > 60)  throw new Error("Invalid temperature");
    if (!isFinite(h) || h < 0 || h > 100)   throw new Error("Invalid humidity");
    if (!Number.isInteger(snd) || snd < 0 || snd > 4095) throw new Error("Invalid sound_level");

    let hvac = p.hvac == null ? "" : String(p.hvac).toUpperCase();
    if (hvac === "UNKNOWN") { hvac = ""; }               // NEW: Board 1 has no link to Board 2 right now

    // NEW: light ON/OFF and fan speed (0-100 %). Both optional; blank when not sent or not known.
    let light = p.lighting == null ? "" : String(p.lighting).toUpperCase();
    if (light !== "ON" && light !== "OFF") { light = ""; }
    let fan = Number(p.fan_speed);
    if (p.fan_speed == null || !isFinite(fan) || fan < 0 || fan > 100) { fan = ""; } else { fan = Math.round(fan); }
    if (hvac !== "" && ["OFF","LOW","MEDIUM","HIGH"].indexOf(hvac) === -1) throw new Error("Invalid hvac");

    const c = getSettings();
    const fac = facilities[id];
    const status = {
      temp: tempStatus(t, c),
      hum: humStatus(h, c),
      noise: soundLevel(snd),  // use mapped level instead of threshold
      comfort: comfortStatus(t, h, c)
    };

    const row = [new Date(), id, fac.name, fac.category,
                 Math.round(t * 10) / 10, Math.round(h * 10) / 10, snd,
                 status.temp, status.hum, status.noise, status.comfort,
                 hvac, String(p.data_source || "SENSOR"),
                 light, fan,
                 occupancyCell_(p.occupancy),              // Occupancy Level from Board 1
                 lightLevelCell_(p.light_level),           // Light Level (raw) from Board 1
                 ambientCell_(p.ambient),                  // Ambient DARK / BRIGHT from Board 2
                 light, fan];                              // Lighting and Fan Speed (%) columns (same values)

    const lock = LockService.getScriptLock();
    lock.waitLock(10000);
    try {
      ensureTab(TAB_DATA, HEADERS, null, true).appendRow(row);
      saveLatest(id, fac, t, h, snd, status, hvac, light, fan);
      checkAlerts(id, fac.name, t, h, snd, status);
    } finally { lock.releaseLock(); }

    return json({ success: true, facility: id, comfort: status.comfort });
  } catch (err) {
    logError(err, e);
    return json({ success: false, error: String(err.message) });
  }
}

// writes rejected posts to an "Errors" tab (API key hidden)
function logError(err, e) {
  try {
    let body = (e && e.postData) ? e.postData.contents : "(no body)";
    body = body.replace(/"api_key":"[^"]*"/, '"api_key":"***"');
    const sh = ensureTab("Errors", ["Timestamp","Error","Received data"], null, false);
    sh.appendRow([new Date(), String(err.message), body]);
  } catch (ignore) {}
}

// ================== LATEST READINGS (for doGet) ==================
function saveLatest(id, fac, t, h, snd, status, hvac, light, fan) {
  const data = {
    facility_id: id, facility_name: fac.name, category: fac.category,
    temperature: Math.round(t * 10) / 10, humidity: Math.round(h * 10) / 10, sound_level: snd,
    temp_status: status.temp, humidity_status: status.hum, noise_status: status.noise,
    comfort: status.comfort, hvac: hvac,
    lighting: light, fan_speed: fan,                     // NEW
    time: Utilities.formatDate(new Date(), Session.getScriptTimeZone(), "dd/MM/yy HH:mm:ss")
  };
  PropertiesService.getScriptProperties().setProperty("latest_" + id, JSON.stringify(data));
}

// ================== ALERTS ==================
// logs a row when a status becomes abnormal, and when it returns to normal
function checkAlerts(id, name, t, h, snd, status) {
  const props = PropertiesService.getScriptProperties();
  const key = "status_" + id;
  const old = JSON.parse(props.getProperty(key) || '{"temp":"NORMAL","hum":"NORMAL","noise":"QUIET"}');
  const sh = ensureTab(TAB_ALERTS, ALERT_HEADERS, null, true);
  const now = new Date();

  const checks = [
    ["Temperature", old.temp,  status.temp,  "NORMAL", t],
    ["Humidity",    old.hum,   status.hum,   "NORMAL", h],
    ["Noise",       old.noise, status.noise, "QUIET",  snd]
  ];
  for (let i = 0; i < checks.length; i++) {
    const type = checks[i][0], before = checks[i][1], after = checks[i][2], normal = checks[i][3], value = checks[i][4];
    const isAlert = (after === "HIGH" || after === "LOW" || after === "LOUD");
    if (before !== after && (isAlert || after === normal)) {
      const label = isAlert ? (before + " -> " + after) : (before + " -> " + after + " (cleared)");
      sh.appendRow([now, id, name, type, label, value]);
    }
  }
  props.setProperty(key, JSON.stringify({ temp: status.temp, hum: status.hum, noise: status.noise }));
}

// ================== LIVE API (browser / app) ==================
// ?action=latest                -> latest reading for every facility
// ?action=latest&facility=E2L5A -> latest reading for one facility
function doGet(e) {
  const action = (e && e.parameter && e.parameter.action) || "status";
  const props = PropertiesService.getScriptProperties();

  if (action === "latest") {
    return json(liveLatest(e.parameter.facility));      // LiveApi.gs: crowd + energy + environment
  }

  return json({ ok: true, service: "Environment Logger",
                time: Utilities.formatDate(new Date(), Session.getScriptTimeZone(), "dd/MM/yy HH:mm:ss") });
}

// ================== BATCH POSTING ==================
// Process batch of 4 readings from Board 1
function livePostBatch(batch) {
  const id = String(batch.facility_id || "").trim().toUpperCase();
  const facilities = getFacilities();
  if (!(id in facilities)) throw new Error("Invalid facility ID: " + id);

  const sh = ensureTab(TAB_DATA, HEADERS, null, true);
  const lock = LockService.getScriptLock();
  lock.waitLock(10000);

  try {
    const readings = batch.readings || [];
    for (let i = 0; i < readings.length; i++) {
      const r = readings[i];
      const t = Number(r.temperature), h = Number(r.humidity), snd = Number(r.sound_level);
      const ldr = Number(r.light_level);

      if (!isFinite(t) || !isFinite(h) || !isFinite(snd)) throw new Error("Invalid sensor data");

      const c = getSettings();
      const fac = facilities[id];
      const status = {
        temp: tempStatus(t, c),
        hum: humStatus(h, c),
        noise: soundLevel(snd),
        comfort: comfortStatus(t, h, c)
      };

      const row = [new Date(), id, fac.name, fac.category,
                   Math.round(t * 10) / 10, Math.round(h * 10) / 10, snd,
                   status.temp, status.hum, status.noise, status.comfort,
                   "", "BATCH", "", "",
                   occupancyCell_(r.occupancy),               // Occupancy Level from Board 1
                   lightLevelCell_(r.light_level),            // Light Level (raw) from Board 1
                   "", "", ""];                               // Ambient, Lighting, Fan: not sent in batches

      sh.appendRow(row);
    }
    return { success: true, facility: id, count: readings.length };
  } finally { lock.releaseLock(); }
}

// ================== DAILY SUMMARY ==================
// dailySummary()  -> summarises YESTERDAY (used by the nightly trigger)
// summaryToday()  -> summarises TODAY (for testing)
function dailySummary(daysAgo) {
  if (typeof daysAgo !== "number") { daysAgo = 1; }    // the trigger passes an event object
  const tz = Session.getScriptTimeZone();
  const target = new Date(Date.now() - daysAgo * 86400000);
  const targetStr = Utilities.formatDate(target, tz, "dd/MM/yy");

  const data = ss().getSheetByName(TAB_DATA).getDataRange().getValues();
  const groups = {};
  for (let i = 1; i < data.length; i++) {
    const r = data[i];
    if (!(r[0] instanceof Date)) continue;
    if (Utilities.formatDate(r[0], tz, "dd/MM/yy") !== targetStr) continue;
    const id = r[1];
    if (!groups[id]) {
      groups[id] = { name: r[2], n: 0, tSum: 0, tMin: 999, tMax: -999, hSum: 0, hMax: -999, sSum: 0, comfy: 0 };
    }
    const g = groups[id];
    const t = Number(r[4]), h = Number(r[5]), s = Number(r[6]);
    g.n = g.n + 1;
    g.tSum = g.tSum + t; if (t < g.tMin) { g.tMin = t; } if (t > g.tMax) { g.tMax = t; }
    g.hSum = g.hSum + h; if (h > g.hMax) { g.hMax = h; }
    g.sSum = g.sSum + s;
    if (r[10] === "COMFY") { g.comfy = g.comfy + 1; }
  }

  const out = ensureTab(TAB_SUMMARY, SUMMARY_HEADERS, null, true);
  let written = 0;
  for (const id in groups) {
    const g = groups[id];
    out.appendRow([targetStr, id, g.name, g.n,
                   round1(g.tSum / g.n), g.tMin, g.tMax,
                   round1(g.hSum / g.n), g.hMax,
                   Math.round(g.sSum / g.n),
                   round1(g.comfy / g.n * 100)]);
    written = written + 1;
  }
  return "Summary for " + targetStr + ": " + written + " facilities";
}

function summaryToday() { Logger.log(dailySummary(0)); }

function createDailyTrigger() {
  const triggers = ScriptApp.getProjectTriggers();
  for (let i = 0; i < triggers.length; i++) {
    if (triggers[i].getHandlerFunction() === "dailySummary") { ScriptApp.deleteTrigger(triggers[i]); }
  }
  ScriptApp.newTrigger("dailySummary").timeBased().everyDays(1).atHour(0).nearMinute(10).create();
  return "Daily summary trigger set for ~00:10";
}

// ================== HELPERS & TESTS ==================
function json(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
function round1(x) { return Math.round(x * 10) / 10; }

// Run from the editor to test without the Arduino
function testPost() {
  const fake = { postData: { contents: JSON.stringify({
    api_key: PropertiesService.getScriptProperties().getProperty("ENV_API_KEY"),
    facility_id: "E2L2M", temperature: 25.0, humidity: 61.0, sound_level: 245,
    hvac: "LOW", lighting: "ON", fan_speed: 40, data_source: "TEST"
  })}};
  Logger.log(doPost(fake).getContent());
}
