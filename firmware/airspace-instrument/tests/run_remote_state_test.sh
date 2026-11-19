#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
state_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
state_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-remote-state-test.XXXXXX")"
state_json="${IDF_PATH:-$state_root/tools/esp-idf}/components/json/cJSON"
cc -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined \
  -I"$state_json" -c "$state_json/cJSON.c" -o "$state_test_dir/cJSON.o"
cc -std=c11 -Wall -Wextra -Werror -Wframe-larger-than=256 \
  -I"$state_root/firmware/airspace-instrument/main" \
  -c "$state_root/firmware/airspace-instrument/main/remote_state.c" \
  -o "$state_test_dir/remote_state.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$state_root/firmware/airspace-instrument/main" -I"$state_json" \
  "$state_root/firmware/airspace-instrument/main/remote_state.c" \
  "$state_root/firmware/airspace-instrument/tests/remote_state_test.c" \
  "$state_test_dir/cJSON.o" -o "$state_test_dir/remote_state_test"
"$state_test_dir/remote_state_test"
