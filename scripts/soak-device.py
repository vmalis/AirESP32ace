#!/usr/bin/env python3
# AirESP32ace
# Created by VM, November 2026
"""Read-only USB soak for a board running the candidate. Close the companion
and enable Dock Mode on the instrument first. No settings, secrets or Wi-Fi
commands are sent. Only whitelisted health metrics reach the output log."""
import argparse
from collections import defaultdict
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
from statistics import median
import termios
import time

def report(**items):
    print(json.dumps(items, separators=(",", ":")), flush=True)

class PassiveHealthPort:
    """One raw POSIX handle throughout the run. Never set DTR/RTS, send a
    break, or flush the receive queue. The only outbound message is HEALTH."""
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
        self.buffer = bytearray()
        self.skip_overlong = False
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            attrs = termios.tcgetattr(self.fd)
            attrs[:6] = [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL,
                         0, termios.B115200, termios.B115200]
            attrs[6][termios.VMIN], attrs[6][termios.VTIME] = 1, 0
            # TCSANOW: raw, no echo/flow-control and HUPCL off. Do not restore
            # HUPCL on close: that would risk resetting the physical board.
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def _wait(self, until, writing=False):
        left = until - time.monotonic()
        if left > 0:
            rd, wr, _ = select.select([] if writing else [self.fd], [self.fd] if writing else [], [], left)
            if rd or wr: return
        raise TimeoutError("timed out awaiting USB health; no coverage for this interval")

    def _line(self, until):
        while time.monotonic() < until:
            pos = self.buffer.find(b"\n")
            if pos >= 0:
                line = self.buffer[:pos]
                del self.buffer[:pos+1]
                if self.skip_overlong or pos > 4096:
                    self.skip_overlong = False
                    continue
                return line
            if len(self.buffer) > 4096:
                self.skip_overlong = True
                self.buffer.clear()
            self._wait(until)
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not chunk: raise ConnectionError("USB health port closed")
            self.buffer.extend(chunk)
        raise TimeoutError("timed out awaiting complete health reply")

    def health(self, timeout=7):
        until = time.monotonic() + timeout
        command = memoryview(b"ASTCFG:HEALTH\n")
        while command:
            self._wait(until, writing=True)
            try:
                n = os.write(self.fd, command)
            except BlockingIOError:
                continue
            if not n: raise ConnectionError("USB health port closed")
            command = command[n:]
        while True:
            line = self._line(until)
            if not line.startswith(b"ASTCFG:OK:"): continue
            try:
                packet = json.loads(line[len(b"ASTCFG:OK:"):])
            except (ValueError, UnicodeDecodeError):
                continue
            if isinstance(packet, dict) and {"bootId","elfSHA256","uiHeartbeat"} <= packet.keys():
                return packet

class OutageWitness:
    """Conservative lower bound: count only time bracketed by offline samples."""
    def __init__(self, minimum_seconds):
        if minimum_seconds <= 0:
            raise ValueError("minimum outage must be positive")
        self.minimum = minimum_seconds
        self.saw_online = False
        self.first_offline = self.last_offline = None
        self.newer_than = None
        self.prior_fetch = 0
        self.pending_seconds = 0
        self.longest_recovered = 0

    def observe(self, now, online, fetch):
        # A reset of the device clock/cached timestamp is not a fresh sample.
        self.prior_fetch = max(self.prior_fetch, fetch)
        if not online:
            if self.first_offline is None:
                self.first_offline = now
                self.newer_than = None  # abandon any unverified prior recovery
                self.pending_seconds = 0
            self.last_offline = now
        else:
            if self.first_offline is not None:
                if self.saw_online:  # booting offline is not a witnessed disconnect
                    self.pending_seconds = self.last_offline - self.first_offline
                # Require progress AFTER seeing connectivity restored, even if
                # the first connected sample already contains a new timestamp.
                self.newer_than = self.prior_fetch
                self.first_offline = self.last_offline = None
            elif self.newer_than is not None and fetch > self.newer_than:
                self.longest_recovered = max(self.longest_recovered, self.pending_seconds)
                self.pending_seconds = 0
                self.newer_than = None
            self.saw_online = True

    @property
    def pending(self):
        return self.first_offline is not None or self.newer_than is not None

    @property
    def recovered(self):
        return self.longest_recovered >= self.minimum

