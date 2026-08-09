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

The exact 466 × 466 preview hosts the same LVGL screen renderer used by the firmware. Resolve the ESP-IDF components with one firmware build, then:

```bash
./companion/AirspaceCompanion/scripts/build_app_bundle.sh debug
open "companion/AirspaceCompanion/.build/arm64-apple-macosx/debug/AirESP32ace.app"
```

Use `release` instead of `debug` for an optimized local bundle. The script uses ad-hoc signing and does not require an Apple Developer certificate.

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
4. After the tracker restarts and joins Wi-Fi, verify that the companion reports it as reachable.
5. Add up to eight radar cities, choose clock cities, and sync. The companion generates and uploads the required maps.

## Common issues

### The companion builds but the exact preview is unavailable

Build the firmware once so ESP-IDF downloads LVGL and the Waveshare board component, then rebuild the `.app` bundle.

### The device is reachable but maps are missing

Open the relevant companion section and sync it. Map bitmaps are generated locally and deliberately excluded from Git.

### Credentials show a red cross

The app runs a minimal authorization probe. Confirm the key/client credentials in the provider dashboard, including whether the provider has disabled an otherwise syntactically valid key.

### Flash usage or RAM changes unexpectedly

Use the ESP-IDF size report and test screen switching repeatedly on hardware. Most large artwork should be stored as device files rather than linked into the application image.
