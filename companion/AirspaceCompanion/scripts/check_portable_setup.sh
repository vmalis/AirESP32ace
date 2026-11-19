#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026
set -euo pipefail
PACKAGE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d /private/tmp/airesp-portable-test.XXXXXX)"
# Compile the production value type without the app UI or XCTest dependency.
awk '/^struct DeviceProvisioningConfiguration:/{copy=1} /^@MainActor/{if(copy)exit} copy' \
  "$PACKAGE/Sources/AirspaceCompanion/DeviceManagement.swift" > "$TEST_DIR/Configuration.swift"
xcrun swiftc -parse-as-library -module-cache-path "$TEST_DIR/modules" \
  "$TEST_DIR/Configuration.swift" "$PACKAGE/scripts/provisioning_smoke.swift" \
  -o "$TEST_DIR/provisioning"
"$TEST_DIR/provisioning" "$PACKAGE/.build/firmware-tool/dist/AirESP32aceFirmwareTool"
xcrun swiftc -parse-as-library -module-cache-path "$TEST_DIR/modules" \
  "$PACKAGE/Sources/AirspaceCompanion/Models.swift" \
  "$PACKAGE/Sources/AirspaceCompanion/SharedConfiguration.swift" \
  "$PACKAGE/Sources/AirspaceCompanion/FlightStatusProvider.swift" \
  "$PACKAGE/scripts/city_catalog_smoke.swift" -o "$TEST_DIR/cities"
"$TEST_DIR/cities"
