# Autonomous flight tracking

The Mac prescribes up to four non-overlapping flight occurrences and prepares maps and flags. The device owns position, progress, telemetry, schedule/status refreshes, completion, and automatic handoff. Closing the Mac app does not interrupt tracking.

Install the matching app and firmware, then **Sync** existing flights once. USB-C and Wi-Fi deliver the same complete queue. Queue replacement follows asset transfer and is checksummed and persistent, with a recoverable previous snapshot. Re-syncing the same occurrence retains device runtime progress while accepting repaired city labels and map geometry. Late provider replies cannot overwrite a replaced queue.

Credentials are provisioned separately into the device's configuration storage. They are not part of tracking snapshots or release downloads. The companion owns queue edits; the device owns tracked-flight runtime updates.

## Airport city names

Every Tracker view and both sync transports use the prescribed endpoint cities, rather than the separate Nearby Radar airport cache. Missing cities in old or newly prepared flights use the Mac's bundled public-domain [OurAirports reference](https://ourairports.com/data/), indexed by IATA and ICAO code with country and coordinate guards. Valid provider names take precedence; unknown or conflicting identities are not guessed.

The database is Mac-only, needs no extra provider requests, and does not change the eight-city Radar/Clock limit. Resolved names persist with the device's flight queue.

## Countdown and updates

- More than one hour before estimated departure (scheduled time as fallback), Tracker is countdown-only. Mac page controls, direct native preview requests, and Dock Details respect this gate. A delay can return the display to countdown-only.
- Confirmed delays update ETD and show an amber `DELAYED - DEPARTS IN` heading. A matched on-time update clears it.
- Scheduled flights cannot inherit live aircraft motion from another occurrence using the same callsign or airframe. Cached preflight motion is cleared; active flights retain the last valid telemetry fields.
- FlightAware is the primary schedule/gate source. The device checks its next flight every six hours beyond 24 hours, hourly within 24 hours, and every ten minutes within two hours. In-flight metadata cadence is 30 minutes for short flights and one hour for longer flights, becoming five minutes near/after ETA.
- OpenSky supplies airborne positions and telemetry. Motion refreshes are considered every minute, subject to a shared per-board credit budget. Radar normally refreshes every 90 seconds and reserves credits while Tracker is requesting observations. HTTP 429 defers state requests. Flight identity and sample age are checked separately from radio contact age. FlightAware position fallback is limited to once per five minutes, including failed attempts; recovery of OpenSky does not reset that cooldown.
- AirLabs is a schedule/enrichment fallback and can confirm completion after ETA. Route and departure-window matching are required; empty responses do not clear a valid flight.
- During gaps, progress may be estimated from the latest position and ETA. Estimates are labeled, can be corrected by fresh coordinates, and cannot declare touchdown or reach 100%.
- Arrived flights remain available for two hours unless the next prepared flight is due. Active flights retain priority. Days-old unconfirmed occurrences expire at the existing ETA-plus-24-hours cutoff.

Network work runs in the background data worker, never in rendering. Existing maps, aircraft artwork, route geometry, and shared digital-twin rendering are preserved. The Mac reads `/api/tracker` or `ASTTRACK:GET`; it does not automatically poll providers and push runtime snapshots. Manual discovery/refresh is available when preparing flights.

## USB firmware updates

Firmware updates require USB-C; Wi-Fi supports normal autonomous operation and flight synchronization. **Update Firmware** preserves settings and content. **Set Up New Device** erases the board. With v0.1.5, pair the intended instrument over USB using **Pair This Mac** before using pinned HTTPS for wireless control. Radios and providers on the instrument work independently of the paired Mac.

Configuration, telemetry, asset transfers, and the installer share an exclusive USB lease. Flashing waits for active requests, owns the port through completion, and releases it on failure. Background polls cannot inject commands into esptool. The updater uses the supported ESP32-S3 watchdog reset so the application boots after writing.

## Verification

The project includes XCTest coverage for city resolution, occurrence matching, countdown gates, rendering, provider handling and USB ownership; host Tracker/storage policy tests; and checks on the compiled ESP32 display stack frames. See the [v0.1.5 release notes](../release/v0.1.5-notes.txt) for measured hardware results and outstanding tests.
