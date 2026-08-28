# Prebuilt firmware

These binaries target the **Waveshare ESP32-S3-Touch-AMOLED-1.75C** with 32 MB flash. They are built from this public repository's release-safe source, which uses the bundled LVGL fallback fonts and omits proprietary watch artwork and downloaded airline/cargo marks.

## Files and offsets

| Offset | File | Purpose |
|---:|---|---|
| `0x0000` | `bootloader.bin` | ESP32-S3 bootloader |
| `0x8000` | `partition-table.bin` | AirESP32ace partition layout |
| `0x10000` | `AirESP32ace_280826_v01.bin` | Main application |

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
  0x10000 AirESP32ace_280826_v01.bin
```

If automatic download mode fails, hold the lower BOOT button while starting the flash, then release it once the serial connection begins.

## Application-only update

When the bootloader and partition table are already installed, only the application needs to be replaced:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x10000 AirESP32ace_280826_v01.bin
```

The companion's firmware panel can perform the same application update after it confirms the connected USB device.

## Configuration

The binaries contain no personal Wi-Fi, hotspot, or provider credentials. Use the macOS companion's **Set Up New Device** flow to enter them, then sync cities, maps, preferences, tracked flights, and Dock Mode.
