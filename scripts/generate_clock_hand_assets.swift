// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreGraphics
import Foundation

private struct HandSpec {
    let symbol: String
    let sourcePath: String
    let width: Int
    let height: Int
    let contentOffsetX: Int
    let pivotXOverride: Int?
    let pivotYOverride: Int?
}

private struct RenderedHand {
    let bgra: [UInt8]
    let preview: CGImage
    let pivotX: Int
    let pivotY: Int
}

private struct RenderedEmblem {
    let alpha: [UInt8]
    let preview: CGImage
}

private enum AssetError: Error, CustomStringConvertible {
    case usage
    case cannotLoad(String)
    case cannotCreateContext
    case cannotFindPivot(String)
    case cannotWritePreview(String)

    var description: String {
        switch self {
        case .usage:
            return "usage: swift scripts/generate_clock_hand_assets.swift <minute.png> <hour.png> <emblem.png> <out.c> <out.h> <preview-directory>"
        case .cannotLoad(let path):
            return "cannot load image: \(path)"
        case .cannotCreateContext:
            return "cannot create bitmap context"
        case .cannotFindPivot(let symbol):
            return "cannot locate circular pivot cap: \(symbol)"
        case .cannotWritePreview(let path):
            return "cannot write preview: \(path)"
        }
    }
}

private func loadImage(_ path: String) throws -> CGImage {
    guard let image = NSImage(contentsOfFile: path),
          let tiff = image.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let cgImage = bitmap.cgImage else {
        throw AssetError.cannotLoad(path)
    }
    return cgImage
}

private func detectPivot(
    in rgba: [UInt8],
    width: Int,
    height: Int,
    symbol: String
) throws -> (x: Int, y: Int) {
    /*
     The supplied artwork has a separate white circular cap at the lower end of
     each hand.  Detect that component after resizing so the LVGL rotation axis
     is derived from the pixels that are actually embedded, not an estimate
     made from the source image dimensions.
     */
    let lowerStart = height * 2 / 3
    var bright = [Bool](repeating: false, count: width * height)
    for y in lowerStart..<height {
        for x in 0..<width {
            let pixel = (y * width + x) * 4
            let alpha = Int(rgba[pixel + 3])
            if alpha >= 192 &&
                Int(rgba[pixel]) >= 210 &&
                Int(rgba[pixel + 1]) >= 210 &&
                Int(rgba[pixel + 2]) >= 210 {
                bright[y * width + x] = true
            }
        }
    }

    var visited = [Bool](repeating: false, count: bright.count)
    var best: (pixels: [(Int, Int)], maxY: Int)?
    let directions = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    for y in lowerStart..<height {
        for x in 0..<width {
            let start = y * width + x
            if !bright[start] || visited[start] {
                continue
            }
            var queue = [(x, y)]
            var cursor = 0
            var component: [(Int, Int)] = []
            var maxY = y
            visited[start] = true
            while cursor < queue.count {
                let point = queue[cursor]
                cursor += 1
                component.append(point)
                maxY = max(maxY, point.1)
                for direction in directions {
                    let nx = point.0 + direction.0
                    let ny = point.1 + direction.1
                    if nx < 0 || nx >= width || ny < lowerStart || ny >= height {
                        continue
                    }
                    let index = ny * width + nx
                    if bright[index] && !visited[index] {
                        visited[index] = true
                        queue.append((nx, ny))
                    }
                }
            }
            if component.count < 8 {
                continue
            }
            if best == nil || maxY > best!.maxY ||
                (maxY == best!.maxY && component.count > best!.pixels.count) {
                best = (component, maxY)
            }
        }
    }

    guard let cap = best else {
        throw AssetError.cannotFindPivot(symbol)
    }
    let xs = cap.pixels.map(\.0)
    let ys = cap.pixels.map(\.1)
    let centerX = (xs.min()! + xs.max()! + 1) / 2
    let centerY = (ys.min()! + ys.max()! + 1) / 2
    return (centerX, centerY)
}

