# Testing and demo checklist (both members)

## W01: agree the data contract (day 1, 11/11/26)
- [ ] Post the field table from `README.md` in the group chat.
- [ ] Every board owner confirms their `facility_id` is in the Facilities tab and in `facilities.csv`.
- [ ] Capacity filled in on the Facilities tab for each place with a counter.

## W11: simulated data (day 3, 13/11/26)
| # | Do | Expect | Pass? |
|---|---|---|---|
| 1 | Open `index.html?mock=1` | How busy list says "Simulated live data"; 7 spots show live counts | |
| 2 | Console: `pushLive([{facility_id:"FCA",current_occupancy:58,capacity:60}])` | E4 Food Court turns red, label "Packed 58/60" | |
| 3 | Console: `pushLive([{facility_id:"FCA",current_occupancy:10,capacity:60}])` | Turns green, "Quiet 10/60" | |
| 4 | `pushLive([{facility_id:"FCA",light_on:false,fan_pct:0,hvac:"OFF"}])` | Panel: Lights OFF, Fan 0%, HVAC OFF | |
| 5 | `pushLive([{facility_id:"FCA",temperature:29.5,humidity:60}])` | Temperature tile red, "Hot" | |
| 6 | Apps Script: run `testLive`, wait 15 s on the real map | FCA, LIB, E2L5A update without reloading | |
| 7 | Change `LIVE_API` to a wrong URL (or turn Wi-Fi off) | Red "Can't reach the live data" / "Offline" note; map still works | |

## W13: end-to-end with all boards live (day 4, 14/11/26)
| Board | Action | Expect on the website within about 30 s | Pass? |
|---|---|---|---|
| Crowd | Walk in through the doorway 3 times | Count +3, colour changes at 35 / 65 / 85% | |
| Crowd | Walk out once | Count -1 | |
| PIR lighting | Wave at the PIR | Lights ON; OFF after the hold time | |
| Fan / HVAC | Change the fan setting | Fan % and HVAC level change | |
| Temp / humidity | Breathe on the DHT22 | Temperature or humidity rises; comfort label may change | |
| Any | Unplug a board for 2 min | That part's badge turns red: "No new reading..." | |

## W14: fix list
| Problem found | Who | Fixed? |
|---|---|---|
| | | |

## W16: demo run-through
- [ ] Phone hotspot on; every board on the hotspot and powered
- [ ] Apps Script `?action=latest` opens and shows every facility
- [ ] Website open on the laptop and a phone, How busy list showing "Live from N sensors"
- [ ] Backup: `index.html?mock=1` open in another tab, plus screenshots and a short screen recording
- [ ] Who talks and who walks through the doorway decided

## Common problems
| Symptom | Fix |
|---|---|
| "Can't reach the live data" | Apps Script deployment access must be **Anyone**; redeploy as **New version** after edits |
| A facility never shows | Its `facility_id` isn't in `CROWD_SPOTS` (index.html) or the Facilities tab, or the spelling differs |
| Count shows but no colour | No capacity: fill the Facilities tab Capacity column, or send `capacity` |
| Board gets `Invalid API key` | Its `api_key` must equal the `ENV_API_KEY` script property |
| ESP32 POST returns -1 or 302 | Use `HTTPC_FORCE_FOLLOW_REDIRECTS` and `WiFiClientSecure` with `setInsecure()` (as in the example) |
| Old values stay after a test | Run `clearLiveTest` in Apps Script |
