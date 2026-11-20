"""
Board 2 simulation test over MQTT (no Board 1 needed).

Sends SIMULATION commands to Board 2 and checks its energy/* replies.
Run it on any PC with paho-mqtt installed:   python board2_sim_test.py

Board 2 publishes every ~2 s, so each step waits SETTLE_S before reading the newest values.
"""
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "broker.emqx.io"      # same broker as Board 1 and Board 2
PORT = 1883
PREFIX = "sitg7/room1/"
CLIENT_ID = "sitg7-test-01"    # must differ from sitg7-weibin and sitg7-energy-uno02
SETTLE_S = 5

latest = {}


def on_connect(client, userdata, flags, reason_code, properties=None):
    client.subscribe(PREFIX + "energy/#")


def on_message(client, userdata, msg):
    latest[msg.topic[len(PREFIX) + len("energy/"):]] = msg.payload.decode(errors="replace")


def send(client, name, value):
    client.publish(PREFIX + "cmd/" + name, value, qos=0)


# (description, commands to send, expected values)
# expected keys: hvac, hvac_detail, fan, lighting, brightness
STEPS = [
    ("SIM on, vacant, dark, crowd 1 -> everything off",
     [("mode", "SIMULATION"), ("light", "AUTO"), ("fan", "AUTO"), ("scenario", "NORMAL"),
      ("crowd", "1"), ("occupancy", "VACANT"), ("ambient", "DARK")],
     dict(hvac="OFF", hvac_detail="OFF", fan="0", lighting="OFF")),
    ("Occupied, dark, crowd 1, normal air -> LOW 70%, LED on",
     [("occupancy", "OCCUPIED")],
     dict(hvac="LOW", hvac_detail="LOW", fan="70", lighting="ON")),
    ("Crowd 2, normal air -> LOW-MEDIUM 78%",
     [("crowd", "2")],
     dict(hvac="MEDIUM", hvac_detail="LOW-MEDIUM", fan="78", lighting="ON")),
    ("Crowd 2, HOT -> HIGH 100%",
     [("scenario", "HOT")],
     dict(hvac="HIGH", hvac_detail="HIGH", fan="100")),
    ("Crowd 3, HOT (temp high, humidity normal) -> MEDIUM-HIGH 92%",
     [("crowd", "3")],
     dict(hvac="HIGH", hvac_detail="MEDIUM-HIGH", fan="92")),
    ("Crowd 3, NORMAL air -> MEDIUM 85%",
     [("scenario", "NORMAL")],
     dict(hvac="MEDIUM", hvac_detail="MEDIUM", fan="85")),
    ("Crowd 3, HUMID -> HIGH 100%",
     [("scenario", "HUMID")],
     dict(hvac="HIGH", hvac_detail="HIGH", fan="100")),
    ("Bright room -> LED off, fan unchanged",
     [("ambient", "BRIGHT")],
     dict(lighting="OFF", fan="100")),
    ("Manual light ON in a bright room -> LED on",
     [("light", "ON")],
     dict(lighting="ON", fan="100")),
    ("Light back to AUTO -> LED off (bright)",
     [("light", "AUTO")],
     dict(lighting="OFF")),
    ("Manual fan 50% -> fan 50, HVAC level unchanged",
     [("fan", "50")],
     dict(fan="50", hvac="HIGH")),
    ("Fan back to AUTO -> fan follows the table again",
     [("fan", "AUTO")],
     dict(fan="100")),
    ("Brightness 25% -> reported brightness 25 while LED is on",
     [("ambient", "DARK"), ("brightness", "25")],
     dict(lighting="ON", brightness="25")),
    ("Vacant again -> LED off, fan 0, HVAC OFF (no 2-minute hold in SIMULATION)",
     [("occupancy", "VACANT")],
     dict(hvac="OFF", fan="0", lighting="OFF")),
]


def main():
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
    except AttributeError:
        client = mqtt.Client(client_id=CLIENT_ID)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, PORT, 60)
    client.loop_start()
    time.sleep(2)

    # check that Board 2 is online at all before running the steps
    time.sleep(SETTLE_S)
    if "mode" not in latest:
        print("No energy/* messages from Board 2. Is it powered, running this sketch, and on the same broker?")
        client.loop_stop()
        sys.exit(1)

    failures = 0
    for description, commands, expected in STEPS:
        for name, value in commands:
            send(client, name, value)
        time.sleep(SETTLE_S)

        bad = []
        for key, want in expected.items():
            got = latest.get(key, "<none>")
            if got != want:
                bad.append(f"{key}: expected {want}, got {got}")
        if bad:
            failures += 1
            print(f"FAIL  {description}")
            for line in bad:
                print(f"        {line}")
        else:
            print(f"PASS  {description}")

    client.loop_stop()
    client.disconnect()
    print(f"\n{len(STEPS) - failures} of {len(STEPS)} steps passed.")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
