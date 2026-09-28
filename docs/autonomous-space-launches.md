# Autonomous space launch updates

Space-launch runtime is owned by the ESP32, not by the companion. The Mac
prescribes enabled programs and installs existing rocket/map artwork. It reads
device snapshots for its preview; leaving the Mac closed does not stop updates.

## Why Flight 14 was missed

The previous firmware displayed the last launch snapshot manually pushed from
the companion. It never polled Launch Library 2 itself. That left an old,
quarter-precision Starship Flight 14 prediction on the device after the provider
had published an exact launch time and live status.

The replacement polls Launch Library 2 from the existing background data task.
Discovery includes the previous 24 hours as well as future missions, so a launch
is not lost merely because it leaves the upcoming list. Once selected, a mission
is refreshed by its exact UUID. NASA Artemis, Roscosmos Soyuz MS, and SpaceX
Starship remain the supported programs. Existing artwork, maps, and navigation
are preserved. The QR page omits the mission heading that could be cropped;
the existing "SCAN TO WATCH" instruction remains below the code.

## Freshness, lifecycle, and limits

- Refresh every six hours normally, every 30 minutes inside 24 hours of NET,
  and every five minutes inside six hours of NET or while in flight.
- A shared, persisted budget limits all three programs to 12 attempts per hour,
  with at least 60 seconds between requests. Rebooting or pressing Sync does not
  reset it. Failures back off from one minute through five, 15, 30, and 60 minutes
  to six hours; HTTP 429 respects Retry-After (one hour by default).
- Follow the same UUID through launch. Payload Deployed is not treated as final
  Starship completion. Success, failure, and partial failure are retained for
  two hours after first observation, then discovery advances to the next mission.
- Never keep a mission more than two days past its NET. Reject malformed data,
  mismatched identities, and older provider revisions.
- Persist the last good records and request budget with checksums and backup
  recovery. A migrated companion snapshot starts stale until checked online.
- After two refresh intervals without a successful check, show STALE and disable
  the precise countdown. An unavailable schedule never becomes a fake preview.
- HTTP and JSON parsing stay off the UI task. Responses are bounded to 96 KiB in
  PSRAM, and TLS allocations are released before parsing. No new full-status copy
  is introduced into the LVGL map-loading path.
- Space-screen entry releases inactive file artwork before the world-map
  allocation. The unused aligned Radar/Tracker/weather render copy is reclaimed
  after its old LVGL tree is deleted, and rebuilt on return. This leaves room for
  ISS artwork even while the weather worker replaces its radar cache.

## Synchronization

`GET /api/launches` and USB `ASTLAUNCH:GET` expose identical device snapshots;
USB capability `LAUNCH1` advertises support. Companion state synchronization sends
only enabled `launchAgencies`, not launch runtime. Device publication is protected
from subsequent legacy snapshot writes. USB launch-artwork transfers retain all
four existing tracked-flight asset slots.

USB JSON replies are sent in 256-byte chunks to fit the driver's 512-byte
all-or-nothing transmit ring. Console output is held out of each framed reply.
This fixes timeouts on launch snapshots and larger configuration responses.

Update both firmware and companion to v0.1.2. Existing enabled programs migrate
from the installed launch snapshot; use Sync once if launch artwork has not yet
been installed. No API key is needed for Launch Library 2. Wi-Fi and synchronized
device time are required for updates.

## Verification

- Sanitizer-enabled launch policy tests cover the actual Flight 14 shape, UTC and
  daylight-saving offsets, identity, old revisions, stale/expiry behavior,
  completion retention, preferences, and reboot-stable request budgeting.
- The production parser was also run against the complete live normal-mode
  Flight 14 discovery and exact-UUID responses on September 28, 2026, including
  Launch Successful.
- Remote-state ownership tests, tracker policy tests, route-direction tests, and
  aircraft-registry tests pass. Optimized ESP32 stack checks keep both the status
  merge and map-buffer allocator at 48 bytes, below the 256-byte regression limit.
- The standalone Swift launch-snapshot smoke check exercises the production
  models without XCTest. The full XCTest suite requires an accepted Xcode
  installation; Command Line Tools alone do not include XCTest.
- The developer build was flashed application-only to the physical board.
  With the companion closed, the device fetched all three launch programs
  successfully over Wi-Fi. The user confirmed Flight 14 showed Launch Successful.
  Fresh records and the shared request budget survived the subsequent update.
- The companion's production USB and Wi-Fi clients returned matching launch
  identities and enabled programs. A full USB configuration reply decoded
  successfully. Existing flight slots and launch artwork were retained.
- Two consecutive hardware cycles through Radar, Tracker, Weather radar,
  Clock, Solar, Moon, ISS, and all three launch views passed after the artwork
  memory correction. No space-asset load failures or reboot/panic were observed;
  route and launch artwork remained installed, and the original style was restored.
- Connected and disconnected firmware-panel previews keep Back and Install
  visible in a fixed footer; disconnected Install stays disabled. The device
  illustration has no extra center icon. The actual QR renderer was checked
  with the long Flight 14 mission name.
- The public firmware is separately rebuilt from sanitized public source with
  blank credentials and fallback fonts. Hardware acceptance used the developer
  artwork variant of the same code. Extended offline/reconnect soak testing and
  the complete XCTest suite remain follow-up checks, not claimed acceptance.

Run the host checks with:

```sh
sh firmware/airspace-instrument/tests/run_launch_policy_test.sh
sh firmware/airspace-instrument/tests/run_remote_state_test.sh
sh firmware/airspace-instrument/tests/run_tracker_policy_test.sh
sh companion/AirspaceCompanion/scripts/check_launch_snapshot.sh
```

An activated external ESP-IDF installation can be selected with `IDF_PATH` for
the firmware build and launch policy test. Otherwise the developer checkout's
`tools/esp-idf` installation is used.

The firmware dependency lock is included to keep the public build on the same
display-driver/LVGL dependency versions as the verified developer build. A clean
checkout must not silently upgrade its display stack during this maintenance
release.
