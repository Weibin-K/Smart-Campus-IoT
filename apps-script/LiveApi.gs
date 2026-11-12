// =====================================================================================
//  LIVE MAP API  v2 (tracker W04)
//  v2, 12/11/26: reads Nasrah's Crowd Management tab (Max Occupancy, Booking, Data Source),
//  falls back to the Environment tab when there is no latest_<ID> property, finds the header row
//  even under a title row, adds brightness / mode for the room controls, and adds checkW04().
//  One JSON for the campus map: crowd + energy + environment per facility.
//
//  Where it goes: the Environment Logger Apps Script project (the one with Code.gs,
//  SHEET_ID, getFacilities(), json() and saveLatest()). Add it as a NEW file:
//  Files > + > Script > name it LiveApi. It uses those helpers, so don't paste it
//  into a different project.
//
//  Then make 2 small edits in Code.gs (exact lines in README_MEMBER2.md):
//    doGet:  if (action === "latest") { return json(liveLatest(e.parameter.facility)); }
//    doPost: if (p.type === "crowd" || p.type === "energy") { return json(livePost(p)); }
//
//  Record returned per facility (any part can be missing until its board posts):
//    facility_id, facility_name, category,
//    current_occupancy, capacity, occupancy_pct, crowd_status,      <- Crowd Management
//    light_on (true/false), fan_pct (0-100), hvac (OFF/LOW/MEDIUM/HIGH) <- Energy Saving
//    temperature, humidity, comfort, sound_level                      <- Environment
//    time ("dd/MM/yy HH:mm:ss" of the newest part), ts, crowd_ts, energy_ts, env_ts (ms)
// =====================================================================================

// ---------- settings you may need to change ----------
const LIVE_CROWD_SHEET_ID = "";            // "" = same spreadsheet as SHEET_ID. Else the crowd sheet's ID
const LIVE_ENERGY_SHEET_ID = "";           // "" = same spreadsheet as SHEET_ID
const LIVE_TAB_CROWD = "Crowd Management";
const LIVE_TAB_ENERGY = "Energy Saving";
const LIVE_TAB_ENV = "Environment";
const LIVE_EXPECT = ["FCA", "FCB", "LIB", "STB", "GYM", "DR1", "DR2", "DR3"];   // W04 is done when all of these show
const LIVE_SCAN_ROWS = 400;                // how many of the newest rows to read in each tab
const LIVE_CACHE_SECONDS = 10;             // many phones polling every 15 s share one sheet read
const LIVE_LOG = true;                     // livePost also appends a row to the two log tabs below
const LIVE_TAB_CROWD_LOG = "Crowd Live Log";
const LIVE_TAB_ENERGY_LOG = "Energy Live Log";

// Header names to look for in each tab's header row, case does not matter.
// An exact match wins, then a header that contains the text. Put your real header first if it differs.
// Fields are matched in the order listed, and each column is used once, so the more specific
// headers (Booking End Time, Booking Status) are claimed before the general ones (Timestamp, Status).
const LIVE_COLUMNS = {
  crowd: {
    facility:   ["facility id", "facility_id", "facility", "location id", "location"],
    bookingEnd: ["booking end time", "booking end", "booking_end"],
    booking:    ["booking status", "booking_status", "booking"],
    source:     ["data source", "data_source", "source"],
    time:       ["timestamp", "last updated", "time", "date"],
    pct:        ["occupancy (%)", "occupancy %", "occupancy_pct", "percent", "%"],
    status:     ["crowd status", "crowd_status", "status"],
    count:      ["current occupancy", "current_occupancy", "people inside", "occupancy", "count", "people"],
    capacity:   ["max occupancy", "max_occupancy", "capacity", "max capacity"]
  },
  energy: {
    facility:   ["facility id", "facility_id", "facility", "location id", "location"],
    time:       ["timestamp", "time", "date"],
    bright:     ["brightness (%)", "brightness", "light level"],
    mode:       ["control mode", "mode"],
    light:      ["light status", "light_on", "lights", "light", "led", "pir"],
    fan:        ["fan speed (%)", "fan_pct", "fan %", "fan speed", "fan"],
    hvac:       ["hvac setting", "hvac level", "hvac"]
  },
  env: {
    facility:   ["facility id", "facility_id", "facility", "location id", "location"],
    time:       ["timestamp", "time", "date"],
    temp:       ["temperature (°c)", "temperature (c)", "temperature", "temp"],
    hum:        ["humidity (%)", "humidity", "rh"],
    sound:      ["sound level", "sound", "noise"],
    comfort:    ["comfort status", "comfort"],
    hvac:       ["hvac setting", "hvac level", "hvac"],
    fan:        ["fan speed (%)", "fan_pct", "fan speed", "fan"]
  }
};

