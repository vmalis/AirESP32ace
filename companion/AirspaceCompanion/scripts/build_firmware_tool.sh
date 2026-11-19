#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PACKAGE_DIR/.build/firmware-tool"
PYTHON="${AIRSPACE_TOOL_BUILD_PYTHON:-$PACKAGE_DIR/.build/firmware-tool-venv/bin/python}"

if [[ ! -x "$PYTHON" ]]; then
  echo "Create .build/firmware-tool-venv with an arm64 Python, then install scripts/firmware_tool_requirements.txt." >&2
  exit 1
fi
mkdir -p "$BUILD_DIR"
export PYINSTALLER_CONFIG_DIR="$BUILD_DIR/cache"
"$PYTHON" -m PyInstaller \
  --noconfirm --clean --onefile --console --target-arch arm64 \
  --name AirESP32aceFirmwareTool \
  --distpath "$BUILD_DIR/dist" --workpath "$BUILD_DIR/work" \
  --specpath "$BUILD_DIR" \
  --collect-all esptool --collect-all esp_idf_nvs_partition_gen \
  "$SCRIPT_DIR/firmware_tool.py"
"$BUILD_DIR/dist/AirESP32aceFirmwareTool" flash version
"$BUILD_DIR/dist/AirESP32aceFirmwareTool" nvs --help >/dev/null
