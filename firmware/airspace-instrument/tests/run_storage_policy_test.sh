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
# Include the unchanged IDF-facing implementation with fault-injected drivers.
sed -n '/^static size_t read_sector(/,$p' "$storage_root/firmware/airspace-instrument/main/storage_mount.c" > "$storage_test_dir/storage_mount_under_test.inc"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$storage_root/firmware/airspace-instrument/main" -I"$storage_test_dir" \
  "$storage_root/firmware/airspace-instrument/main/storage_policy.c" \
  "$storage_root/firmware/airspace-instrument/tests/storage_mount_test.c" \
  -o "$storage_test_dir/mount_test"
"$storage_test_dir/mount_test"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$storage_root/firmware/airspace-instrument/main" \
  "$storage_root/firmware/airspace-instrument/main/artwork_upload.c" \
  "$storage_root/firmware/airspace-instrument/tests/artwork_upload_test.c" \
  -o "$storage_test_dir/artwork_test"
(cd "$storage_test_dir" && ./artwork_test .)
sed -n '/^static int art_receive(/,/^}/p' "$storage_root/firmware/airspace-instrument/main/remote.c" > "$storage_test_dir/artwork_receive_under_test.inc"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$storage_test_dir" "$storage_root/firmware/airspace-instrument/tests/artwork_receive_test.c" \
  -o "$storage_test_dir/artwork_receive_test"
"$storage_test_dir/artwork_receive_test"
