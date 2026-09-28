# AirESP32ace for macOS

`AirESP32ace-macOS.zip` contains **AirESP32ace.app v0.1.2** for Apple-silicon Macs running **macOS 15 or newer**. It includes the matching `AirESP32ace_280926_v01.bin` firmware, bootloader, partition table, and native renderer.

Update both app and device firmware. Launch and tracked-flight updates now run on the ESP32; the companion prescribes programs/flights and reads the device's live state. Existing launch artwork can be retained. See [autonomous space launches](../../docs/autonomous-space-launches.md).

## Open the app

1. Download and unzip the archive.
2. Control-click **AirESP32ace.app** and choose **Open**.
3. If macOS still blocks it, open **System Settings → Privacy & Security** and choose **Open Anyway**.

The app is ad-hoc signed so its bundled native renderer loads correctly, but it is not Developer ID signed or notarized. No Apple Developer account is required. You can also build the same app directly from the published source.

## Verify the download

From Terminal, run:

```bash
shasum -a 256 AirESP32ace-macOS.zip
```

Compare the result with `SHA256SUMS` in this folder.

Configuration, Wi-Fi credentials, and API credentials are created locally after launch. They are not embedded in the archive.
