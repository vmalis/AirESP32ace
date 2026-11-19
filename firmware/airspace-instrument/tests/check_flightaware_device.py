# AirESP32ace
# Created by VM, August 2026

"""Opt-in hardware test: invalid key -> red state -> restored real key -> green.

Requires explicit user approval: this temporarily changes the selected device's
FlightAware credential. All other configuration is preserved. The valid key is
read from the local companion store, never printed or written to test output.
"""

import argparse
import copy
import ipaddress
import json
import re
import subprocess
import time
from pathlib import Path
from urllib.request import Request, urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--expected-mac", required=True)
    parser.add_argument("--invalid-hold-seconds", type=int, default=30)
    parser.add_argument("--allow-temporary-invalid-key", action="store_true")
    args = parser.parse_args()
    if not args.allow_temporary_invalid_key:
        parser.error("Explicit --allow-temporary-invalid-key is required")
    address = ipaddress.IPv4Address(args.host)
    if not address.is_private or not 0 <= args.invalid_hold_seconds <= 60:
        parser.error("Use a private IPv4 device address and a 0..60 second hold")
    arp = subprocess.check_output(["/usr/sbin/arp", "-an"], text=True)
    found = re.search(r"\(" + re.escape(str(address)) + r"\) at ([0-9a-f:]+) ", arp, re.I)
    normalize = lambda value: ":".join(f"{int(part, 16):02x}" for part in value.split(":"))
    if found is None or normalize(found[1]) != normalize(args.expected_mac):
        raise RuntimeError("Device hardware identity does not match; nothing changed")

    store = Path.home() / "Library/Application Support/AirspaceCompanion/credentials.json"
    valid_key = json.loads(store.read_text())["flightaware.aeroapi.key"].strip()
    if not valid_key:
        raise RuntimeError("No saved valid key; nothing changed")
    base = f"http://{address}"

    def config():
        with urlopen(base + "/api/config", timeout=10) as response:
            return json.load(response)

    def set_key(key, available):
        payload = json.dumps({"credentials": {"flightAware": {
            "apiKey": key, "available": available,
        }}}).encode()
        request = Request(base + "/api/config", data=payload,
                          headers={"Content-Type": "application/json"}, method="POST")
        with urlopen(request, timeout=15) as response:
            result = json.load(response)
            if result.get("conflict") or result.get("reconnecting"):
                raise RuntimeError("Unexpected config conflict/network change")

    def wait_for(health):
        deadline = time.monotonic() + 100
        while time.monotonic() < deadline:
            current = config()
            service = current["device"]["services"]
            if service.get("flightAwareHealth") == health:
                assert service["flightAwareReady"] == (health == "valid")
                return current
            time.sleep(2)
        raise RuntimeError(f"Device did not reach {health} FlightAware state")

    def stable_settings(value):
        value = copy.deepcopy(value)
        for field in ("savedAtUnix", "reconnecting", "conflict"):
            value.pop(field, None)
        for field in ("revision", "services"):
            value["device"].pop(field, None)
        value.get("companion", {}).pop("lastSyncedDeviceRevision", None)
        return value

    before = config()
    if "flightAwareHealth" not in before["device"]["services"]:
        raise RuntimeError("Corrected firmware is not installed; nothing changed")
    print(f"{address}: identity verified; starting controlled invalid-key test", flush=True)
    try:
        # Keep the companion's availability hint true: the device must reject
        # this key using its own online probe, not merely trust that hint.
        set_key("AirESP32ace-deliberately-invalid-health-test", True)
        wait_for("invalid")
        print(f"{address}: INVALID confirmed (red-cross render state); holding "
              f"{args.invalid_hold_seconds}s for visual inspection", flush=True)
        time.sleep(args.invalid_hold_seconds)
    finally:
        # Restore even when an assertion, timeout, or Ctrl-C interrupts the test.
        restored = False
        for attempt in range(3):
            try:
                set_key(valid_key, True)
                after = wait_for("valid")
                restored = True
                break
            except Exception:
                if attempt < 2:
                    time.sleep(2)
        if not restored:
            raise RuntimeError("RESTORATION NOT VERIFIED: keep device powered and restore "
                               "the saved FlightAware key through the companion")
        print(f"{address}: VALID confirmed (green-check render state); saved key restored", flush=True)
        assert stable_settings(before) == stable_settings(after), "Other settings changed"
        print(f"{address}: all other device settings and prepared content configuration preserved", flush=True)


if __name__ == "__main__":
    main()
