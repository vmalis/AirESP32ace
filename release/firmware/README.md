# Prebuilt firmware

These binaries target the **Waveshare ESP32-S3-Touch-AMOLED-1.75C** with 32 MB flash. The application matches this repository's firmware source and was built with blank compiled Wi-Fi credentials. Personal settings and API keys are provisioned separately, never embedded in these downloads.

## Files and offsets

| Offset | File | Purpose |
|---:|---|---|
| `0x0000` | `bootloader.bin` | ESP32-S3 bootloader |
| `0x8000` | `partition-table.bin` | AirESP32ace partition layout |
| `0x10000` | `AirESP32ace_181126_v01.bin` | v0.1.3 main application |

## First installation

Install ESP-IDF 5.5+ or `esptool`, connect the board with a data-capable USB-C cable, and identify its serial port. Then run from this directory:

```bash
esptool.py --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX \
  --baud 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 32MB \
  0x0000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 AirESP32ace_181126_v01.bin
```

If automatic download mode fails, hold the lower BOOT button while starting the flash, then release it once the serial connection begins.

## Application-only update

When the bootloader and partition table are already installed, only the application needs to be replaced:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x10000 AirESP32ace_181126_v01.bin
```

The companion's firmware panel can perform the same application update after it confirms the connected USB device.

The application-only update preserves existing configuration and stored assets. Use the matching v0.1.3 companion. This release fixes FlightAware validation for active Personal-tier keys by using the no-cost account-usage endpoint instead of the tier-restricted alerts endpoint. Changed keys are revalidated asynchronously. It also adds storage-capacity diagnostics and cleanup of retired 5/100-mile radar cache files while preserving live maps, plus stronger flight-queue verification. Autonomous tracking/launch updates remain device-owned. See [autonomous space launches](../../docs/autonomous-space-launches.md) for the underlying provider policy.

## Configuration

The binaries contain no personal Wi-Fi, hotspot, or provider credentials. Use the macOS companion's **Set Up New Device** flow to enter them, then sync cities, maps, preferences, tracked flights, and Dock Mode.