// ---------- GET ?action=latest ----------
function liveLatest(oneFacility) {
  const one = oneFacility ? String(oneFacility).trim().toUpperCase() : "";
  const cache = CacheService.getScriptCache();
  let all = null;
  const hit = cache.get("live_all");
  if (hit) { all = JSON.parse(hit); }
  if (!all) {
    all = buildLive_();
    try { cache.put("live_all", JSON.stringify(all), LIVE_CACHE_SECONDS); } catch (err) { }   // over 100 KB: skip the cache
  }
  if (one) {
    const f = all.facilities.filter(r => r.facility_id === one)[0];
    return f || { error: "No data yet for " + one };
  }
  return all;
}

function buildLive_() {
  const tz = Session.getScriptTimeZone();
  const facilities = getFacilities();                         // from Code.gs: the Facilities tab
  const props = PropertiesService.getScriptProperties().getProperties();
  const out = {};
  function rec(id) {
    if (!out[id]) {
      const f = facilities[id] || {};
      out[id] = { facility_id: id, facility_name: f.name || id, category: f.category || "" };
      if (Number(f.capacity) > 0) { out[id].capacity = Number(f.capacity); }
    }
    return out[id];
  }

  // 1. environment: latest_<ID>, written by saveLatest() in Code.gs
  for (const k in props) {
    if (k.indexOf("latest_") !== 0) { continue; }
    const v = safeParse_(props[k]);
    if (!v || !v.facility_id) { continue; }
    const r = rec(String(v.facility_id).toUpperCase());
    ["temperature", "humidity", "sound_level", "comfort", "temp_status", "humidity_status", "noise_status"]
      .forEach(f => { if (v[f] !== undefined && v[f] !== "") { r[f] = v[f]; } });
    if (v.hvac) { r.hvac = v.hvac; }
    r.env_ts = v.ts || parseTime_(v.time, tz);
  }
  // 1b. environment from the Environment tab, for any facility without a newer latest_<ID>
  const envRows = newestRows_(SHEET_ID, LIVE_TAB_ENV, LIVE_COLUMNS.env, tz);
  for (const id in envRows) {
    const g = envRows[id], r = rec(id);
    if (r.env_ts && g.ts && r.env_ts >= g.ts) { continue; }
    if (toNum_(g.temp) !== null) { r.temperature = toNum_(g.temp); }
    if (toNum_(g.hum) !== null) { r.humidity = toNum_(g.hum); }
    if (toNum_(g.sound) !== null) { r.sound_level = toNum_(g.sound); }
    if (g.comfort) { r.comfort = String(g.comfort).trim().toUpperCase(); }
    if (g.hvac) { r.hvac = normHvac_(g.hvac); }
    if (toNum_(g.fan) !== null) { r.fan_pct = Math.max(0, Math.min(100, Math.round(toNum_(g.fan)))); }
    r.env_ts = g.ts || r.env_ts || null;
  }

  // 2. crowd: newest row per facility in the Crowd Management tab, and crowd_<ID> from livePost
  const crowd = newestRows_(LIVE_CROWD_SHEET_ID || SHEET_ID, LIVE_TAB_CROWD, LIVE_COLUMNS.crowd, tz);
  for (const k in props) {
    if (k.indexOf("crowd_") === 0) {
      const v = safeParse_(props[k]);
      if (v && (!crowd[v.facility_id] || v.ts >= crowd[v.facility_id].ts)) { crowd[v.facility_id] = v; }
    }
  }
  for (const id in crowd) {
    const c = crowd[id], r = rec(id);
    const count = toNum_(c.count !== undefined ? c.count : c.current_occupancy);
    const cap = toNum_(c.capacity);
    let pct = toNum_(c.pct !== undefined ? c.pct : c.occupancy_pct);
    if (cap > 0) { r.capacity = cap; }
    if (count !== null) { r.current_occupancy = count; }
    if (pct !== null && pct <= 1 && count !== null && r.capacity && count / r.capacity > 0.02) { pct = pct * 100; }
    if (pct === null && count !== null && r.capacity > 0) { pct = 100 * count / r.capacity; }
    if (pct !== null) { r.occupancy_pct = Math.round(pct); }
    const st = String(c.status || c.crowd_status || "").trim().toUpperCase();
    r.crowd_status = st || (pct === null ? "" : crowdStatus_(pct));
    if (c.booking) { r.booking = String(c.booking).trim().toUpperCase().replace(/[\s-]+/g, "_"); }
    if (c.bookingEnd) { r.booking_end = c.bookingEnd instanceof Date ? Utilities.formatDate(c.bookingEnd, tz, "HH:mm") : String(c.bookingEnd); }
    if (c.source) { r.source = String(c.source).trim().toUpperCase(); }
    r.crowd_ts = c.ts || null;
  }

  // 3. energy: newest row per facility in the Energy Saving tab, and energy_<ID> from livePost
  const energy = newestRows_(LIVE_ENERGY_SHEET_ID || SHEET_ID, LIVE_TAB_ENERGY, LIVE_COLUMNS.energy, tz);
  for (const k in props) {
    if (k.indexOf("energy_") === 0) {
      const v = safeParse_(props[k]);
      if (v && (!energy[v.facility_id] || v.ts >= energy[v.facility_id].ts)) { energy[v.facility_id] = v; }
    }
  }
  for (const id in energy) {
    const g = energy[id], r = rec(id);
    const light = onOff_(g.light !== undefined ? g.light : g.light_on);
    if (light !== null) { r.light_on = light; }
    const fan = toNum_(g.fan !== undefined ? g.fan : g.fan_pct);
    if (g.bright === undefined && g.brightness !== undefined) { g.bright = g.brightness; }
    if (fan !== null) { r.fan_pct = Math.max(0, Math.min(100, Math.round(fan))); }
    const hv = normHvac_(g.hvac);
    if (hv) { r.hvac = hv; }
    const br = toNum_(g.bright !== undefined ? g.bright : g.brightness);
    if (br !== null) { r.brightness = Math.max(0, Math.min(100, Math.round(br))); }
    const md = String(g.mode || "").trim().toUpperCase();
    if (md === "AUTO" || md === "MANUAL") { r.mode = md; }
    r.energy_ts = g.ts || null;
  }

  const list = [];
  for (const id in out) {
    const r = out[id];
    const ts = Math.max(r.crowd_ts || 0, r.energy_ts || 0, r.env_ts || 0);
    if (ts) { r.ts = ts; r.time = Utilities.formatDate(new Date(ts), tz, "dd/MM/yy HH:mm:ss"); }
    list.push(r);
  }
  list.sort((a, b) => a.facility_id < b.facility_id ? -1 : 1);
  return { count: list.length, facilities: list,
           generated: Utilities.formatDate(new Date(), tz, "dd/MM/yy HH:mm:ss") };
}

