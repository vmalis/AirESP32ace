# Bring-Up Checklist

Target board: Waveshare ESP32-S3-Touch-AMOLED-1.75C.

This is the original prototype bring-up record. Its USB setup examples and
plaintext HTTP endpoints describe older firmware, not a v0.1.5 upgrade. On
a configured instrument use **Update Firmware** in the matching companion;
pair over USB for pinned HTTPS control. Do not erase a working board to
follow the historical first-install procedure below.

Known from Waveshare docs/repo:

- ESP32-S3R8
- 466 x 466 CO5300 QSPI AMOLED
- CST9217 capacitive touch
- AXP2101 power management
- QMI8658 IMU
- 8 MB PSRAM
- 32 MB NOR flash in the official repo/docs resources

## 1. Keep the factory firmware for now

Use the installed app as a hardware test:

- display lights correctly
- touch responds
- PWR long press powers off/wakes as expected
- BOOT behavior is not critical yet
- battery/USB behavior looks normal

Do not run `erase_flash` yet.

## 2. Install local tools

Minimum tools:

```sh
python3 -m pip install --user esptool
```

For building our ESP-IDF firmware, install ESP-IDF 5.5.x or newer. Waveshare CI builds this repo with ESP-IDF 5.5.4 and 6.0.2.

## 3. Find the serial port

Connect the board with a data-capable USB-C cable.

```sh
./scripts/list-ports.sh
```

Likely macOS port names:

```text
/dev/cu.usbmodem*
/dev/cu.usbserial*
```

## 4. Back up factory flash

Replace the port with your real one:

```sh
./scripts/backup-factory-flash.sh /dev/cu.usbmodemXXXX
```

This reads 32 MB from flash and writes a timestamped file under `backups/`.

If the board does not enter download mode automatically, hold BOOT while plugging in USB, then release BOOT after the tool connects.

## 5. Configure Wi-Fi, location, and time

The firmware reads these from ESP-IDF project config. Keep real credentials in the local
`firmware/airspace-instrument/sdkconfig` file, which is intentionally not for sharing.

The simplest local setup path is:

```sh
./scripts/configure-airspace-local.sh
```

It prompts for SSID/password/location and writes only the ignored local `sdkconfig`.

If you prefer the ESP-IDF UI:

```sh
./scripts/idf.sh firmware/airspace-instrument menuconfig
```

Open:

```text
AirESP32ace
```

Set:

- `Wi-Fi SSID`
- `Wi-Fi password`
- `Location label`
- `Location latitude`
- `Location longitude`
- `POSIX timezone`

For Pacific time, the default timezone is:

```text
PST8PDT,M3.2.0,M11.1.0
```

For Moscow:

```text
MSK-3
```

For Germany:

```text
CET-1CEST,M3.5.0,M10.5.0/3
```

## 6. Build and flash the first custom firmware

Use the helper so the bundled ESP-IDF is activated automatically:

```sh
./scripts/idf.sh firmware/airspace-instrument set-target esp32s3
./scripts/idf.sh firmware/airspace-instrument build
./scripts/idf.sh firmware/airspace-instrument -p /dev/cu.usbmodemXXXX flash monitor
```

Current expected custom UI:

- Radar screen first; system diagnostics are still available by cycling screens
- touch swipe/click and BOOT button move between screens
- tap a local-radar aircraft to open full-screen aircraft details
- hardware BOOT button exits aircraft details
- radar, tracked flight, weather, sky, and GMT prototype screens
- Wi-Fi uses the configured SSID directly; it does not scan nearby networks
- once Wi-Fi connects, the board starts a push receiver at:

```text
http://<board-ip>/api/status
```

Current push endpoints:

```text
GET  /api/status
POST /api/state   JSON: {"style":"xdrRadar|cartographicGlass|gmtChronograph","routeProgress":0.46}
POST /api/map     raw 466 x 466 RGB565, exactly 434312 bytes
```

The Mac companion's `Push to Device` button uses these endpoints.

## 7. Restore factory firmware if needed

Use your own backup if you made one:

```sh
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0x0 backups/<your-backup>.bin
```

Or use Waveshare's included factory recovery image:

```sh
./scripts/flash-factory-image.sh /dev/cu.usbmodemXXXX
```

## Project direction after bring-up

Once the display/touch/memory path is confirmed, the next technical steps are:

1. Split UI into screens: local radar, tracked flight, weather, sky, GMT.
2. Implement deterministic 466 x 466 vector mockups in LVGL.
3. Add Mac companion protocol for dynamic map/weather/flight backgrounds.
4. Build the live-data and rendering layers after the board foundation is stable.

See `docs/live-data-plan.md` for the current live-data split.
