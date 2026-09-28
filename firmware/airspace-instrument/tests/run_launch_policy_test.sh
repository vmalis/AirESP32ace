#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
launch_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
launch_json="${IDF_PATH:-$launch_root/tools/esp-idf}/components/json/cJSON"
launch_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-launch-test.XXXXXX")"
cc -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined \
  -I"$launch_json" -c "$launch_json/cJSON.c" -o "$launch_test_dir/cJSON.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$launch_root/firmware/airspace-instrument/main" -I"$launch_json" \
  "$launch_root/firmware/airspace-instrument/main/launch_policy.c" \
  "$launch_root/firmware/airspace-instrument/tests/launch_policy_test.c" \
  "$launch_test_dir/cJSON.o" -o "$launch_test_dir/launch_test"
"$launch_test_dir/launch_test" "$@"