// ---------- POST {type: "crowd" | "energy", ...} from a board ----------
//   crowd:  {api_key, type: "crowd",  facility_id: "FCA", current_occupancy: 42, capacity: 60}
//   energy: {api_key, type: "energy", facility_id: "FCA", light_on: true, fan_pct: 70, hvac: "HIGH"}
//   (send only the parts your board knows; the rest stays as it was)
function livePost(p) {
  const props = PropertiesService.getScriptProperties();
  const keys = [props.getProperty("ENV_API_KEY"), props.getProperty("CROWD_API_KEY"), props.getProperty("ENERGY_API_KEY")].filter(k => k);
  if (!keys.length || keys.indexOf(p.api_key) === -1) { return { success: false, error: "Invalid API key" }; }
  const id = String(p.facility_id || "").trim().toUpperCase();
  const facilities = getFacilities();
  if (!(id in facilities)) { return { success: false, error: "Invalid facility ID: " + id + " (add it to the Facilities tab)" }; }
  const now = Date.now(), name = facilities[id].name;
  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    const key = p.type + "_" + id;
    const v = safeParse_(props.getProperty(key)) || { facility_id: id };
    if (p.type === "crowd") {
      const count = toNum_(p.current_occupancy);
      if (count === null || count < 0 || count > 10000 || Math.round(count) !== count) { return { success: false, error: "Invalid current_occupancy" }; }
      v.count = count;
      const cap = toNum_(p.capacity);
      if (cap !== null) {
        if (cap <= 0 || cap > 10000) { return { success: false, error: "Invalid capacity" }; }
        v.capacity = cap;
      }
      if (p.crowd_status) { v.status = String(p.crowd_status).toUpperCase(); }
      v.ts = now;
      if (LIVE_LOG) {
        const capNow = v.capacity || Number(facilities[id].capacity) || "";
        logRow_(LIVE_TAB_CROWD_LOG, ["Timestamp", "Facility ID", "Facility Name", "Current Occupancy", "Capacity", "Occupancy (%)", "Data Source"],
                [new Date(now), id, name, count, capNow, capNow ? Math.round(100 * count / capNow) : "", String(p.data_source || "SENSOR")]);
      }
    } else {
      if (p.light_on !== undefined) {
        const l = onOff_(p.light_on);
        if (l === null) { return { success: false, error: "Invalid light_on (true/false or ON/OFF)" }; }
        v.light = l;
      }
      if (p.fan_pct !== undefined) {
        const f = toNum_(p.fan_pct);
        if (f === null || f < 0 || f > 100) { return { success: false, error: "Invalid fan_pct (0-100)" }; }
        v.fan = f;
      }
      if (p.brightness !== undefined) {
        const b = toNum_(p.brightness);
        if (b === null || b < 0 || b > 100) { return { success: false, error: "Invalid brightness (0-100)" }; }
        v.bright = b;
      }
      if (p.mode !== undefined) {
        const m = String(p.mode).toUpperCase();
        if (m !== "AUTO" && m !== "MANUAL") { return { success: false, error: "Invalid mode (AUTO or MANUAL)" }; }
        v.mode = m;
      }
      if (p.hvac !== undefined) {
        const h = normHvac_(p.hvac);
        if (["OFF", "LOW", "MEDIUM", "HIGH"].indexOf(h) === -1) { return { success: false, error: "Invalid hvac" }; }
        v.hvac = h;
      }
      v.ts = now;
      if (LIVE_LOG) {
        logRow_(LIVE_TAB_ENERGY_LOG, ["Timestamp", "Facility ID", "Facility Name", "Light", "Fan (%)", "HVAC", "Data Source"],
                [new Date(now), id, name, v.light === undefined ? "" : (v.light ? "ON" : "OFF"),
                 v.fan === undefined ? "" : v.fan, v.hvac || "", String(p.data_source || "SENSOR")]);
      }
    }
    props.setProperty(key, JSON.stringify(v));
    CacheService.getScriptCache().remove("live_all");
  } finally { lock.releaseLock(); }
  return { success: true, facility: id, type: p.type };
}

