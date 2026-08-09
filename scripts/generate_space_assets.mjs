// AirESP32ace
// Created by VM, August 2026

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, join, resolve } from "node:path";

const argumentsList = process.argv.slice(2);
const worldOnly = argumentsList.includes("--world-only");
const agencyOnly = argumentsList.includes("--agency-only");
const positionalArguments = argumentsList.filter(
  (argument) =>
    argument !== "--world-only" &&
    argument !== "--agency-only",
);
const outputDirectory = positionalArguments[0];
const sunSource = positionalArguments[1];
const earthSource = positionalArguments[2];
const moonSource = positionalArguments[3];
const issSource = positionalArguments[4];
const nasaSource =
  positionalArguments[5] ??
  "companion/AirspaceCompanion/Sources/AirspaceCompanion/Resources/iss_nasa.png";
const roscosmosSource =
  positionalArguments[6] ??
  "companion/AirspaceCompanion/Sources/AirspaceCompanion/Resources/iss_roscosmos.png";

if (
  !outputDirectory ||
  !sunSource ||
  !earthSource ||
  !moonSource ||
  !issSource
) {
  console.error(
    "usage: node scripts/generate_space_assets.mjs <output-directory> <sun.png> <earth.png> <moon.png> <iss.png> [nasa.png roscosmos.png] [--world-only|--agency-only]",
  );
  process.exit(2);
}

mkdirSync(outputDirectory, { recursive: true });

function renderRGBA(source, width, height, filter) {
  const rendered = spawnSync(
    "ffmpeg",
    [
      "-loglevel",
      "error",
      "-i",
      source,
      "-vf",
      filter ??
        `scale=${width}:${height}:force_original_aspect_ratio=decrease:flags=lanczos,pad=${width}:${height}:(ow-iw)/2:(oh-ih)/2:color=black@0,format=rgba`,
      "-frames:v",
      "1",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1",
    ],
    { maxBuffer: width * height * 8 },
  );
  if (rendered.status !== 0) {
    process.stderr.write(rendered.stderr);
    process.exit(rendered.status ?? 1);
  }
  if (rendered.stdout.length !== width * height * 4) {
    throw new Error(
      `${basename(source)}: expected ${width * height * 4} RGBA bytes, got ${rendered.stdout.length}`,
    );
  }
  return rendered.stdout;
}

function rgbaToRGB565A8(rgba, width, height, alphaTransform) {
  const pixels = width * height;
  const output = Buffer.alloc(pixels * 3);
  for (let pixelIndex = 0; pixelIndex < pixels; pixelIndex += 1) {
    const input = pixelIndex * 4;
    const red = rgba[input];
    const green = rgba[input + 1];
    const blue = rgba[input + 2];
    const rgb565 =
      ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    output[pixelIndex * 2] = rgb565 & 0xff;
    output[pixelIndex * 2 + 1] = rgb565 >> 8;
    output[pixels * 2 + pixelIndex] = alphaTransform
      ? alphaTransform(red, green, blue, rgba[input + 3])
      : rgba[input + 3];
  }
  return output;
}

function writeRGA(name, source, width, height, options = {}) {
  const rgba = renderRGBA(source, width, height, options.filter);
  const output = rgbaToRGB565A8(
    rgba,
    width,
    height,
    options.alphaTransform,
  );
  const path = join(outputDirectory, `${name}.rga`);
  writeFileSync(path, output);
  console.log(`${name}: ${width}x${height}, ${output.length} bytes`);
}

