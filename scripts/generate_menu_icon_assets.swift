// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreGraphics
import Foundation

private let cityIconSize = 56
private let hotspotIconSize = 28

enum AssetError: Error, CustomStringConvertible {
    case usage
    case cannotLoadImage(String)
    case cannotReadBitmap(String)
    case cannotCreateContext

    var description: String {
        switch self {
        case .usage:
            return "usage: swift scripts/generate_menu_icon_assets.swift <out.c> <out.h> <name=source.png>..."
        case .cannotLoadImage(let path):
            return "cannot load image: \(path)"
        case .cannotReadBitmap(let path):
            return "cannot read bitmap pixels: \(path)"
        case .cannotCreateContext:
            return "cannot create drawing context"
        }
    }
}

struct IconInput {
    let name: String
    let path: String
}

struct RenderedIcon {
    let size: Int
    let bytes: [UInt8]
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

func renderIcon(_ input: IconInput) throws -> RenderedIcon {
    let cgImage = try loadImage(path: input.path)
    let isMonochromeSymbol = input.name == "hotspot"
    let iconSize = isMonochromeSymbol ? hotspotIconSize : cityIconSize
    let bytesPerRow = iconSize * 4
    var rgba = [UInt8](repeating: 0, count: iconSize * bytesPerRow)
    let bitmapInfo = CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue

    guard let context = CGContext(
        data: &rgba,
        width: iconSize,
        height: iconSize,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: bitmapInfo
    ) else {
        throw AssetError.cannotCreateContext
    }

    context.clear(CGRect(x: 0, y: 0, width: iconSize, height: iconSize))
    context.interpolationQuality = .high
    context.draw(cgImage, in: CGRect(x: 0, y: 0, width: iconSize, height: iconSize))

    var rgb565: [UInt8] = []
    var alpha: [UInt8] = []
    rgb565.reserveCapacity(iconSize * iconSize * 2)
    alpha.reserveCapacity(iconSize * iconSize)

    for y in 0..<iconSize {
        for x in 0..<iconSize {
            let i = y * bytesPerRow + x * 4
            var r = rgba[i]
            var g = rgba[i + 1]
            var b = rgba[i + 2]
            var a = rgba[i + 3]

            if isMonochromeSymbol {
                let luminance = 0.2126 * Double(r) + 0.7152 * Double(g) + 0.0722 * Double(b)
                let ink = max(0.0, min(1.0, (245.0 - luminance) / 245.0))
                r = 0xd8
                g = 0xde
                b = 0xd9
                a = UInt8(max(0.0, min(255.0, ink * 255.0)).rounded())
            }

            let pixel = (UInt16(r >> 3) << 11) | (UInt16(g >> 2) << 5) | UInt16(b >> 3)
            rgb565.append(UInt8(pixel & 0xff))
            rgb565.append(UInt8(pixel >> 8))
            alpha.append(a)
        }
    }

    return RenderedIcon(size: iconSize, bytes: rgb565 + alpha)
}

func symbolName(_ name: String) -> String {
    "airspace_menu_icon_" + name
        .lowercased()
        .map { ch in
            if ch >= "a" && ch <= "z" { return String(ch) }
            if ch >= "0" && ch <= "9" { return String(ch) }
            return "_"
        }
        .joined()
}

func writeHeader(path: String, inputs: [IconInput]) throws {
    var text = """
    #pragma once

    #include "lvgl.h"

    const lv_image_dsc_t *airspace_city_icon_for_key(const char *key);
    extern const lv_image_dsc_t airspace_menu_icon_hotspot;

    """

    for input in inputs where input.name != "hotspot" {
        text += "extern const lv_image_dsc_t \(symbolName(input.name));\n"
    }

    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

func writeSource(path: String, inputs: [IconInput], rendered: [RenderedIcon]) throws {
    var text = """
    #include "menu_icons.h"

    #include <strings.h>

    """

    for (input, icon) in zip(inputs, rendered) {
        let symbol = symbolName(input.name)
        text += """
        LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST
        const uint8_t \(symbol)_map[] = {

        """

        for chunkStart in stride(from: 0, to: icon.bytes.count, by: 16) {
            let chunkEnd = min(icon.bytes.count, chunkStart + 16)
            let values = icon.bytes[chunkStart..<chunkEnd]
                .map { String(format: "0x%02x", $0) }
                .joined(separator: ",")
            text += "    \(values),\n"
        }

        text += """

        };

        const lv_image_dsc_t \(symbol) = {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565A8,
                .flags = 0,
                .w = \(icon.size),
                .h = \(icon.size),
                .stride = \(icon.size * 2),
                .reserved_2 = 0,
            },
            .data_size = sizeof(\(symbol)_map),
            .data = \(symbol)_map,
            .reserved = NULL,
        };

        """
    }

    text += """
    const lv_image_dsc_t *airspace_city_icon_for_key(const char *key)
    {
        if (key == NULL) {
            return NULL;
        }

    """

    for input in inputs where input.name != "hotspot" {
        let upper = input.name.uppercased()
        text += """
        if (strcasecmp(key, "\(upper)") == 0) {
            return &\(symbolName(input.name));
        }

        """
    }

    text += """
        return NULL;
    }

    """

    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

do {
    guard CommandLine.arguments.count >= 5 else {
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

    let rendered = try inputs.map(renderIcon)
    try writeHeader(path: outH, inputs: inputs)
    try writeSource(path: outC, inputs: inputs, rendered: rendered)
    print("Generated \(inputs.count) menu icons (\(cityIconSize)px cities, \(hotspotIconSize)px hotspot)")
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