// ---------- helpers ----------
function newestRows_(sheetId, tab, cols, tz) {
  const found = {};
  let sh;
  try { sh = SpreadsheetApp.openById(sheetId).getSheetByName(tab); } catch (err) { return found; }
  if (!sh) { return found; }
  const last = sh.getLastRow(), width = sh.getLastColumn();
  if (last < 2 || width < 1) { return found; }
  // the header row is the first of the top 5 rows that has a facility column (a title row may sit above it)
  const top = sh.getRange(1, 1, Math.min(5, last), width).getValues();
  let headRow = 0;
  for (let i = 0; i < top.length; i++) {
    const t = top[i].map(h => String(h).trim().toLowerCase());
    if (cols.facility.some(n => t.indexOf(n) >= 0)) { headRow = i; break; }
  }
  const head = top[headRow].map(h => String(h).trim().toLowerCase());
  const used = {}, at = {};
  for (const field in cols) {                 // fields in LIVE_COLUMNS order; a column is used once
    at[field] = -1;
    const names = cols[field];
    for (let pass = 0; pass < 2 && at[field] < 0; pass++) {
      for (let n = 0; n < names.length && at[field] < 0; n++) {
        for (let i = 0; i < head.length; i++) {
          if (used[i] || !head[i]) { continue; }
          if (pass === 0 ? head[i] === names[n] : head[i].indexOf(names[n]) >= 0) { at[field] = i; used[i] = true; break; }
        }
      }
    }
  }
  if (at.facility < 0) { return found; }
  const start = Math.max(headRow + 2, last - LIVE_SCAN_ROWS + 1);
  if (start > last) { return found; }
  const rows = sh.getRange(start, 1, last - start + 1, width).getValues();
  for (let r = rows.length - 1; r >= 0; r--) {   // newest (bottom) row first
    const id = String(rows[r][at.facility]).trim().toUpperCase();
    if (!id || found[id]) { continue; }
    if (!/^[A-Z0-9]{2,8}$/.test(id)) { continue; }        // skip notes and blank-looking rows
    const o = { facility_id: id, ts: at.time >= 0 ? cellTime_(rows[r][at.time], tz) : null };
    for (const field in at) {
      if (field !== "facility" && field !== "time" && at[field] >= 0 && rows[r][at[field]] !== "") { o[field] = rows[r][at[field]]; }
    }
    found[id] = o;
  }
  return found;
}

