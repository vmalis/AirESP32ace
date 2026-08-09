// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreGraphics
import Foundation

private let iconSize = 42

enum AssetError: Error, CustomStringConvertible {
    case usage
    case cannotLoadImage(String)
    case cannotReadBitmap(String)
    case emptyImage(String)
    case cannotCreateContext

    var description: String {
        switch self {
        case .usage:
            return "usage: swift scripts/generate_detail_metric_assets.swift <out.c> <out.h> <name=source.png>..."
        case .cannotLoadImage(let path):
            return "cannot load image: \(path)"
        case .cannotReadBitmap(let path):
            return "cannot read bitmap pixels: \(path)"
        case .emptyImage(let path):
            return "source image has no visible pixels: \(path)"
        case .cannotCreateContext:
            return "cannot create drawing context"
        }
    }
}

struct IconInput {
    let name: String
    let path: String
}

func loadImage(path: String) throws -> CGImage {
    guard let image = NSImage(contentsOfFile: path) else {
        throw AssetError.cannotLoadImage(path)
    }
    guard let tiff = image.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let cgImage = bitmap.cgImage
    else {
        throw AssetError.cannotReadBitmap(path)
    }
    return cgImage
}

func visibleBounds(_ image: CGImage, path: String) throws -> CGRect {
    let width = image.width
    let height = image.height
    let bytesPerRow = width * 4
    var rgba = [UInt8](repeating: 0, count: height * bytesPerRow)
    guard let context = CGContext(
        data: &rgba,
        width: width,
        height: height,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
        throw AssetError.cannotCreateContext
    }
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))

    var minX = width
    var minY = height
    var maxX = -1
    var maxY = -1
    for y in 0..<height {
        for x in 0..<width {
            if rgba[y * bytesPerRow + x * 4 + 3] > 3 {
                minX = min(minX, x)
                minY = min(minY, y)
                maxX = max(maxX, x)
                maxY = max(maxY, y)
            }
        }
    }
    guard maxX >= minX, maxY >= minY else {
        throw AssetError.emptyImage(path)
    }
    let padding = max(4.0, Double(max(maxX - minX, maxY - minY)) * 0.08)
    let crop = CGRect(
        x: max(0.0, Double(minX) - padding),
        y: max(0.0, Double(minY) - padding),
        width: min(Double(width), Double(maxX + 1) + padding) - max(0.0, Double(minX) - padding),
        height: min(Double(height), Double(maxY + 1) + padding) - max(0.0, Double(minY) - padding)
    )
    let side = max(crop.width, crop.height)
    return CGRect(
        x: max(0.0, min(Double(width) - side, crop.midX - side / 2.0)),
        y: max(0.0, min(Double(height) - side, crop.midY - side / 2.0)),
        width: min(side, Double(width)),
        height: min(side, Double(height))
    )
}

func renderMask(_ input: IconInput) throws -> [UInt8] {
    let image = try loadImage(path: input.path)
    let bounds = try visibleBounds(image, path: input.path)
    guard let cropped = image.cropping(to: bounds.integral) else {
        throw AssetError.cannotReadBitmap(input.path)
    }

    let bytesPerRow = iconSize * 4
    var rgba = [UInt8](repeating: 0, count: iconSize * bytesPerRow)
    guard let context = CGContext(
        data: &rgba,
        width: iconSize,
        height: iconSize,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
        throw AssetError.cannotCreateContext
    }
    context.clear(CGRect(x: 0, y: 0, width: iconSize, height: iconSize))
    context.interpolationQuality = .high
    context.draw(cropped, in: CGRect(x: 0, y: 0, width: iconSize, height: iconSize))

    var mask = [UInt8](repeating: 0, count: iconSize * iconSize)
    for y in 0..<iconSize {
        for x in 0..<iconSize {
            let offset = y * bytesPerRow + x * 4
            let alpha = Double(rgba[offset + 3]) / 255.0
            let red = Double(rgba[offset]) / 255.0
            let green = Double(rgba[offset + 1]) / 255.0
            let blue = Double(rgba[offset + 2]) / 255.0
            let luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
            let coverage = alpha * (0.32 + 0.68 * (1.0 - luminance))
            mask[y * iconSize + x] = UInt8(max(0.0, min(255.0, coverage * 255.0)).rounded())
        }
    }
    return mask
}

func symbolName(_ name: String) -> String {
    "airspace_detail_metric_" + name
        .lowercased()
        .map { character in
            if character >= "a" && character <= "z" { return String(character) }
            if character >= "0" && character <= "9" { return String(character) }
            return "_"
        }
        .joined()
}

func writeHeader(path: String, inputs: [IconInput]) throws {
    var text = """
    #pragma once

    #include "lvgl.h"

    """
    for input in inputs {
        text += "extern const lv_image_dsc_t \(symbolName(input.name));\n"
    }
    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

func writeSource(path: String, inputs: [IconInput], masks: [[UInt8]]) throws {
    var text = """
    #include "detail_metric_icons.h"

    """
    for (input, mask) in zip(inputs, masks) {
        let symbol = symbolName(input.name)
        text += """
        LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST
        static const uint8_t \(symbol)_map[] = {

        """
        for start in stride(from: 0, to: mask.count, by: 18) {
            let end = min(mask.count, start + 18)
            let values = mask[start..<end]
                .map { String(format: "0x%02x", $0) }
                .joined(separator: ",")
            text += "    \(values),\n"
        }
        text += """

        };

        const lv_image_dsc_t \(symbol) = {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_A8,
                .flags = 0,
                .w = \(iconSize),
                .h = \(iconSize),
                .stride = \(iconSize),
                .reserved_2 = 0,
            },
            .data_size = sizeof(\(symbol)_map),
            .data = \(symbol)_map,
            .reserved = NULL,
        };

        """
    }
    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

do {
    guard CommandLine.arguments.count >= 6 else {
        throw AssetError.usage
    }
    let outC = CommandLine.arguments[1]
    let outH = CommandLine.arguments[2]
    let inputs = try CommandLine.arguments.dropFirst(3).map { argument -> IconInput in
        let parts = argument.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
        guard parts.count == 2, !parts[0].isEmpty, !parts[1].isEmpty else {
            throw AssetError.usage
        }
        return IconInput(name: String(parts[0]), path: String(parts[1]))
    }
    let masks = try inputs.map(renderMask)
    try writeHeader(path: outH, inputs: inputs)
    try writeSource(path: outC, inputs: inputs, masks: masks)
    print("Generated \(inputs.count) detail metric icons at \(iconSize)x\(iconSize) A8")
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