private func render(_ spec: HandSpec) throws -> RenderedHand {
    let source = try loadImage(spec.sourcePath)
    let bytesPerRow = spec.width * 4
    var rgba = [UInt8](repeating: 0, count: spec.height * bytesPerRow)
    guard let context = CGContext(
        data: &rgba,
        width: spec.width,
        height: spec.height,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
            CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
        throw AssetError.cannotCreateContext
    }

    context.clear(CGRect(x: 0, y: 0, width: spec.width, height: spec.height))
    context.interpolationQuality = .high
    context.setBlendMode(.copy)
    let scale = CGFloat(spec.height) / CGFloat(source.height)
    let drawWidth = CGFloat(source.width) * scale
    let drawRect = CGRect(
        x: (CGFloat(spec.width) - drawWidth) * 0.5 +
            CGFloat(spec.contentOffsetX),
        y: 0,
        width: drawWidth,
        height: CGFloat(spec.height)
    )
    context.draw(source, in: drawRect)

    guard let preview = context.makeImage() else {
        throw AssetError.cannotCreateContext
    }

    var bgra = [UInt8](repeating: 0, count: rgba.count)
    for pixel in 0..<(spec.width * spec.height) {
        let offset = pixel * 4
        let alpha = Int(rgba[offset + 3])
        var red = Int(rgba[offset])
        var green = Int(rgba[offset + 1])
        var blue = Int(rgba[offset + 2])
        if alpha > 0 && alpha < 255 {
            red = min(255, (red * 255 + alpha / 2) / alpha)
            green = min(255, (green * 255 + alpha / 2) / alpha)
            blue = min(255, (blue * 255 + alpha / 2) / alpha)
        }
        bgra[offset] = UInt8(blue)
        bgra[offset + 1] = UInt8(green)
        bgra[offset + 2] = UInt8(red)
        bgra[offset + 3] = UInt8(alpha)
    }
    let detectedPivot: (x: Int, y: Int)? =
        spec.pivotXOverride == nil ||
            spec.pivotYOverride == nil
            ? try detectPivot(
                in: rgba,
                width: spec.width,
                height: spec.height,
                symbol: spec.symbol)
            : nil
    return RenderedHand(
        bgra: bgra,
        preview: preview,
        pivotX:
            spec.pivotXOverride ??
            (detectedPivot!.x - spec.contentOffsetX),
        pivotY:
            spec.pivotYOverride ??
            detectedPivot!.y)
}

private func renderEmblem(
    _ path: String,
    size: Int
) throws -> RenderedEmblem {
    let source = try loadImage(path)
    let bytesPerRow = size * 4
    var rgba = [UInt8](repeating: 0, count: size * bytesPerRow)
    guard let context = CGContext(
        data: &rgba,
        width: size,
        height: size,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
            CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
        throw AssetError.cannotCreateContext
    }
    context.clear(CGRect(x: 0, y: 0, width: size, height: size))
    context.interpolationQuality = .high
    context.setBlendMode(.copy)
    context.draw(
        source,
        in: CGRect(x: 0, y: 0, width: size, height: size))

    var alpha = [UInt8](repeating: 0, count: size * size)
    var white = [UInt8](repeating: 0, count: rgba.count)
    for pixel in 0..<(size * size) {
        let value = rgba[pixel * 4 + 3]
        alpha[pixel] = value
        white[pixel * 4] = value
        white[pixel * 4 + 1] = value
        white[pixel * 4 + 2] = value
        white[pixel * 4 + 3] = value
    }
    guard let previewContext = CGContext(
        data: &white,
        width: size,
        height: size,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
            CGImageAlphaInfo.premultipliedLast.rawValue
    ), let preview = previewContext.makeImage() else {
        throw AssetError.cannotCreateContext
    }
    return RenderedEmblem(alpha: alpha, preview: preview)
}

private func writePreview(_ image: CGImage, to path: String) throws {
    let bitmap = NSBitmapImageRep(cgImage: image)
    guard let png = bitmap.representation(using: .png, properties: [:]) else {
        throw AssetError.cannotWritePreview(path)
    }
    try png.write(to: URL(fileURLWithPath: path), options: .atomic)
}