function logRow_(tab, headers, row) {
  const book = SpreadsheetApp.openById(SHEET_ID);
  let sh = book.getSheetByName(tab);
  if (!sh) { sh = book.insertSheet(tab); sh.getRange(1, 1, 1, headers.length).setValues([headers]); sh.setFrozenRows(1); }
  sh.appendRow(row);
}
function normHvac_(v) {
  const h = String(v === null || v === undefined ? "" : v).trim().toUpperCase();
  return h === "MED" || h === "MID" ? "MEDIUM" : h;
}
function crowdStatus_(pct) { return pct < 35 ? "QUIET" : pct < 65 ? "MODERATE" : pct < 85 ? "BUSY" : "PACKED"; }
function toNum_(v) {
  if (v === null || v === undefined || v === "") { return null; }
  const n = Number(String(v).replace("%", "").trim());
  return isFinite(n) ? n : null;
}
function onOff_(v) {
  if (v === true || v === 1) { return true; }
  if (v === false || v === 0) { return false; }
  const s = String(v === null || v === undefined ? "" : v).trim().toUpperCase();
  if (["ON", "TRUE", "1", "YES", "LIGHTS ON", "LIGHT ON"].indexOf(s) >= 0) { return true; }
  if (["OFF", "FALSE", "0", "NO", "LIGHTS OFF", "LIGHT OFF"].indexOf(s) >= 0) { return false; }
  return null;
}
function cellTime_(v, tz) {
  if (v instanceof Date) { return v.getTime(); }
  return parseTime_(v, tz);
}
function parseTime_(s, tz) {
  s = String(s || "").trim();
  if (!s) { return null; }
  s = s.replace(",", "");                           // "11/11/2026, 13:05:22" from toLocaleString
  const formats = ["dd/MM/yyyy HH:mm:ss", "dd/MM/yy HH:mm:ss", "dd/MM/yyyy HH:mm", "dd/MM/yy HH:mm", "yyyy-MM-dd HH:mm:ss"];
  for (let i = 0; i < formats.length; i++) {
    try { const d = Utilities.parseDate(s, tz, formats[i]); if (d && isFinite(d.getTime())) { return d.getTime(); } } catch (err) { }
  }
  const t = Date.parse(s);
  return isFinite(t) ? t : null;
}
function safeParse_(s) { try { return s ? JSON.parse(s) : null; } catch (err) { return null; } }

