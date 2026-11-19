#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
storage_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
storage_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-storage-test.XXXXXX")"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$storage_root/firmware/airspace-instrument/main" \
  "$storage_root/firmware/airspace-instrument/main/storage_policy.c" \
  "$storage_root/firmware/airspace-instrument/tests/storage_policy_test.c" \
  -o "$storage_test_dir/storage_test"
"$storage_test_dir/storage_test"
