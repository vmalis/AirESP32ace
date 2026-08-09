#!/bin/sh
# AirESP32ace
# Created by VM, August 2026

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
output="${TMPDIR:-/tmp}/airspace-route-direction-test"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"$root/firmware/airspace-instrument/main" \
  "$root/firmware/airspace-instrument/main/route_direction.c" \
  "$root/firmware/airspace-instrument/tests/route_direction_test.c" \
  -lm \
  -o "$output"

"$output"
