#!/bin/sh
# AirESP32ace
# Created by VM, August 2026

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
output="${TMPDIR:-/tmp}/airspace-aircraft-type-registry-test"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"$root/firmware/airspace-instrument/main" \
  "$root/firmware/airspace-instrument/main/aircraft_type_registry.c" \
  "$root/firmware/airspace-instrument/tests/aircraft_type_registry_test.c" \
  -o "$output"

"$output"
