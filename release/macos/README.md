# AirESP32ace for macOS

`AirESP32ace-macOS.zip` contains **AirESP32ace.app v0.1.5** for Apple-silicon Macs running **macOS 15 or newer**. It includes the matching `AirESP32ace_191126_v03.bin` firmware, bootloader, partition table, native renderer, and self-contained USB flashing/NVS helper. The receiving Mac does not need Python, ESP-IDF, Homebrew, or Xcode.

Update both app and device firmware. Launch and tracked-flight updates now run on the ESP32; the companion prescribes programs/flights and reads the device's live state. Existing launch artwork can be retained. See [autonomous space launches](../../docs/autonomous-space-launches.md).

Version 0.1.5 pairs the Mac over USB for encrypted Wi-Fi control, checks the target and running firmware around an update, and inventories on-device artwork before repairing missing files. To keep existing content, use **Update Firmware**. Then use **Pair This Mac** over USB if wireless control is needed; pairing a different Mac replaces the previous authorization.

FlightAware Personal-tier validation, blank-key preservation, and the portable setup tools from v0.1.3 remain included. After **Set Up New Device**, enter the device's assigned IP under Settings and choose **Install device content**. **Update Firmware** preserves existing settings and artwork. Charlotte and Columbia (South Carolina) are optional replacements within the eight-city limit.

## Open the app

1. Download and unzip the archive.
2. Quit the old app, drag **AirESP32ace.app** into Applications (choose Replace), then open it.
3. If macOS still blocks it, open **System Settings → Privacy & Security** and choose **Open Anyway**.

The app is ad-hoc signed so its bundled native renderer loads correctly, but it is not Developer ID signed or notarized. No Apple Developer account is required. You can also build the same app directly from the published source.

## Verify the download

From Terminal, run:

```bash
shasum -a 256 AirESP32ace-macOS.zip
```

Compare the result with `SHA256SUMS` in this folder.

Configuration, Wi-Fi credentials, and API credentials are created locally after launch. They are not embedded in the archive.
