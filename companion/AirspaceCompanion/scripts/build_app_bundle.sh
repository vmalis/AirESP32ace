#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPOSITORY_DIR="$(cd "$PACKAGE_DIR/../.." && pwd)"
APP_NAME="AirESP32ace"
FIRMWARE_RELEASE_NAME="AirESP32ace_181126_v02.bin"
BUNDLE_ID="com.vadimmalis.airspace-companion"
RENDERER_ID="$BUNDLE_ID.renderer"
BUILD_CONFIG="${1:-debug}"
FIRMWARE_BUILD_DIR="${AIRSPACE_FIRMWARE_BUILD_DIR:-$REPOSITORY_DIR/firmware/airspace-instrument/build}"
if [[ "$BUILD_CONFIG" == "release" ]]; then
  if [[ ! -f "$FIRMWARE_BUILD_DIR/config/sdkconfig.h" ]] || \
     grep -Eq '^#define CONFIG_AIRSPACE_WIFI_(SSID|PASSWORD) "[^"].*"' \
       "$FIRMWARE_BUILD_DIR/config/sdkconfig.h"; then
    echo "error: release requires verified credential-free firmware; use a clean AIRSPACE_FIRMWARE_BUILD_DIR." >&2
    exit 1
  fi
fi
BUILD_DEVELOPER_DIR="${DEVELOPER_DIR:-$(/usr/bin/xcode-select -p)}"
BUILD_SWIFT="$(DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" /usr/bin/xcrun --find swift)"
BUILD_SDK="$(DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" /usr/bin/xcrun --sdk macosx --show-sdk-path)"
NATIVE_BUILD_TYPE="Debug"
if [[ "$BUILD_CONFIG" == "release" ]]; then
  NATIVE_BUILD_TYPE="Release"
fi
NATIVE_SOURCE_DIR="$REPOSITORY_DIR/shared/native_renderer"
NATIVE_BUILD_DIR="$NATIVE_SOURCE_DIR/build"
SWIFT_SCRATCH_PATH=""
if [[ "$BUILD_CONFIG" == "release" ]]; then
  SWIFT_SCRATCH_PATH="/tmp/airspace-companion-release-build"
fi

cd "$PACKAGE_DIR"

# Flashing and first-time setup must work on a Mac with no developer tools.
bash "$SCRIPT_DIR/build_firmware_tool.sh"

