import json
import urllib.request
import sys

sys.stdout.reconfigure(encoding="utf-8")
base = "https://data.etabus.gov.hk/v1/transport/kmb"
for bound in ["outbound", "inbound"]:
    with urllib.request.urlopen(f"{base}/route-stop/40X/{bound}/1", timeout=20) as r:
        data = json.load(r)["data"]
    print(f"=== {bound} ({len(data)}) ===")
    for i, item in enumerate(data):
        sid = item["stop"]
        with urllib.request.urlopen(f"{base}/stop/{sid}", timeout=20) as r:
            s = json.load(r)["data"]
        print(f"{i+1:2d}. {s.get('name_en','')} | {s.get('name_tc','')} | {sid}")
