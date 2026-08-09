# AirESP32ace
# Created by VM, August 2026

"""Generate a 466 px OLED-ready ISS partner flag wedge."""

from __future__ import annotations

import math
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SCREEN_SIZE = 466
SCALE = 4
CANVAS_SIZE = SCREEN_SIZE * SCALE
CENTER = (CANVAS_SIZE / 2, CANVAS_SIZE / 2)
OUTER_RADIUS = 229 * SCALE
INNER_RADIUS = 208 * SCALE
START_DEGREES = 30
END_DEGREES = 150
FLAGS_DIR = Path("assets/flags")
OUTPUT_DIR = Path("assets/space")
# The visible wedge occupies only this region of the full-screen preview.
# Keeping the runtime asset cropped avoids blending more than 180,000 fully
# transparent pixels on every opacity-animation frame.
RUNTIME_CROP_BOX = (32, 334, 434, 465)

# Visual order from left to right around the bottom arc. US and Russia occupy
# the center, with the remaining partner flags pushed to either side.
FLAG_ORDER = [
    "gb",
    "ca",
    "jp",
    "be",
    "dk",
    "fr",
    "de",
    "us",
    "ru",
    "it",
    "nl",
    "es",
    "no",
    "ch",
    "se",
]


def load_flag(code: str) -> Image.Image:
    path = FLAGS_DIR / f"{code}.png"
    if not path.exists():
        raise FileNotFoundError(f"Missing flag asset: {path}")
    return Image.open(path).convert("RGBA")


def sample_bilinear(image: Image.Image, x: float, y: float) -> tuple[int, int, int, int]:
    width, height = image.size
    x = max(0.0, min(width - 1.0, x))
    y = max(0.0, min(height - 1.0, y))
    x0 = int(math.floor(x))
    y0 = int(math.floor(y))
    x1 = min(width - 1, x0 + 1)
    y1 = min(height - 1, y0 + 1)
    fx = x - x0
    fy = y - y0
    p00 = image.getpixel((x0, y0))
    p10 = image.getpixel((x1, y0))
    p01 = image.getpixel((x0, y1))
    p11 = image.getpixel((x1, y1))
    out = []
    for index in range(4):
        top = p00[index] * (1 - fx) + p10[index] * fx
        bottom = p01[index] * (1 - fx) + p11[index] * fx
        out.append(round(top * (1 - fy) + bottom * fy))
    return tuple(out)


def put_rgb565a8(image: Image.Image, path: Path) -> None:
    rgba = image.convert("RGBA").tobytes()
    pixels = image.width * image.height
    output = bytearray(pixels * 3)
    for pixel in range(pixels):
        red = rgba[pixel * 4]
        green = rgba[pixel * 4 + 1]
        blue = rgba[pixel * 4 + 2]
        alpha = rgba[pixel * 4 + 3]
        rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output[pixel * 2] = rgb565 & 0xFF
        output[pixel * 2 + 1] = rgb565 >> 8
        output[pixels * 2 + pixel] = alpha
    path.write_bytes(output)


def render() -> Image.Image:
    flags = [load_flag(code) for code in FLAG_ORDER]

    image = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (0, 0, 0, 0))
    pixels = image.load()

    start = math.radians(START_DEGREES)
    end = math.radians(END_DEGREES)
    span = end - start
    flag_count = len(flags)
    separator_width = math.radians(1.1)

    for y in range(CANVAS_SIZE):
        dy = y - CENTER[1]
        for x in range(CANVAS_SIZE):
            dx = x - CENTER[0]
            radius = math.hypot(dx, dy)
            if radius < INNER_RADIUS or radius > OUTER_RADIUS:
                continue

            theta = math.atan2(dy, dx)
            if theta < 0:
                theta += math.tau
            if theta < start or theta > end:
                continue

            normalized_angle = (theta - start) / span
            visual_left_to_right = 1.0 - normalized_angle
            flag_position = visual_left_to_right * flag_count
            flag_index = min(flag_count - 1, max(0, int(flag_position)))
            local_u = flag_position - flag_index

            slice_start = start + (flag_count - flag_index - 1) * span / flag_count
            slice_end = start + (flag_count - flag_index) * span / flag_count
            if (
                theta - slice_start < separator_width * 0.5
                or slice_end - theta < separator_width * 0.5
            ):
                pixels[x, y] = (0, 0, 0, 255)
                continue

            local_v = (radius - INNER_RADIUS) / (OUTER_RADIUS - INNER_RADIUS)
            flag = flags[flag_index]
            fx = local_u * (flag.width - 1)
            fy = local_v * (flag.height - 1)
            pixels[x, y] = sample_bilinear(flag, fx, fy)

    draw = ImageDraw.Draw(image)
    for index in range(flag_count + 1):
        angle = math.radians(START_DEGREES + index * (END_DEGREES - START_DEGREES) / flag_count)
        x0 = CENTER[0] + math.cos(angle) * INNER_RADIUS
        y0 = CENTER[1] + math.sin(angle) * INNER_RADIUS
        x1 = CENTER[0] + math.cos(angle) * OUTER_RADIUS
        y1 = CENTER[1] + math.sin(angle) * OUTER_RADIUS
        draw.line((x0, y0, x1, y1), fill=(0, 0, 0, 255), width=2 * SCALE)

    return image.resize((SCREEN_SIZE, SCREEN_SIZE), Image.Resampling.LANCZOS)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--runtime-only",
        action="store_true",
        help=(
            "Preserve the supplied iss_flags_cropped.png and only rebuild "
            "the device RGB565A8 asset from it."
        ),
    )
    arguments = parser.parse_args()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    if arguments.runtime_only:
        source = OUTPUT_DIR / "iss_flags_cropped.png"
        if not source.exists():
            raise FileNotFoundError(f"Missing cropped source asset: {source}")
        put_rgb565a8(
            Image.open(source).convert("RGBA"),
            OUTPUT_DIR / "iss_flags.rga",
        )
        return

    image = render()
    image.save(OUTPUT_DIR / "iss_flag_wedge_466.png")

    preview = Image.new("RGBA", image.size, (0, 0, 0, 255))
    preview.alpha_composite(image)
    preview.save(OUTPUT_DIR / "iss_flag_wedge_466_preview.png")

    runtime_image = image.crop(RUNTIME_CROP_BOX)
    runtime_image.save(OUTPUT_DIR / "iss_flags_cropped.png")
    put_rgb565a8(runtime_image, OUTPUT_DIR / "iss_flags.rga")


if __name__ == "__main__":
    main()