cmake \
  -S "$NATIVE_SOURCE_DIR" \
  -B "$NATIVE_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$NATIVE_BUILD_TYPE" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT="$BUILD_SDK" \
  -DCMAKE_C_COMPILER="$(DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" /usr/bin/xcrun --find clang)" \
  -DCMAKE_CXX_COMPILER="$(DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" /usr/bin/xcrun --find clang++)"
cmake --build "$NATIVE_BUILD_DIR" --parallel

SWIFT_BUILD_ARGS=(
  build
  --disable-sandbox
  --configuration "$BUILD_CONFIG"
)
if [[ -n "$SWIFT_SCRATCH_PATH" ]]; then
  SWIFT_BUILD_ARGS+=(--scratch-path "$SWIFT_SCRATCH_PATH")
fi
env DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" \
  CLANG_MODULE_CACHE_PATH=/tmp/airspace_swift_module_cache \
  SWIFTPM_MODULECACHE_OVERRIDE=/tmp/airspace_swiftpm_module_cache \
  "$BUILD_SWIFT" \
  "${SWIFT_BUILD_ARGS[@]}"

BINARY_DIR=".build/arm64-apple-macosx/$BUILD_CONFIG"
# Newer Swift toolchains default to Swift Build and use out/Products, while
# older SwiftPM uses the target-triple directory. Never package a stale binary
# left in the old location after a toolchain upgrade.
PRODUCT_DIR="$(env DEVELOPER_DIR="$BUILD_DEVELOPER_DIR" \
  "$BUILD_SWIFT" "${SWIFT_BUILD_ARGS[@]}" --show-bin-path)"
BINARY_PATH="$PRODUCT_DIR/AirESP32ace"
APP_PATH="$BINARY_DIR/$APP_NAME.app"
CONTENTS_PATH="$APP_PATH/Contents"
MACOS_PATH="$CONTENTS_PATH/MacOS"
RESOURCES_PATH="$CONTENTS_PATH/Resources"
FRAMEWORKS_PATH="$CONTENTS_PATH/Frameworks"
HELPERS_PATH="$CONTENTS_PATH/Helpers"

rm -rf "$APP_PATH"
mkdir -p "$MACOS_PATH" "$RESOURCES_PATH" "$FRAMEWORKS_PATH" "$HELPERS_PATH"
cp "$PACKAGE_DIR/.build/firmware-tool/dist/AirESP32aceFirmwareTool" "$HELPERS_PATH/"

cp "$BINARY_PATH" "$MACOS_PATH/AirESP32ace"
chmod +x "$MACOS_PATH/AirESP32ace"

RESOURCE_BUNDLE_NAME="${APP_NAME}_AirspaceCompanion.bundle"
RESOURCE_BUNDLE="$PRODUCT_DIR/$RESOURCE_BUNDLE_NAME"
if [[ ! -d "$RESOURCE_BUNDLE" ]]; then
  echo "error: SwiftPM resource bundle is missing: $RESOURCE_BUNDLE" >&2
  exit 1
fi

# Keep nested resources in the standard macOS location. AppResources resolves
# this packaged location before using SwiftPM's build-directory fallback.
cp -R "$RESOURCE_BUNDLE" "$RESOURCES_PATH/"
if [[ ! -f "$RESOURCE_BUNDLE/AirportCities.json" && \
      ! -f "$RESOURCE_BUNDLE/Contents/Resources/AirportCities.json" ]]; then
  echo "error: newly built resource bundle is missing the airport city catalog" >&2
  exit 1
fi

cp \
  "$NATIVE_BUILD_DIR/libAirspaceRenderer.dylib" \
  "$FRAMEWORKS_PATH/libAirspaceRenderer.dylib"
# Airline logos are fetched/cached on demand, not bulk redistributed.
rsync -a --exclude airline_logos/ --exclude cargo_logos/ \
  "$REPOSITORY_DIR/assets/" "$RESOURCES_PATH/assets/"
cp -R "$REPOSITORY_DIR/Cities" "$RESOURCES_PATH/cities"
cp "$PACKAGE_DIR/Sources/AirspaceCompanion/Resources/AppIcon.icns" "$RESOURCES_PATH/AppIcon.icns"
"${AIRSPACE_TOOL_BUILD_PYTHON:-$PACKAGE_DIR/.build/firmware-tool-venv/bin/python}" \
  "$SCRIPT_DIR/collect_firmware_tool_notices.py" \
  "$RESOURCES_PATH/ThirdParty/FirmwareTool"

# Ship the current tracker application as a simple one-file update, plus the
# bootloader and partition table needed only by the first-time setup wizard.
FIRMWARE_RESOURCES_DIR="$RESOURCES_PATH/Firmware"
if [[ -f "$FIRMWARE_BUILD_DIR/airspace_instrument.bin" && \
      -f "$FIRMWARE_BUILD_DIR/bootloader/bootloader.bin" && \
      -f "$FIRMWARE_BUILD_DIR/partition_table/partition-table.bin" ]]; then
  mkdir -p "$FIRMWARE_RESOURCES_DIR"
  cp "$FIRMWARE_BUILD_DIR/airspace_instrument.bin" \
    "$FIRMWARE_RESOURCES_DIR/airspace_instrument.bin"
  cp "$FIRMWARE_BUILD_DIR/airspace_instrument.bin" \
    "$FIRMWARE_RESOURCES_DIR/$FIRMWARE_RELEASE_NAME"
  cp "$FIRMWARE_BUILD_DIR/bootloader/bootloader.bin" \
    "$FIRMWARE_RESOURCES_DIR/bootloader.bin"
  cp "$FIRMWARE_BUILD_DIR/partition_table/partition-table.bin" \
    "$FIRMWARE_RESOURCES_DIR/partition-table.bin"
else
  echo "warning: firmware build outputs are missing; the in-app installer will be unavailable"
fi

DEVICE_ARTWORK="$REPOSITORY_DIR/output/guide/assets/device-states/device-clean.png"
if [[ -f "$DEVICE_ARTWORK" ]]; then
  mkdir -p "$RESOURCES_PATH/Device"
  cp "$DEVICE_ARTWORK" "$RESOURCES_PATH/Device/device-clean.png"
fi

cat > "$CONTENTS_PATH/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>AirESP32ace</string>
  <key>CFBundleIconFile</key>
  <string>AppIcon</string>
  <key>CFBundleIdentifier</key>
  <string>$BUNDLE_ID</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>$APP_NAME</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.4</string>
  <key>CFBundleVersion</key>
  <string>5</string>
  <key>LSMinimumSystemVersion</key>
  <string>15.0</string>
  <key>LSApplicationCategoryType</key>
  <string>public.app-category.utilities</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSLocalNetworkUsageDescription</key>
  <string>AirESP32ace connects to your paired instrument over Wi-Fi to sync flights, maps, and settings and read its live status.</string>
  <key>NSAppTransportSecurity</key>
  <dict>
    <key>NSAllowsLocalNetworking</key>
    <true/>
    <key>NSExceptionDomains</key>
    <dict>
      <key>10.0.0.0/8</key>
      <dict><key>NSExceptionAllowsInsecureHTTPLoads</key><true/></dict>
      <key>172.16.0.0/12</key>
      <dict><key>NSExceptionAllowsInsecureHTTPLoads</key><true/></dict>
      <key>192.168.0.0/16</key>
      <dict><key>NSExceptionAllowsInsecureHTTPLoads</key><true/></dict>
    </dict>
  </dict>
</dict>
</plist>
PLIST

# Local development builds use ad-hoc signatures only. No signing identity or
# private key is accessed, so rebuilding cannot trigger authorization dialogs.
find "$APP_PATH" -name '.DS_Store' -type f -delete
find "$APP_PATH" -name '._*' -type f -delete
if [[ "$BUILD_CONFIG" == "release" ]]; then
  # Swift and native symbol tables retain absolute source paths. They are not
  # needed by the distributable app and must not expose the build machine.
  strip -S "$MACOS_PATH/AirESP32ace"
  strip -S "$FRAMEWORKS_PATH/libAirspaceRenderer.dylib"
  "${AIRSPACE_TOOL_BUILD_PYTHON:-$PACKAGE_DIR/.build/firmware-tool-venv/bin/python}" \
    "$REPOSITORY_DIR/scripts/audit_release_privacy.py" --app "$APP_PATH"
fi
codesign --force --sign - \
  --identifier "$RENDERER_ID" \
  --timestamp=none \
  "$FRAMEWORKS_PATH/libAirspaceRenderer.dylib"
codesign --force --sign - \
  --identifier "$BUNDLE_ID.firmware-tool" \
  --timestamp=none \
  "$HELPERS_PATH/AirESP32aceFirmwareTool"
codesign --force --sign - \
  --identifier "$BUNDLE_ID" \
  --timestamp=none \
  "$APP_PATH"
codesign --verify --deep --strict "$APP_PATH"

echo "$PACKAGE_DIR/$APP_PATH"
