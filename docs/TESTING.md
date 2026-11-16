# Testing and demo checklist (everyone)

## W01: agree the data contract
- [ ] Post the field table from `README.md` in the group chat.
- [ ] Every board owner confirms their `facility_id` is in the Facilities tab and in `facilities.csv`.
- [ ] Capacity filled in on the Facilities tab for each place with a counter.
- [ ] Agree what `cmd/mode` means and whether topics use `room1` or the facility ID (see W11).

## W14: simulated data and commands (Weibin)
| # | Do | Expect | Pass? |
|---|---|---|---|
| 1 | Open `index.html?mock=1` | How busy list says "Simulated live data"; spots show live counts | |
| 2 | Console: `pushLive([{facility_id:"FCA",current_occupancy:58,capacity:60}])` | E4 Food Court turns red, label "Packed 58/60" | |
| 3 | Console: `pushLive([{facility_id:"FCA",current_occupancy:10,capacity:60}])` | Turns green, "Quiet 10/60" | |
| 4 | `pushLive([{facility_id:"FCA",light_on:false,fan_pct:0,hvac:"OFF"}])` | Panel: Lights OFF, Fan 0%, HVAC OFF | |
| 5 | `pushLive([{facility_id:"FCA",temperature:29.5,humidity:60}])` | Temperature tile red, "Hot" | |
| 6 | Apps Script: run `testLive`, wait 15 s on the real map (Weibin) | FCA, LIB, E2L5A update without reloading | |
| 7 | Change `LIVE_API` to a wrong URL (or turn Wi-Fi off) | Red "Can't reach the live data" / "Offline" note; map still works | |
| 8 | Press each room control (after W10) | Command appears in IoT MQTT Panel on `sitg7/+/cmd/#`; panel shows Manual mode | |

## W27: two-board link (Weibin)
| # | Do | Expect | Pass? |
|---|---|---|---|
| 1 | Power both boards, wait 60 s (PIR warm-up) | Board 1 LCD page 2 shows `LED:... FAN:...%`; Board 2 built-in LED on | |
| 2 | Cover the light sensor and wave at the PIR | Board 2 LED turns on within about a second | |
| 3 | Uncover the light sensor | LED off within a few seconds | |
| 4 | Stay still for more than 3 s | LED off | |
| 5 | Type 1, 2, 3 in Board 1's Serial Monitor | Fan speeds up; LCD shows the HVAC level | |
| 6 | Type 0 | Fan stops, HVAC OFF | |
| 7 | Unplug Board 1 for 30 s | Board 2 switches LED and fan off after 20 s | |
| 8 | Unplug Board 2 for 15 s | Board 1 LCD shows `Board 2: no link` | |

## W16: end-to-end with all boards live (everyone)
| Board | Action | Expect on the website within about 30 s | Pass? |
|---|---|---|---|
| Crowd (Nasrah) | Walk in through the doorway 3 times | Count +3, colour changes at 35 / 65 / 85% | |
| Crowd (Nasrah) | Walk out once | Count -1 | |
| Board 1 (Weibin) | Breathe on the DHT11 | Temperature or humidity rises; comfort label may change | |
| Board 1 + 2 | Cover the light sensor and wave at the PIR | Lights ON on the map | |
| Board 1 + 2 | Change the crowd level | Fan % and HVAC level change | |
| Any | Unplug a board for 2 min | That part's badge turns red: "No new reading..." | |

## W17: fix list
| Problem found | Who | Fixed? |
|---|---|---|
| | | |

## W19: demo run-through
- [ ] Phone hotspot on; every board on the hotspot and powered
- [ ] Apps Script `?action=latest` opens and shows every facility
- [ ] Website open on the laptop and a phone, How busy list showing "Live from N sensors"
- [ ] IoT MQTT Panel app open on `sitg7/room1/#`
- [ ] Backup: `index.html?mock=1` open in another tab, plus screenshots and a short screen recording
- [ ] Who talks and who walks through the doorway decided

## Common problems
| Symptom | Fix |
|---|---|
| "Can't reach the live data" | Apps Script deployment access must be **Anyone**; redeploy as **New version** after edits |
| A facility never shows | Its `facility_id` isn't in `CROWD_SPOTS` (index.html) or the Facilities tab, or the spelling differs |
| Count shows but no colour | No capacity: fill the Facilities tab Capacity column, or send `capacity` |
| Board gets `Invalid API key` | Its `api_key` must equal the `ENV_API_KEY` script property |
| Board 1 LCD says `Board 2: no link` | Board 2 is off, on a different hotspot, or not on the broker (its built-in LED is off) |
| LED never turns on | PIR still warming up (60 s), or the light thresholds on Board 2 need recalibrating |
| Old values stay after a test | Run `clearLiveTest` in Apps Script |
