# AirESP32ace for macOS

`AirESP32ace-macOS.zip` contains the ready-to-run **AirESP32ace.app** for Apple-silicon Macs.

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
