// Fake Apps Script reply for testing the map without any boards (open index.html?mock=1).
// Same shape as one entry of ?action=latest. The map wobbles the counts and temperatures a little every 15 s.
window.SIT_MOCK_LIVE = [
  {facility_id: "FCA",   current_occupancy: 48, capacity: 60, light_on: true,  fan_pct: 80, hvac: "HIGH",   temperature: 29.4, humidity: 68, comfort: "HOT"},
  {facility_id: "FCB",   current_occupancy: 18, capacity: 60, light_on: true,  fan_pct: 40, hvac: "LOW",    temperature: 26.9, humidity: 64, comfort: "WARM"},
  {facility_id: "LIB",   current_occupancy: 31, capacity: 40, light_on: true,  fan_pct: 50, hvac: "MEDIUM", temperature: 24.2, humidity: 58, comfort: "COMFY"},
  {facility_id: "E2L5A", current_occupancy: 4,  capacity: 30, light_on: true,  fan_pct: 20, hvac: "LOW",    temperature: 23.1, humidity: 55, comfort: "COOL"},
  {facility_id: "E2L5B", current_occupancy: 0,  capacity: 30, light_on: false, fan_pct: 0,  hvac: "OFF",    temperature: 25.0, humidity: 74, comfort: "HUMID"},
  {facility_id: "GYM",   current_occupancy: 22, capacity: 30, light_on: true,  fan_pct: 100, hvac: "HIGH",  temperature: 27.8, humidity: 66, comfort: "WARM"},
  {facility_id: "W1L5",  current_occupancy: 12, capacity: 50, light_on: true,  fan_pct: 30, hvac: "LOW",    temperature: 24.8, humidity: 60, comfort: "COMFY"}
];
