#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu
launch_package="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
launch_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airspace-launch-swift.XXXXXX")"
xcrun swiftc -parse-as-library \
  -module-cache-path "$launch_test_dir/modules" \
  "$launch_package/Sources/AirspaceCompanion/LaunchModels.swift" \
  "$launch_package/Sources/AirspaceCompanion/DeviceLaunchSnapshot.swift" \
  "$launch_package/scripts/launch_snapshot_smoke.swift" \
  -o "$launch_test_dir/check-launch-snapshot"
"$launch_test_dir/check-launch-snapshot"
