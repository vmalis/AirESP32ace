#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
tracker_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
tracker_json="${IDF_PATH:-$tracker_root/tools/esp-idf}/components/json/cJSON"
tracker_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-tracker-test.XXXXXX")"
cc -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined \
  -I"$tracker_json" -c "$tracker_json/cJSON.c" -o "$tracker_test_dir/cJSON.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$tracker_root/firmware/airspace-instrument/main" -I"$tracker_json" \
  "$tracker_root/firmware/airspace-instrument/main/tracker_policy.c" \
  "$tracker_root/firmware/airspace-instrument/main/tracker_storage.c" \
  "$tracker_root/firmware/airspace-instrument/tests/tracker_policy_test.c" \
  "$tracker_test_dir/cJSON.o" -lm -o "$tracker_test_dir/tracker_test"
"$tracker_test_dir/tracker_test"
