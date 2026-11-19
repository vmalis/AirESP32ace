#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
health_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
health_json="${IDF_PATH:-$health_root/tools/esp-idf}/components/json/cJSON"
health_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-flightaware-test.XXXXXX")"
# Compile the production function itself with a fake HTTP transport/config.
# Stop at its unindented closing brace; fail if the definition cannot be found.
awk '
  /^int app_tracker_flightaware_probe_status\(void\)$/ { copying = 1; found++ }
  copying { print }
  copying && /^}$/ { copying = 0; finished++ }
  END { if (found != 1 || finished != 1) exit 1 }
' "$health_root/firmware/airspace-instrument/main/tracker.c" \
  > "$health_test_dir/flightaware_health_under_test.inc"
cc -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined \
  -I"$health_json" -c "$health_json/cJSON.c" -o "$health_test_dir/cJSON.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$health_json" -I"$health_test_dir" -I"$health_root/firmware/airspace-instrument/main" \
  "$health_root/firmware/airspace-instrument/tests/flightaware_health_test.c" \
  "$health_test_dir/cJSON.o" -o "$health_test_dir/health_test"
"$health_test_dir/health_test"