// ---------- tests: run these from the editor (W11) ----------
// Posts fake crowd and energy readings for 3 facilities, then logs what the map will get.
function testLive() {
  const key = PropertiesService.getScriptProperties().getProperty("ENV_API_KEY");
  const fake = [
    { type: "crowd",  facility_id: "FCA",   current_occupancy: 48, capacity: 60 },
    { type: "energy", facility_id: "FCA",   light_on: true,  fan_pct: 80, hvac: "HIGH" },
    { type: "crowd",  facility_id: "LIB",   current_occupancy: 12, capacity: 40 },
    { type: "energy", facility_id: "LIB",   light_on: true,  fan_pct: 30, hvac: "LOW" },
    { type: "crowd",  facility_id: "E2L5A", current_occupancy: 0,  capacity: 30 },
    { type: "energy", facility_id: "E2L5A", light_on: false, fan_pct: 0,  hvac: "OFF" }
  ];
  fake.forEach(f => { f.api_key = key; f.data_source = "TEST"; Logger.log(JSON.stringify(livePost(f))); });
  Logger.log(JSON.stringify(liveLatest(), null, 2));
}
// Shows the map's JSON without posting anything
function testLatest() { CacheService.getScriptCache().remove("live_all"); Logger.log(JSON.stringify(liveLatest(), null, 2)); }
// Shows which columns were found in each tab (check this first if a field is missing)
function testColumns() {
  const tz = Session.getScriptTimeZone();
  Logger.log("Crowd tab, newest per facility: " + JSON.stringify(newestRows_(LIVE_CROWD_SHEET_ID || SHEET_ID, LIVE_TAB_CROWD, LIVE_COLUMNS.crowd, tz)));
  Logger.log("Energy tab, newest per facility: " + JSON.stringify(newestRows_(LIVE_ENERGY_SHEET_ID || SHEET_ID, LIVE_TAB_ENERGY, LIVE_COLUMNS.energy, tz)));
}
// Removes the test values livePost saved (crowd_ and energy_ properties), so the map stops showing them
function clearLiveTest() {
  const props = PropertiesService.getScriptProperties(), all = props.getProperties();
  let n = 0;
  for (const k in all) { if (k.indexOf("crowd_") === 0 || k.indexOf("energy_") === 0) { props.deleteProperty(k); n++; } }
  CacheService.getScriptCache().remove("live_all");
  Logger.log("Removed " + n + " saved crowd/energy values");
}

// ---------- W04 check: run this from the editor, then read the log ----------
// Shows each tab's header row and columns found, which facilities the API returns, and which of
// LIVE_EXPECT are still missing (and why). W04 is done when it ends with "ALL FOUND".
function checkW04() {
  const tz = Session.getScriptTimeZone();
  CacheService.getScriptCache().remove("live_all");
  [[LIVE_CROWD_SHEET_ID || SHEET_ID, LIVE_TAB_CROWD, LIVE_COLUMNS.crowd],
   [SHEET_ID, LIVE_TAB_ENV, LIVE_COLUMNS.env],
   [LIVE_ENERGY_SHEET_ID || SHEET_ID, LIVE_TAB_ENERGY, LIVE_COLUMNS.energy]].forEach(function (t) {
    let sh = null;
    try { sh = SpreadsheetApp.openById(t[0]).getSheetByName(t[1]); }
    catch (err) { Logger.log("[" + t[1] + "] can't open spreadsheet " + t[0] + ": " + err); return; }
    if (!sh) { Logger.log("[" + t[1] + "] no tab with this exact name"); return; }
    const rows = newestRows_(t[0], t[1], t[2], tz), ids = Object.keys(rows).sort();
    Logger.log("[" + t[1] + "] " + sh.getLastRow() + " rows. Header row: " +
      JSON.stringify(sh.getRange(1, 1, Math.min(2, sh.getLastRow()), sh.getLastColumn()).getValues()));
    Logger.log("[" + t[1] + "] facilities found: " + (ids.length ? ids.join(", ") : "NONE (facility column not found?)"));
    if (ids.length) { Logger.log("[" + t[1] + "] example: " + JSON.stringify(rows[ids[0]])); }
  });
  const facs = getFacilities();
  const all = liveLatest();
  const got = all.facilities.map(r => r.facility_id);
  Logger.log("API returns " + all.count + ": " + got.join(", "));
  const missing = LIVE_EXPECT.filter(id => got.indexOf(id) < 0);
  missing.forEach(function (id) {
    Logger.log("MISSING " + id + ": " + (id in facs ? "in the Facilities tab, but no row for it in Crowd Management / Environment yet"
                                                    : "not in the Facilities tab and no row in the data tabs (see W21)"));
  });
  all.facilities.forEach(function (r) {
    if (r.crowd_ts && !r.capacity) { Logger.log("NOTE " + r.facility_id + ": has a count but no capacity, so the map can't colour it. Fill Max Occupancy or the Facilities tab."); }
    if (!r.ts) { Logger.log("NOTE " + r.facility_id + ": no timestamp found, so the map will show it as 'just now'."); }
  });
  Logger.log(missing.length ? "NOT DONE: " + missing.length + " missing" : "ALL FOUND: W04 data side is done. Now Deploy > Manage deployments > New version.");
}
