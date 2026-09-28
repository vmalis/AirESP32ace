#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
state_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
state_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-remote-state-test.XXXXXX")"
cc -std=c11 -Wall -Wextra -Werror -Wframe-larger-than=256 \
  -I"$state_root/firmware/airspace-instrument/main" \
  -c "$state_root/firmware/airspace-instrument/main/remote_state.c" \
  -o "$state_test_dir/remote_state.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$state_root/firmware/airspace-instrument/main" \
  "$state_root/firmware/airspace-instrument/main/remote_state.c" \
  "$state_root/firmware/airspace-instrument/tests/remote_state_test.c" \
  -o "$state_test_dir/remote_state_test"
"$state_test_dir/remote_state_test"
