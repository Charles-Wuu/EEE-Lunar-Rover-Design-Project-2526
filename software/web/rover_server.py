from flask import Flask, jsonify, request, send_from_directory
import threading
import json
import os
import time
from urllib import request as urlrequest
from urllib.error import URLError

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

app = Flask(__name__)

ARDUINO_URL = os.environ.get("ARDUINO_URL", "http://172.20.10.2").rstrip("/")
HTTP_PORT = int(os.environ.get("PORT", "5050"))
HTTP_HOST = os.environ.get("HOST", "0.0.0.0")

latest = {
    "mag":         {"raw": 512, "voltage": 1.65, "polarity": "unknown"},
    "ir":          {"count": 0, "lambda_val": 0.0, "ir_class": "low"},
    "us":          {"detected": False},
    "rf":          {"packet": "--", "age": "--"},
    "rock_type":   "unknown",
    "drive_cmd":   "stop",
    "last_action": "none"
}

def classify():
    ir_high = latest["ir"]["lambda_val"] >= 430
    us_on   = latest["us"]["detected"]
    mag     = latest["mag"]["polarity"]
    if  ir_high and     us_on and mag == "down": return "Basaltoid"
    if not ir_high and not us_on and mag == "down": return "Gravion"
    if not ir_high and     us_on and mag == "up":   return "Regolix"
    if  ir_high and not us_on and mag == "up":   return "Lunarite"
    return "unknown"

def read_network():
    data_url = f"{ARDUINO_URL}/data"
    print(f"[NETWORK] Reading detection data from {data_url}")

    while True:
        try:
            with urlrequest.urlopen(data_url, timeout=1.5) as response:
                payload = json.loads(response.read().decode("utf-8"))

            latest["mag"].update(payload.get("mag", {}))
            latest["ir"].update(payload.get("ir", {}))
            latest["us"].update(payload.get("us", {}))
            latest["rf"].update(payload.get("rf", {}))
            latest["rock_type"] = payload.get("rock_type") or classify()
            print(f"[NETWORK] ok rock={latest['rock_type']} rf_age={latest['rf'].get('age')}")

        except (URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
            print(f"[NETWORK] Could not read Arduino data: {e}")

        time.sleep(0.5)

@app.route("/drive", methods=["POST"])
def drive():
    data = request.get_json(silent=True) or {}
    cmd = data.get("cmd", "stop")
    allowed = {"forward", "backward", "left", "right", "spinleft", "spinright", "stop", "on", "off"}
    if cmd not in allowed:
        cmd = "stop"
    latest["drive_cmd"] = cmd
    print(f"[DRIVE] {cmd}")
    try:
        urlrequest.urlopen(f"{ARDUINO_URL}/{cmd}", timeout=1.5).read()
    except (URLError, TimeoutError, OSError) as e:
        print(f"[DRIVE] Could not send {cmd} to Arduino: {e}")
    return jsonify({"ok": True, "cmd": cmd})

@app.route("/action", methods=["POST"])
def action():
    data = request.get_json(silent=True) or {}
    cmd = data.get("cmd", "none")
    latest["last_action"] = cmd
    print(f"[ACTION] {cmd}")
    return jsonify({"ok": True, "cmd": cmd})

@app.route("/data")
def data():
    return jsonify(latest)

@app.route("/detect.html")
def detect_page():
    return send_from_directory(BASE_DIR, "detect.html")

@app.route("/control.html")
def control_page():
    return send_from_directory(BASE_DIR, "control.html")

@app.route("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")

if __name__ == "__main__":
    threading.Thread(target=read_network, daemon=True).start()
    app.run(host=HTTP_HOST, port=HTTP_PORT, debug=True, use_reloader=False)
