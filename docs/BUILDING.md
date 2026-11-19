# Building from source

## Mac companion

### Requirements

- macOS 15 or later
- Xcode and Xcode Command Line Tools
- Apple silicon recommended

### Fast development run

```bash
cd companion/AirspaceCompanion
swift run AirESP32ace
```

Swift Package Manager compiles and opens the companion. The first build takes longer; later builds are incremental.

### Full app bundle and exact device preview

The exact 466 × 466 preview hosts the same LVGL screen renderer used by the firmware. Resolve the ESP-IDF components with one firmware build. For the bundled installer, create an arm64 Python virtual environment compatible with the macOS 15 deployment target, install the pinned build requirements, and include the matching esptool source archive:

```bash
python3 -m venv companion/AirspaceCompanion/.build/firmware-tool-venv
companion/AirspaceCompanion/.build/firmware-tool-venv/bin/pip install \
  -r companion/AirspaceCompanion/scripts/firmware_tool_requirements.txt
companion/AirspaceCompanion/.build/firmware-tool-venv/bin/pip download \
  --no-deps --no-binary=:all: esptool==4.12.0 \
  -d companion/AirspaceCompanion/.build/firmware-tool/sources
```

Then build the bundle:

```bash
./companion/AirspaceCompanion/scripts/build_app_bundle.sh debug
open "companion/AirspaceCompanion/.build/arm64-apple-macosx/debug/AirESP32ace.app"
```

Use `release` instead of `debug` for an optimized local bundle. Release builds require firmware with blank compiled Wi-Fi SSID/password fields and fail closed otherwise; `AIRSPACE_FIRMWARE_BUILD_DIR` selects a separate clean firmware build. The script uses ad-hoc signing and does not require an Apple Developer certificate. The finished app includes its runtime and does not require these build tools on the receiving Mac. Inspect the package before publishing with `python3 scripts/audit_release_privacy.py --git --archive <app.zip>`.

## Firmware

### Requirements

- ESP-IDF 5.5 or later
- Python environment installed by ESP-IDF
- Data-capable USB-C cable

Activate ESP-IDF, then run:

```bash
./scripts/idf.sh firmware/airspace-instrument set-target esp32s3
./scripts/idf.sh firmware/airspace-instrument menuconfig
./scripts/idf.sh firmware/airspace-instrument build
```

Flash and monitor:

```bash
./scripts/idf.sh firmware/airspace-instrument \
  -p /dev/cu.usbmodemXXXX flash monitor
```

If automatic download mode fails, hold the lower BOOT button while connecting USB-C, start the flash operation, then release it after the serial connection begins.

## First setup

1. Configure home Wi-Fi, the optional phone hotspot, and API credentials in the companion.
2. Connect the tracker by USB-C.
3. Open the companion's device-outline button and choose **Set Up New Device**.
4. After the tracker restarts and joins Wi-Fi, enter its assigned IP in the companion's Settings and click **Check**.
5. Choose **Install device content** and wait for the verification receipt. It installs all three radar-map ranges for each selected city plus Weather/Space artwork; retries are safe.
6. Keep at most eight radar cities. Use **Replace city** for optional Charlotte/Columbia choices, then sync.

First-time setup erases the entire board. An ordinary firmware update preserves its settings and content. On another Mac, use USB-C Settings sync to change Wi-Fi; blank API fields preserve provider credentials already on the device.

## Common issues

### The companion builds but the exact preview is unavailable

Build the firmware once so ESP-IDF downloads LVGL and the Waveshare board component, then rebuild the `.app` bundle.

### The device is reachable but maps are missing

For a newly provisioned board, use **Settings → Install device content**. For a changed city, rebuild/sync its maps. Map bitmaps are generated locally and deliberately excluded from Git.

### Credentials show a red cross

Install v0.1.3 or newer: older firmware used FlightAware's tier-restricted alerts endpoint and could show a false red cross for an active Personal-tier key. The app and firmware now use the no-cost account-usage authorization probe. A genuinely rejected key still shows a red cross; replacing it triggers device-side revalidation. Check provider status/credentials if the corrected build still reports an error.

### Flash usage or RAM changes unexpectedly

Use the ESP-IDF size report and test screen switching repeatedly on hardware. Most large artwork should be stored as device files rather than linked into the application image.
