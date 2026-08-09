// AirESP32ace
// Created by VM, August 2026

import { mkdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join } from "node:path";

const size = 128;
const sources = {
  SJC: "Cities/SJ.png",
  SFO: "Cities/SFO.png",
  SAN: "Cities/SanDiego.png",
  JFK: "Cities/NY.png",
  MOW: "Cities/moscow.png",
  LED: "Cities/StPetersburgh.png",
  DUS: "Cities/Dusseldorf.png",
  ICN: "Cities/Seoul.png",
};

const outputDirectory = process.argv[2];
if (!outputDirectory) {
  console.error(
    "usage: node scripts/generate_weather_city_icons.mjs <output-directory>",
  );
  process.exit(2);
}
mkdirSync(outputDirectory, { recursive: true });

for (const [key, source] of Object.entries(sources)) {
  const rendered = spawnSync(
    "ffmpeg",
    [
      "-loglevel",
      "error",
      "-i",
      source,
      "-vf",
      `scale=${size}:${size}:flags=lanczos,format=rgba`,
      "-frames:v",
      "1",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1",
    ],
    { maxBuffer: size * size * 8 },
  );
  if (rendered.status !== 0) {
    process.stderr.write(rendered.stderr);
    process.exit(rendered.status ?? 1);
  }

  const rgba = rendered.stdout;
  const pixels = size * size;
  if (rgba.length !== pixels * 4) {
    throw new Error(`${source}: expected ${pixels * 4} bytes, got ${rgba.length}`);
  }
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
    output[pixels * 2 + pixelIndex] = rgba[input + 3];
  }
  writeFileSync(join(outputDirectory, `${key}.rga`), output);
  console.log(`${key}: ${output.length} bytes`);
}
