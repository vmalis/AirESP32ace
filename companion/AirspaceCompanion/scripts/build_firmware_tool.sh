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
  --noconfirm --clean \
  --distpath "$BUILD_DIR/dist" --workpath "$BUILD_DIR/work" \
  "$SCRIPT_DIR/firmware_tool.spec"
# Inspect decompressed entries: a plain strings/ZIP scan of the signed
# executable cannot find a private pip RECORD inside its compressed archive.
"$PYTHON" - "$BUILD_DIR/dist/AirESP32aceFirmwareTool" <<'PY'
from pathlib import Path
import sys
from PyInstaller.archive.readers import CArchiveReader
archive = CArchiveReader(sys.argv[1])
home = str(Path.home()).encode() + b"/"
for name, entry in archive.toc.items():
    path = Path(name)
    if path.parent.name.endswith(".dist-info") and path.name in ("RECORD", "direct_url.json"):
        raise SystemExit("USB helper contains private installer metadata")
    if home in archive.extract(name):
        raise SystemExit("USB helper contains an absolute home path")
    if entry[-1] == "z":
        modules = archive.open_embedded_archive(name)
        if any(home in (modules.extract(key, raw=True) or b"") for key in modules.toc):
            raise SystemExit("USB helper contains an absolute home path in a Python module")
PY
"$BUILD_DIR/dist/AirESP32aceFirmwareTool" flash version
"$BUILD_DIR/dist/AirESP32aceFirmwareTool" nvs --help >/dev/null