private func writeHeader(
    _ specs: [HandSpec],
    rendered: [RenderedHand],
    to path: String
) throws {
    var text = """
    #pragma once

    #include "lvgl.h"

    #define AIRSPACE_CLOCK_HAND_SCALE 128

    """
    for (spec, hand) in zip(specs, rendered) {
        let macro = spec.symbol.uppercased()
        text += "#define \(macro)_PIVOT_X \(hand.pivotX)\n"
        text += "#define \(macro)_PIVOT_Y \(hand.pivotY)\n"
        text += "extern const lv_image_dsc_t \(spec.symbol);\n\n"
    }
    text += "extern const lv_image_dsc_t airspace_clock_crown;\n"
    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

private func writeSource(
    _ specs: [HandSpec],
    images: [[UInt8]],
    emblem: RenderedEmblem,
    to path: String
) throws {
    var text = "#include \"clock_hand_assets.h\"\n\n"
    for (spec, bytes) in zip(specs, images) {
        text += "LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST\n"
        text += "static const uint8_t \(spec.symbol)_map[] = {\n"
        for start in stride(from: 0, to: bytes.count, by: 16) {
            let end = min(bytes.count, start + 16)
            let values = bytes[start..<end]
                .map { String(format: "0x%02x", $0) }
                .joined(separator: ",")
            text += "    \(values),\n"
        }
        text += """
        };

        const lv_image_dsc_t \(spec.symbol) = {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = \(spec.width),
                .h = \(spec.height),
                .stride = \(spec.width * 4),
                .reserved_2 = 0,
            },
            .data_size = sizeof(\(spec.symbol)_map),
            .data = \(spec.symbol)_map,
            .reserved = NULL,
        };

        """
    }
    text += "LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST\n"
    text += "static const uint8_t airspace_clock_crown_map[] = {\n"
    for start in stride(from: 0, to: emblem.alpha.count, by: 16) {
        let end = min(emblem.alpha.count, start + 16)
        let values = emblem.alpha[start..<end]
            .map { String(format: "0x%02x", $0) }
            .joined(separator: ",")
        text += "    \(values),\n"
    }
    text += """
    };

    const lv_image_dsc_t airspace_clock_crown = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_A8,
            .flags = 0,
            .w = 40,
            .h = 40,
            .stride = 40,
            .reserved_2 = 0,
        },
        .data_size = sizeof(airspace_clock_crown_map),
        .data = airspace_clock_crown_map,
        .reserved = NULL,
    };

    """
    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

do {
    guard CommandLine.arguments.count == 7 else {
        throw AssetError.usage
    }
    let minutePath = CommandLine.arguments[1]
    let hourPath = CommandLine.arguments[2]
    let emblemPath = CommandLine.arguments[3]
    let outC = CommandLine.arguments[4]
    let outH = CommandLine.arguments[5]
    let previewDirectory = CommandLine.arguments[6]
    let specs = [
        HandSpec(
            symbol: "airspace_clock_hand_minute",
            sourcePath: minutePath,
            width: 32,
            height: 249,
            contentOffsetX: 0,
            pivotXOverride: nil,
            pivotYOverride: nil
        ),
        HandSpec(
            symbol: "airspace_clock_hand_hour",
            sourcePath: hourPath,
            width: 75,
            height: 201,
            contentOffsetX: 0,
            /* Preserve HOUR2 proportions at 85% of its prior embedded size. */
            pivotXOverride: 38,
            pivotYOverride: 184
        ),
    ]

    try FileManager.default.createDirectory(
        atPath: previewDirectory,
        withIntermediateDirectories: true
    )
    let rendered = try specs.map(render)
    let emblem = try renderEmblem(emblemPath, size: 40)
    try writeHeader(specs, rendered: rendered, to: outH)
    try writeSource(
        specs,
        images: rendered.map(\.bgra),
        emblem: emblem,
        to: outC)
    try writePreview(
        rendered[0].preview,
        to: previewDirectory + "/minute-embedded-2x.png"
    )
    try writePreview(
        rendered[1].preview,
        to: previewDirectory + "/hour-embedded-2x.png"
    )
    try writePreview(
        emblem.preview,
        to: previewDirectory + "/crown-embedded.png"
    )
    print(
        "Generated 2x antialiased clock hands; pivots " +
        "minute=(\(rendered[0].pivotX),\(rendered[0].pivotY)) " +
        "hour=(\(rendered[1].pivotX),\(rendered[1].pivotY))")
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
