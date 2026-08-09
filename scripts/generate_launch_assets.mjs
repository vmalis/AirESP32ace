// AirESP32ace
// Created by VM, August 2026

import { mkdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";

const outputDirectory = resolve(
  process.argv[2] ?? "assets/space",
);
const launchDirectory = resolve("assets/launch");

mkdirSync(outputDirectory, { recursive: true });

const rockets = [
  {
    name: "launch_sls",
    source: "rockets/sls-orion-master.png",
    crop: "crop=267:1214:493:19",
    rocketWidth: 70,
  },
  {
    name: "launch_falcon9",
    source: "rockets/falcon9-crew-dragon-master.png",
    crop: "crop=119:1190:567:29",
    rocketWidth: 36,
  },
  {
    name: "launch_soyuz",
    source: "rockets/soyuz-ms-master.png",
    crop: "crop=293:1156:479:49",
    rocketWidth: 82,
  },
  {
    name: "launch_starship",
    source: "rockets/starship-super-heavy-master.png",
    crop: "crop=206:1216:524:24",
    rocketWidth: 54,
  },
];

function renderRGBA(source, width, height, filter) {
  const rendered = spawnSync(
    "ffmpeg",
    [
      "-loglevel",
      "error",
      "-i",
      source,
      "-vf",
      filter,
      "-frames:v",
      "1",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1",
    ],
    {
      maxBuffer: width * height * 8,
    },
  );
  if (rendered.status !== 0) {
    process.stderr.write(rendered.stderr);
    process.exit(rendered.status ?? 1);
  }
  const expected = width * height * 4;
  if (rendered.stdout.length !== expected) {
    throw new Error(
      `${source}: expected ${expected} RGBA bytes, got ${rendered.stdout.length}`,
    );
  }
  return rendered.stdout;
}

function rgbaToRGB565A8(rgba, width, height) {
  const pixelCount = width * height;
  const output = Buffer.alloc(pixelCount * 3);
  for (let pixel = 0; pixel < pixelCount; pixel += 1) {
    const source = pixel * 4;
    const red = rgba[source];
    const green = rgba[source + 1];
    const blue = rgba[source + 2];
    const rgb565 =
      ((red >> 3) << 11) |
      ((green >> 2) << 5) |
      (blue >> 3);
    output[pixel * 2] = rgb565 & 0xff;
    output[pixel * 2 + 1] = rgb565 >> 8;
    output[pixelCount * 2 + pixel] = rgba[source + 3];
  }
  return output;
}

function writeRGA(name, source, width, height, filter) {
  const rgba = renderRGBA(source, width, height, filter);
  const output = rgbaToRGB565A8(rgba, width, height);
  const destination = join(outputDirectory, `${name}.rga`);
  writeFileSync(destination, output);
  console.log(
    `${name}: ${width}x${height}, ${output.length} bytes`,
  );
}

for (const rocket of rockets) {
  const width = 140;
  const height = 300;
  writeRGA(
    rocket.name,
    join(launchDirectory, rocket.source),
    width,
    height,
    [
      rocket.crop,
      `scale=${rocket.rocketWidth}:${height}:flags=lanczos`,
      `pad=${width}:${height}:(ow-iw)/2:0:color=black@0`,
      "format=rgba",
    ].join(","),
  );
}

writeRGA(
  "launch_spacex",
  join(launchDirectory, "logos/spacex-soft-white.png"),
  144,
  32,
  [
    "scale=144:32:force_original_aspect_ratio=decrease:flags=lanczos",
    "pad=144:32:(ow-iw)/2:(oh-ih)/2:color=black@0",
    "format=rgba",
  ].join(","),
);