function worldMapRGB(width, height) {
  const geojsonPath = resolve(
    "companion/AirspaceCompanion/Sources/AirspaceCompanion/Resources/ne_110m_land.geojson",
  );
  const geojson = JSON.parse(readFileSync(geojsonPath, "utf8"));
  const rgb = Buffer.alloc(width * height * 3);
  const ocean = [0, 0, 0];
  const land = [29, 33, 33];
  const coast = [109, 117, 117];
  const greenwich = [184, 42, 53];
  const equator = [187, 194, 194];
  const referenceEdge = [54, 60, 60];
  const projectionCenterX = width / 2;
  const projectionCenterY = 205;
  const projectionScaleX = 78;
  const projectionScaleY = 87;
  const mapLeft = 8;
  const mapTop = 58;
  const mapRight = width - 9;
  const mapBottom = 352;

  function naturalEarthXFactor(latitudeRadians) {
    const latitudeSquared = latitudeRadians * latitudeRadians;
    const latitudeFourth = latitudeSquared * latitudeSquared;
    return (
      0.8707 -
      0.131979 * latitudeSquared +
      latitudeFourth *
        (-0.013791 +
          latitudeFourth *
            (0.003971 * latitudeSquared -
              0.001529 * latitudeFourth))
    );
  }

  function naturalEarthY(latitudeRadians) {
    const latitudeSquared = latitudeRadians * latitudeRadians;
    const latitudeFourth = latitudeSquared * latitudeSquared;
    return (
      latitudeRadians *
      (1.007226 +
        latitudeSquared *
          (0.015085 +
            latitudeFourth *
              (-0.044475 +
                0.028874 * latitudeSquared -
                0.005916 * latitudeFourth)))
    );
  }

  const worldBounds = Array.from({ length: height }, (_, y) => {
    const target = (projectionCenterY - y) / projectionScaleY;
    const northPole = naturalEarthY(Math.PI / 2);
    if (target < -northPole || target > northPole) return null;
    let lower = -Math.PI / 2;
    let upper = Math.PI / 2;
    for (let iteration = 0; iteration < 18; iteration += 1) {
      const middle = (lower + upper) / 2;
      if (naturalEarthY(middle) < target) lower = middle;
      else upper = middle;
    }
    const latitudeRadians = (lower + upper) / 2;
    const halfWidth =
      Math.PI *
      naturalEarthXFactor(latitudeRadians) *
      projectionScaleX;
    return [
      Math.floor(projectionCenterX - halfWidth) - 1,
      Math.ceil(projectionCenterX + halfWidth) + 1,
    ];
  });

  function setPixel(x, y, color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const offset = (Math.trunc(y) * width + Math.trunc(x)) * 3;
    rgb[offset] = color[0];
    rgb[offset + 1] = color[1];
    rgb[offset + 2] = color[2];
  }

  function setMapPixel(x, y, color) {
    const row = worldBounds[Math.trunc(y)];
    if (
      x < mapLeft ||
      x > mapRight ||
      y < mapTop ||
      y > mapBottom ||
      row === null ||
      x < row[0] ||
      x > row[1]
    ) {
      return;
    }
    setPixel(x, y, color);
  }

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) setPixel(x, y, ocean);
  }

  function drawLine(x0, y0, x1, y1, color) {
    x0 = Math.round(x0);
    y0 = Math.round(y0);
    x1 = Math.round(x1);
    y1 = Math.round(y1);
    const dx = Math.abs(x1 - x0);
    const sx = x0 < x1 ? 1 : -1;
    const dy = -Math.abs(y1 - y0);
    const sy = y0 < y1 ? 1 : -1;
    let error = dx + dy;
    while (true) {
      setMapPixel(x0, y0, color);
      if (x0 === x1 && y0 === y1) break;
      const doubled = error * 2;
      if (doubled >= dy) {
        error += dy;
        x0 += sx;
      }
      if (doubled <= dx) {
        error += dx;
        y0 += sy;
      }
    }
  }

  function project(longitude, latitude) {
    const clampedLatitude = Math.max(-90, Math.min(90, latitude));
    const latitudeRadians = (clampedLatitude * Math.PI) / 180;
    const xFactor = naturalEarthXFactor(latitudeRadians);
    const projectedY = naturalEarthY(latitudeRadians);
    return [
      projectionCenterX +
        projectionScaleX *
          ((longitude * Math.PI) / 180) *
          xFactor,
      projectionCenterY - projectionScaleY * projectedY,
    ];
  }

  function drawReferenceLine(a, b, color) {
    drawLine(a[0], a[1], b[0], b[1], referenceEdge);
    if (Math.abs(a[0] - b[0]) < 0.5) {
      drawLine(a[0] - 1, a[1], b[0] - 1, b[1], referenceEdge);
      drawLine(a[0] + 1, a[1], b[0] + 1, b[1], referenceEdge);
    } else {
      drawLine(a[0], a[1] - 1, b[0], b[1] - 1, referenceEdge);
      drawLine(a[0], a[1] + 1, b[0], b[1] + 1, referenceEdge);
    }
    drawLine(a[0], a[1], b[0], b[1], color);
  }

  function drawProjectedReference(
    fixedCoordinate,
    coordinateStart,
    coordinateEnd,
    isMeridian,
    color,
  ) {
    let previous = isMeridian
      ? project(fixedCoordinate, coordinateStart)
      : project(coordinateStart, fixedCoordinate);
    for (
      let coordinate = coordinateStart + 1;
      coordinate <= coordinateEnd;
      coordinate += 1
    ) {
      const current = isMeridian
        ? project(fixedCoordinate, coordinate)
        : project(coordinate, fixedCoordinate);
      drawReferenceLine(previous, current, color);
      previous = current;
    }
  }

  function unwrappedRing(ring) {
    const points = [];
    let previousLongitude;
    for (const [longitude, latitude] of ring) {
      let unwrappedLongitude = longitude;
      if (previousLongitude !== undefined) {
        while (unwrappedLongitude - previousLongitude > 180) {
          unwrappedLongitude -= 360;
        }
        while (unwrappedLongitude - previousLongitude < -180) {
          unwrappedLongitude += 360;
        }
      }
      points.push([unwrappedLongitude, latitude]);
      previousLongitude = unwrappedLongitude;
    }
    return points;
  }

  function isSouthPolarCap(ring) {
    const longitudes = ring.map((point) => point[0]);
    const latitudes = ring.map((point) => point[1]);
    return (
      Math.min(...latitudes) <= -89 &&
      Math.max(...longitudes) - Math.min(...longitudes) >= 359
    );
  }

  function projectedRingCopies(ring) {
    /*
     * Antarctica deliberately closes across the South Pole from
     * +180 to -180. Longitude unwrapping collapses those two points
     * together and inverts the polar fill, painting ocean as land.
     */
    if (isSouthPolarCap(ring)) {
      return [
        ring.map(([longitude, latitude]) =>
          project(longitude, latitude),
        ),
      ];
    }
    const base = unwrappedRing(ring);
    return [-360, 0, 360].map((longitudeShift) =>
      base.map(([longitude, latitude]) =>
        project(longitude + longitudeShift, latitude),
      ),
    );
  }

  function fillRing(points, color) {
    if (points.length < 3) return;
    const minimumY = Math.max(
      0,
      Math.floor(Math.min(...points.map((p) => p[1]))),
    );
    const maximumY = Math.min(
      height - 1,
      Math.ceil(Math.max(...points.map((p) => p[1]))),
    );
    for (let y = minimumY; y <= maximumY; y += 1) {
      const scanY = y + 0.5;
      const intersections = [];
      for (let index = 0; index < points.length; index += 1) {
        const a = points[index];
        const b = points[(index + 1) % points.length];
        if ((a[1] > scanY) === (b[1] > scanY)) continue;
        intersections.push(
          a[0] +
            ((scanY - a[1]) * (b[0] - a[0])) / (b[1] - a[1]),
        );
      }
      intersections.sort((a, b) => a - b);
      for (let index = 0; index + 1 < intersections.length; index += 2) {
        const start = Math.max(
          0,
          Math.ceil(intersections[index]),
        );
        const end = Math.min(
          width - 1,
          Math.floor(intersections[index + 1]),
        );
        for (let x = start; x <= end; x += 1) {
          setMapPixel(x, y, color);
        }
      }
    }
  }

  function fillPolygon(polygon) {
    for (let ringIndex = 0; ringIndex < polygon.length; ringIndex += 1) {
      for (const projected of projectedRingCopies(polygon[ringIndex])) {
        fillRing(projected, ringIndex === 0 ? land : ocean);
      }
    }
  }

  function strokePolygon(polygon) {
    for (const ring of polygon) {
      for (const projected of projectedRingCopies(ring)) {
        for (let index = 0; index + 1 < projected.length; index += 1) {
          drawLine(
            projected[index][0],
            projected[index][1],
            projected[index + 1][0],
            projected[index + 1][1],
            coast,
          );
        }
      }
    }
  }

  function forEachPolygon(callback) {
    for (const feature of geojson.features ?? []) {
      const geometry = feature.geometry;
      if (!geometry) continue;
      if (geometry.type === "Polygon") {
        callback(geometry.coordinates);
      } else if (geometry.type === "MultiPolygon") {
        for (const polygon of geometry.coordinates) callback(polygon);
      }
    }
  }

  forEachPolygon(fillPolygon);
  forEachPolygon(strokePolygon);
  drawProjectedReference(0, -90, 90, true, greenwich);
  drawProjectedReference(0, -180, 180, false, equator);

  return rgb;
}

