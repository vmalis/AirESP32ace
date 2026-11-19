# AirESP32ace
# Created by VM, August 2026

"""Build the offline city fallback from an explicit OurAirports CSV snapshot.

No network requests or credentials. Source: https://ourairports.com/data/
Public domain. Only unambiguous IATA/ICAO codes of open airports are indexed;
local/GPS identifiers are not interchangeable with these namespaces.
"""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
rows = []
codes = defaultdict(list)
with args.source.open(encoding="utf-8", newline="") as source:
    for row in csv.DictReader(source):
        if row["type"] in ("closed", "closed_airport", "balloonport"):
            continue
        city = row["municipality"].split(" (")[0].strip()
        aliases = [row.get("iata_code", "").strip(), row.get("icao_code", "").strip()]
        aliases = [code for code in aliases if code.isascii() and code.isalpha() and len(code) in (3, 4)]
        if not city or not aliases:
            continue
        entry = {"city": city, "country": row["iso_country"],
                 "latitude": float(row["latitude_deg"]),
                 "longitude": float(row["longitude_deg"])}
        rows.append(entry)
        for code in set(aliases):
            codes[code].append(len(rows) - 1)
catalog = {code: rows[indices[0]] for code, indices in sorted(codes.items()) if len(indices) == 1}
payload = {"source": "https://ourairports.com/data/", "license": "Public domain",
           "sourceSHA256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
           "airports": catalog}
args.output.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"Generated {len(catalog)} unambiguous codes; excluded {sum(len(v) > 1 for v in codes.values())} ambiguous codes.")
