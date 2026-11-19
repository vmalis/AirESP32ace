# AirESP32ace
# Created by VM, August 2026

"""Fail if known local credentials occur in distributables or git-visible files.

Reads secrets only for local byte comparisons; never prints their values.
This supplements, rather than replaces, review for unknown credentials.
"""
import argparse
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--app", type=Path)
parser.add_argument("--git", action="store_true")
parser.add_argument("--git-repo", type=Path,
                    help="Scan git-visible files in another checkout using this Mac's known secrets")
parser.add_argument("--archive", type=Path, action="append", default=[],
                    help="Inspect every decompressed ZIP member, not only compressed archive bytes")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
known = {}

credentials = Path.home() / "Library/Application Support/AirspaceCompanion/credentials.json"
if credentials.exists():
    known.update(json.loads(credentials.read_text()))
config = root / "firmware/airspace-instrument/sdkconfig"
if config.exists():
    for name, value in re.findall(r'^(CONFIG_AIRSPACE_WIFI_(?:SSID|PASSWORD))=(".*")$', config.read_text(), re.M):
        known[name] = json.loads(value)
result = subprocess.run(["defaults", "export", "com.vadimmalis.airspace-companion", "-"], capture_output=True)
if result.returncode == 0:
    preferences = plistlib.loads(result.stdout)
    for name in ("homeWiFiSSID", "hotspotWiFiSSID"):
        known[name] = preferences.get(name, "")
needles = {name: value.encode() for name, value in known.items() if isinstance(value, str) and len(value) >= 4}

paths = []
if args.app:
    paths += [path for path in args.app.rglob("*") if path.is_file()]
if args.git or args.git_repo:
    git_root = args.git_repo.resolve() if args.git_repo else root
    files = subprocess.check_output([
        "git", "ls-files", "-z", "--cached", "--others", "--exclude-standard",
        "--", ".", ":!0_photos", ":!HANDOFF.md"
    ], cwd=git_root)
    paths += [git_root / name.decode() for name in files.split(b"\0") if name]
failures = []
checked = 0
private_files = {"credentials.json", "airspace_config.json", "sdkconfig", "sdkconfig.old",
                 "device_config.csv", "device_config.bin", "device-config.csv", "device-config.bin"}

def inspect(name, data, packaged=False):
    global checked
    checked += 1
    matched = [label for label, needle in needles.items() if needle in data]
    if packaged and Path(name).name in private_files:
        matched.append("private configuration file")
    if matched:
        failures.append((str(name), matched))

for path in paths:
    if not path.is_file():
        continue
    inspect(str(path), path.read_bytes(), bool(args.app and path.is_relative_to(args.app)))
for archive in args.archive:
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if not entry.is_dir():
                inspect(f"{archive}!{entry.filename}", package.read(entry), packaged=True)
if failures:
    for path, names in failures:
        print("PRIVATE DATA MATCH:", path, ", ".join(names))
    sys.exit(1)
print(f"Privacy audit passed: {checked} files/members; no known local credential or network-name matches.")
