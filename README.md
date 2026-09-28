<div align="center">

# AirESP32ace

### A tiny, cinematic aerospace instrument for your desk

ESP32-S3 firmware and a native macOS companion for a 466 × 466 circular AMOLED display.

[![License: MIT](https://img.shields.io/badge/license-MIT-34d058.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.5%2B-34d058.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
[![macOS](https://img.shields.io/badge/macOS-15%2B-34d058.svg)](companion/AirspaceCompanion)
[![Hardware](https://img.shields.io/badge/display-466%C3%97466_AMOLED-34d058.svg)](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C)

</div>

<div align="center">
  <img src="docs/images/screen-map.png" alt="Complete AirESP32ace screen map" width="100%" />
  <br />
  <sub>The complete interaction map: six main worlds, their alternate views, settings, power controls, and Dock Mode.</sub>
</div>

<br />

<table>
  <tr>
    <td><img src="docs/images/radar.png" alt="Local aircraft radar" /></td>
    <td><img src="docs/images/tracker.png" alt="Tracked flight" /></td>
    <td><img src="docs/images/weather.png" alt="Weather" /></td>
  </tr>
  <tr>
    <td align="center"><b>Local radar</b></td>
    <td align="center"><b>Flight tracker</b></td>
    <td align="center"><b>Weather</b></td>
  </tr>
  <tr>
    <td><img src="docs/images/iss.png" alt="ISS tracker" /></td>
    <td><img src="docs/images/launch.png" alt="Space launch tracker" /></td>
    <td valign="middle"><b>One circular instrument.</b><br/>Aircraft, weather, time, the solar system, ISS passes, and crewed launch schedules.</td>
  </tr>
</table>

## What it does

- Renders a smooth local aircraft radar with real icons, predicted motion, selectable range, callsigns, and aircraft detail views.
- Tracks planned flights on both a geographic 9-to-3 route and a custom 6-to-12 route projection.
- Shows current weather, a five-day forecast, and animated radar imagery for the selected city.
- Includes a GMT-style world clock, sunrise/sunset bezel, and configurable city list.
- Computes a live solar-system view and lunar phase, animates the next total solar eclipse, and tracks the ISS over a generated world map.
- Follows selected NASA, Roscosmos, and SpaceX/Starship launch schedules directly on the device, even with the Mac closed.
- Provides touch, two-button navigation, sleep/power controls, brightness adjustment, and an automatic Dock Mode.
- Uses a native SwiftUI companion to configure the device, manage cities and flights, render maps, validate API credentials, preview every device screen, and install firmware.

The firmware is optimized around the limits of an ESP32-S3: persistent RGB565 assets live in flash, working buffers use PSRAM deliberately, and small overlays are updated independently so animations do not require full-screen redraws.

## Hardware

| Part | Link | Notes |
|---|---|---|
| Display/controller | [Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C) | ESP32-S3R8, 8 MB PSRAM, 32 MB flash, 466 × 466 CO5300 AMOLED, CST9217 touch, AXP2101 PMIC. Approximately US$40 from AliExpress when this project was built; availability and pricing vary. |
| Desk dock | [Magnetic phone/watch stand](https://a.co/d/012B5Kl2) | The compact dock shown with the project. Any stable stand with USB-C clearance should work. |
| USB-C cable | Any data-capable cable | Required for first setup and wired firmware recovery. |
| Mac | Apple silicon recommended | Builds and runs the native companion app. |

## Repository layout

```text
firmware/airspace-instrument/   ESP-IDF firmware
companion/AirspaceCompanion/   Native SwiftUI macOS app
shared/native_renderer/        Device renderer hosted by the Mac preview
assets/                        Project-created static artwork and registries
Cities/                        Default city artwork
release/firmware/              Ready-to-flash ESP32-S3 binaries and offsets
release/macos/                 Ready-to-run macOS app archive and checksum
docs/guides/                   Device and companion PDF guides
scripts/                       Build and asset-generation helpers
```

## Quick start

### 1. Flash the ready-made firmware

The [firmware release folder](release/firmware) contains the app, bootloader, and partition-table binaries for the Waveshare ESP32-S3-Touch-AMOLED-1.75C. It contains no Wi-Fi or API credentials. Follow its README for the exact offsets, then complete Wi-Fi, provider, city, and map setup in the companion.

### 2. Run the ready-made Mac companion

Download [AirESP32ace-macOS.zip](release/macos/AirESP32ace-macOS.zip), unzip it, then Control-click **AirESP32ace.app** and choose **Open**. This build is ad-hoc signed but not notarized with Apple; macOS may require one confirmation in **System Settings → Privacy & Security**. No Apple Developer account is required.

The archive is built from the source in this repository and includes the native device renderer and firmware installer resources. Its SHA-256 checksum is published in [release/macos/SHA256SUMS](release/macos/SHA256SUMS).

Version **0.1.2** moves both launch and tracked-flight runtime updates onto the ESP32. Install the matching firmware and companion together. The Mac configures the tracking list, enabled launch programs, credentials, and artwork; the device refreshes data over Wi-Fi. See [autonomous space launches](docs/autonomous-space-launches.md) for freshness rules, provider limits, and verification details.

### 3. Run the Mac companion from source

Requirements: macOS 15 or newer, Xcode with Command Line Tools, and Git.

```bash
git clone https://github.com/vmalis/AirESP32ace.git
cd AirESP32ace/companion/AirspaceCompanion
swift run AirESP32ace
```

That launches the companion directly from Swift Package Manager. Configuration and API credentials are stored locally; they are never part of this repository.

For a self-contained development `.app` with the exact native device renderer:

```bash
cd AirESP32ace
./companion/AirspaceCompanion/scripts/build_app_bundle.sh debug
open "companion/AirspaceCompanion/.build/arm64-apple-macosx/debug/AirESP32ace.app"
```

The full bundle expects the firmware dependencies to have been resolved once, as described below. Local builds use ad-hoc signing, so no Apple Developer certificate is required.

### 4. Build the firmware

Install [ESP-IDF 5.5 or newer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) and activate its environment, then:

```bash
cd AirESP32ace
./scripts/idf.sh firmware/airspace-instrument set-target esp32s3
./scripts/idf.sh firmware/airspace-instrument menuconfig
./scripts/idf.sh firmware/airspace-instrument build
```

In `menuconfig`, open **AirESP32ace** and enter the initial Wi-Fi/location values. The generated `sdkconfig` contains local credentials and is ignored by Git.

### 5. Flash and connect

Find the board's serial port:

```bash
./scripts/list-ports.sh
```

Then flash and monitor it (replace the port with yours):

```bash
./scripts/idf.sh firmware/airspace-instrument \
  -p /dev/cu.usbmodemXXXX flash monitor
```

Once the device joins Wi-Fi, open the companion, enter its IP address, add any optional API credentials, choose **Sync**, and let the Mac generate the map packs. The in-app **Device** panel can also provision a new board or install a later firmware build over USB-C.

## Data services

The project is designed to remain useful when one provider is unavailable.

- **OpenSky Network** supplies nearby-aircraft state vectors for Radar and live airborne telemetry for a selected tracked flight.
- **FlightAware AeroAPI** is the primary tracked-flight source for the exact selected occurrence, route, schedule, terminal/gates, ETD, and ETA when configured.
- **AirLabs** is the tracked-flight fallback when FlightAware is unavailable or does not return the selected occurrence.
- Public weather and launch-schedule endpoints provide their respective screens.

Tracked-flight schedule and gate information is refreshed conservatively: no automatic provider polling more than 24 hours before departure, hourly inside the 24-hour window, and every 10 minutes inside two hours. While airborne, the one-minute loop obtains live position, altitude, speed, and heading from OpenSky; the exact selected FlightAware occurrence is cached for 10 minutes, so AeroAPI is never polled once per minute. FlightAware never serves nearby Radar traffic, and AirLabs is used only when FlightAware fails or lacks required enrichment. Because the FlightAware credential stays in the macOS Keychain rather than on the ESP32, the companion app must be running for those periodic refreshes to reach the device.

Accounts, availability, quotas, and permitted uses are controlled by each provider. Bring your own credentials and review their terms before use. The API status page performs a lightweight authorization check; having a field filled in is not treated as proof that the credential is active.

## Maps and artwork

The original project-created artwork is included, including the exact startup image used on the device, aircraft source sheets, city art, weather art, and launch assets. Pre-rendered map packs and downloaded airline logos are intentionally **not** committed: the companion generates maps for your selected cities/routes and caches airline artwork locally at runtime. The proprietary watch mark is also omitted.

See [Asset and data policy](docs/ASSET_POLICY.md) for the exact boundary.

## Guides

The project maintains one standard edition of each guide.

- [Getting started (PDF)](docs/guides/AirESP32ace_Getting_Started.pdf)
- [Device interaction guide (PDF)](docs/guides/AirESP32ace_Device_User_Guide.pdf)
- [Complete screen map (PDF)](docs/guides/AirESP32ace_Screen_Map.pdf)
- [Mac companion guide (PDF)](docs/guides/AirESP32ace_Companion_User_Guide.pdf)
- [Build and troubleshooting notes](docs/BUILDING.md)

## Project story

AirESP32ace was created over four evenings as an AI-assisted, “vibe coding” project—but the direction was deliberate: screen composition, interaction design, map projections, buffering strategy, PSRAM use, partial redraws, and hardware behavior were all guided and tested on the real device. The goal was not a generic demo; it was a polished little instrument that belongs beside a Studio Display.

## Contributing

Issues and focused pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) and do not commit API keys, generated map caches, downloaded airline marks, the proprietary watch mark, build outputs, or proprietary fonts.

## Support

The project is free and open source. If you publish a derivative, a link back is appreciated. If it was useful or simply entertaining, you can optionally [buy me a coffee](https://ko-fi.com/vm216261). No donation is required to use any part of the project; see [SUPPORT.md](SUPPORT.md).

## License and notices

Source code and project-authored assets are released under the [MIT License](LICENSE). Third-party data and runtime-downloaded artwork remain subject to their respective owners' terms. AirESP32ace is an independent personal project and is not affiliated with Waveshare, Apple, OpenSky, FlightAware, AirLabs, NASA, Roscosmos, SpaceX, or any airline.

Designed and directed by **Vadim Malis**.