function writeWorldMap() {
  const width = 466;
  const height = 466;
  const rgb = worldMapRGB(width, height);
  const pixels = width * height;
  const output = Buffer.alloc(pixels * 2);
  for (let pixelIndex = 0; pixelIndex < pixels; pixelIndex += 1) {
    const input = pixelIndex * 3;
    const red = rgb[input];
    const green = rgb[input + 1];
    const blue = rgb[input + 2];
    const rgb565 =
      ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    output[pixelIndex * 2] = rgb565 & 0xff;
    output[pixelIndex * 2 + 1] = rgb565 >> 8;
  }
  writeFileSync(join(outputDirectory, "world.rgb"), output);
  console.log(`world: ${width}x${height}, ${output.length} bytes`);
}

if (!worldOnly && !agencyOnly) {
  writeRGA("sun", sunSource, 128, 128);
  writeRGA("earth", earthSource, 80, 80);
  writeRGA("moon", moonSource, 64, 64);
  writeRGA("iss", issSource, 64, 64, {
    filter:
      "crop=300:300:104:30,scale=64:64:flags=lanczos,format=rgba",
    alphaTransform: (red, green, blue) => {
      const luminance = (red * 54 + green * 183 + blue * 19) >> 8;
      return Math.max(0, Math.min(255, (244 - luminance) * 6));
    },
  });
}
if (!worldOnly) {
  writeRGA("nasa", nasaSource, 72, 60, {
    filter:
      "scale=54:45:force_original_aspect_ratio=decrease:flags=lanczos,pad=72:60:(ow-iw)/2:(oh-ih)/2:color=black@0,format=rgba",
  });
  writeRGA("roscosm", roscosmosSource, 60, 60, {
    filter:
      "scale=45:45:force_original_aspect_ratio=decrease:flags=lanczos,pad=60:60:(ow-iw)/2:(oh-ih)/2:color=black@0,format=rgba",
  });
}
if (!agencyOnly) {
  writeWorldMap();
}