def run():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--board-id", required=True)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--hours", type=float, default=48)
    p.add_argument("--interval", type=int, default=13)
    p.add_argument("--require-outage", action="store_true",
                   help="require a witnessed disconnect and automatic recovery with new aircraft data")
    p.add_argument("--min-outage-seconds", type=int, default=120,
                   help="minimum span between consecutive offline samples (default 120 s)")
    p.add_argument("--pages", default="tracker,weatherRadar,iss,launch")
    a = p.parse_args()
    if not 0 < a.hours <= 168 or not 2 <= a.interval <= 60:
        p.error("hours must be positive and <= 168; interval 2–60 seconds")
    if a.min_outage_seconds <= 0: p.error("--min-outage-seconds must be positive")
    expected = hashlib.sha256(a.elf.read_bytes()).hexdigest()
    pages = set(a.pages.split(","))
    start = time.monotonic()
    first = prev = None
    seen = set()
    before, after = defaultdict(list), defaultdict(list)
    outage = OutageWitness(a.min_outage_seconds)
    last_good = last_sample = start
    link = PassiveHealthPort(a.port)
    try:
        while True:
            s = link.health()
            now = time.monotonic()
            if now - last_sample > max(30, a.interval * 3):
                raise RuntimeError("monitoring gap (host asleep or disconnected)")
            last_sample = now
            if s["deviceId"] != a.board_id.replace(":", "").upper() or s["elfSHA256"] != expected:
                raise RuntimeError("unexpected board or firmware")
            if first is None: first = s
            if s["bootId"] != first["bootId"] or s["spaceAssetLoadFailures"] != 0:
                raise RuntimeError("unexpected reset or space artwork failure")
            if prev and (s["uiHeartbeat"] == prev["uiHeartbeat"] or s["uptimeSeconds"] < prev["uptimeSeconds"]):
                raise RuntimeError("no UI progress or uptime regressed")
            if first["trackedFlightValid"] and s["trackedFlightValid"]:
                for field in ("routeMapMask", "routeFlagMask"):
                    if first[field] == 3 and s[field] != 3:
                        raise RuntimeError("tracked route artwork no longer available")
            fetch = s["aircraftFetchedUnix"]
            outage.observe(now, s["wifiState"] == "connected", fetch)
            if s["wifiState"] != "connected":
                last_good = now  # allow a full poll/backoff window after reconnect
            elif prev and fetch > prev["aircraftFetchedUnix"]:
                last_good = now
            if now - last_good > 1800 and s["wifiState"] == "connected":
                raise RuntimeError("no successful aircraft refresh in 30 min while online")
            elapsed = now - start
            page = s["renderedPage"]
            seen.add(page)
            if elapsed <= a.hours * 3600 * 0.25: before[page].append(s["psramLargestKB"])
            if elapsed >= a.hours * 3600 * 0.75: after[page].append(s["psramLargestKB"])
            report(elapsedSeconds=round(elapsed), **{k: s[k] for k in (
                "renderedPage","uiHeartbeat","uptimeSeconds","wifiState","psramFreeKB",
                "psramLargestKB","internalFreeKB","internalLargestKB","aircraftFetchedUnix",
                "weatherFetchedUnix","aircraftHttpStatus","spaceAssetLoadFailures")})
            prev = s
            if elapsed >= a.hours * 3600: break
            time.sleep(a.interval)
    finally:
        link.close()
    if pages - seen: raise RuntimeError("not all requested Dock pages were observed")
    if outage.pending or (a.require_outage and not outage.recovered):
        raise RuntimeError("Wi-Fi recovery with fresh data was not demonstrated")
    for page in pages:
        if not before[page] or not after[page]:
            raise RuntimeError("insufficient same-page samples: " + page)
        if median(before[page]) - median(after[page]) > 256:
            raise RuntimeError("largest free block shrank by >256 KiB on " + page)
    report(result="PASS", hours=a.hours, recovered=outage.recovered,
           longestVerifiedOfflineSeconds=int(outage.longest_recovered), pages=sorted(seen))

if __name__ == "__main__":
    run()
