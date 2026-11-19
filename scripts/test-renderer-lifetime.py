#!/usr/bin/env python3
# AirESP32ace
# Created by VM, November 2026
"""Exercise the actual shared renderer, without starting the Mac app or a board."""
import argparse
import ctypes
import hashlib
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("--passes", type=int, default=4)
    args = parser.parse_args()
    if not args.assets.is_dir() or not 1 <= args.passes <= 100:
        parser.error("asset directory and 1–100 passes required")
    lib = ctypes.CDLL(str(args.library.resolve()))
    lib.airspace_renderer_initialize.argtypes = [ctypes.c_char_p]
    lib.airspace_renderer_initialize.restype = ctypes.c_bool
    lib.airspace_renderer_set_page.argtypes = [ctypes.c_int]
    lib.airspace_renderer_pixels.restype = ctypes.c_void_p
    if not lib.airspace_renderer_initialize(bytes(args.assets.resolve())):
        raise RuntimeError("renderer initialization failed")
    height, width = lib.airspace_renderer_height(), lib.airspace_renderer_width()
    stride = lib.airspace_renderer_stride_bytes()
    if (width, height) != (466, 466) or stride < width * 2:
        raise RuntimeError("invalid framebuffer dimensions")
    # Values are the stable public preview-page ABI from desktop_renderer.h.
    pages = [0, 6, 9, 11, 15, 16, 12, 14, 23, 14, 13, 17, 18, 19, 7, 8]
    for _ in range(args.passes):
        previous = None
        for page in pages:
            lib.airspace_renderer_set_page(page)
            lib.airspace_renderer_render()
            ptr = lib.airspace_renderer_pixels()
            if not ptr:
                raise RuntimeError(f"page {page}: missing framebuffer")
            rgb = ctypes.string_at(ptr, stride * height)
            digest = hashlib.sha256(rgb).digest()
            if len(set(rgb)) < 8 or (page not in (6, 7, 8) and digest == previous):
                raise RuntimeError(f"page {page}: blank or unchanged framebuffer")
            previous = digest
    print(f"PASS: {len(pages) * args.passes} native-renderer page renders")

if __name__ == "__main__":
    main()
