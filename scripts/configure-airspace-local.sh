#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdkconfig="${repo_root}/firmware/airspace-instrument/sdkconfig"

if [[ ! -f "${sdkconfig}" ]]; then
  echo "Missing ${sdkconfig}"
  echo "Run this once first:"
  echo "  ./scripts/idf.sh firmware/airspace-instrument build"
  exit 1
fi

read -r -p "Wi-Fi SSID: " airspace_setup_ssid
IFS= read -r -s -p "Wi-Fi password: " airspace_setup_password
echo
read -r -p "Location label [San Francisco]: " airspace_setup_location
read -r -p "Latitude [37.7749]: " airspace_setup_lat
read -r -p "Longitude [-122.4194]: " airspace_setup_lon
read -r -p "POSIX timezone [PST8PDT,M3.2.0,M11.1.0]: " airspace_setup_tz

airspace_setup_location="${airspace_setup_location:-San Francisco}"
airspace_setup_lat="${airspace_setup_lat:-37.7749}"
airspace_setup_lon="${airspace_setup_lon:--122.4194}"
airspace_setup_tz="${airspace_setup_tz:-PST8PDT,M3.2.0,M11.1.0}"

export airspace_setup_ssid
export airspace_setup_password
export airspace_setup_location
export airspace_setup_lat
export airspace_setup_lon
export airspace_setup_tz

python3 - "${sdkconfig}" <<'PY'
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text().splitlines()

values = {
    "CONFIG_AIRSPACE_WIFI_SSID": os.environ["airspace_setup_ssid"],
    "CONFIG_AIRSPACE_WIFI_PASSWORD": os.environ["airspace_setup_password"],
    "CONFIG_AIRSPACE_LOCATION_LABEL": os.environ["airspace_setup_location"],
    "CONFIG_AIRSPACE_LOCATION_LAT": os.environ["airspace_setup_lat"],
    "CONFIG_AIRSPACE_LOCATION_LON": os.environ["airspace_setup_lon"],
    "CONFIG_AIRSPACE_TZ_POSIX": os.environ["airspace_setup_tz"],
}

def quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

seen = set()
out = []
for line in lines:
    replaced = False
    for key, value in values.items():
        if line.startswith(key + "="):
            out.append(f"{key}={quote(value)}")
            seen.add(key)
            replaced = True
            break
    if not replaced:
        out.append(line)

for key, value in values.items():
    if key not in seen:
        out.append(f"{key}={quote(value)}")

path.write_text("\n".join(out) + "\n")
PY

echo "Saved local Airspace config to:"
echo "  ${sdkconfig}"
echo
echo "Next:"
echo "  ./scripts/idf.sh firmware/airspace-instrument build"
echo "  ./scripts/idf.sh firmware/airspace-instrument -p /dev/cu.usbmodem114301 flash monitor"
