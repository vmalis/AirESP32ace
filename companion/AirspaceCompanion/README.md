# AirESP32ace

Native macOS SwiftUI companion for the ESP32-S3 AMOLED airspace instrument.

Current first cut:

- Tracks the configured family flight preset: Air Premia `YP111` / `APZ111`, ICN → SFO.
- Can optionally pull live `APZ111` status from FlightAware AeroAPI when a key is provided.
- Renders a 466×466 route-frame map on the Mac, with ICN pinned at 6 o'clock and SFO pinned at 12 o'clock.
- Bakes an airplane glyph onto the route at the selected/live progress position.
- Saves both:
  - `~/Pictures/AirspaceInstrument/YP111_route_map_466.png`
  - `~/Pictures/AirspaceInstrument/YP111_route_map_466.rgb565` exactly `434,312` bytes (`466 × 466 × 2`)
- Shows three visual directions for the device UI:
  - XDR Radar
  - Cartographic Glass
  - GMT Chronograph
- Pushes the selected design to the ESP32 over Wi-Fi:
  - `XDR Radar` switches the device to local radar.
  - `Cartographic Glass` uploads the rendered RGB565 map and switches the device to the YP111 map screen.
  - `GMT Chronograph` switches the device to the GMT clock.

Run:

```sh
cd companion/AirspaceCompanion
swift run AirESP32ace
```

Notes:

- Switching the design in the Mac preview does not update the device by itself. Use `Push to Device`.
- Without a key, the flight provider uses the static YP111 route preset.
- With a FlightAware AeroAPI key, enter it in the Mac UI or launch with `FLIGHTAWARE_AEROAPI_KEY`.
- The live provider uses `APZ111` because FlightAware recommends ICAO airline idents for airline flight-number lookups.
- The `Plane position` slider is manual unless a live refresh supplies a progress/position update.
- Map/weather/route beauty belongs on the Mac. The ESP32 should mostly display compact bitmap assets plus lightweight LVGL overlays.

Live CLI render:

```sh
cd companion/AirspaceCompanion
FLIGHTAWARE_AEROAPI_KEY=... swift run AirESP32ace --render-yp111-map --live
```
