#!/usr/bin/env python3
# AirESP32ace
# Created by VM, November 2026
"""Pixel regression for full and fallback Weather artwork in the real renderer."""
import argparse
import ctypes
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


# Inclusive nonblack bounds on the black Weather background, then SHA-256 of
# the tightly cropped RGB565 composite. Goldens from the v0.1.4 renderer.
# Fallback raster/alpha silhouette stays identical; only its origin moves +36,+36.
EXPECTED = {
    ("SJC", "full"): ((60, 78, 161, 179), "beedd53ffa036a5c810e0c09ad47fc9165d6e42089c5c6faa2a7b33536833ad9"),
    ("SJC", "fallback"): ((60, 78, 161, 178), "68aeefb8d67c95922863427cebc073e2b8ec6f572fc8d28d94489a51971d0b7e"),
    ("DUS", "full"): ((61, 79, 160, 179), "ac44e6db22842c2f1dbde07a09543ecfa3ea88a9ceb5df0e905310f7dfd85661"),
    ("DUS", "fallback"): ((63, 79, 158, 178), "fdb6a929275c8219cd40201b2454d3b60b2ab83eb54b10cb7f478d85e0bd28d4"),
}


def check(library, assets, city, mode):
    lib = ctypes.CDLL(str(library.resolve()))
    lib.airspace_renderer_initialize.argtypes = [ctypes.c_char_p]
    lib.airspace_renderer_initialize.restype = ctypes.c_bool
    lib.airspace_renderer_set_location.argtypes = [ctypes.c_char_p]
    lib.airspace_renderer_set_page.argtypes = [ctypes.c_int]
    lib.airspace_renderer_pixels.restype = ctypes.c_void_p
    if not lib.airspace_renderer_initialize(bytes(assets.resolve())):
        raise RuntimeError("could not initialize renderer")
    lib.airspace_renderer_set_location(city.encode())
    lib.airspace_renderer_set_page(9)  # AIRSPACE_PREVIEW_WEATHER_CURRENT
    lib.airspace_renderer_render()
    stride = lib.airspace_renderer_stride_bytes()
    frame = ctypes.string_at(lib.airspace_renderer_pixels(), stride * 466)
    # This ROI contains the emblem (even at its old displaced location) and no text.
    ink = [(x, y) for y in range(38, 185) for x in range(18, 173)
           if frame[y * stride + 2*x : y * stride + 2*x + 2] != bytes(2)]
    if not ink:
        raise AssertionError(f"{city} {mode}: missing emblem")
    xs, ys = zip(*ink)
    bounds = min(xs), min(ys), max(xs), max(ys)
    x0, y0, x1, y1 = bounds
    crop = b"".join(frame[y*stride + 2*x0 : y*stride + 2*(x1+1)] for y in range(y0, y1+1))
    actual = bounds, hashlib.sha256(crop).hexdigest()
    if actual != EXPECTED[city, mode]:
        raise AssertionError(f"{city} {mode}: expected {EXPECTED[city, mode]}, got {actual}")
    print(f"PASS {city} {mode}: {x1-x0+1}×{y1-y0+1} at ({x0},{y0}), original silhouette and color")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("--case", nargs=2, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.case:
        check(args.library, args.assets, *args.case)
        return
    with tempfile.TemporaryDirectory() as empty_assets:
        for city, mode in EXPECTED:
            # A separate renderer process per capture avoids retained page/city state.
            root = args.assets if mode == "full" else empty_assets
            subprocess.run([sys.executable, __file__, args.library, root, "--case", city, mode], check=True)


if __name__ == "__main__":
    main()
