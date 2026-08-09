// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation

enum ImageWriter {
    private static let devicePixelWidth = 466
    private static let devicePixelHeight = 466

    struct DeviceImageAsset {
        let pngURL: URL
        let rgb565URL: URL
    }

    static func existingDeviceImage(name: String) throws -> DeviceImageAsset {
        let directory = try outputDirectory()
        let asset = DeviceImageAsset(
            pngURL: directory.appendingPathComponent("\(name).png"),
            rgb565URL: directory.appendingPathComponent("\(name).rgb565")
        )
        guard FileManager.default.fileExists(atPath: asset.pngURL.path),
              FileManager.default.fileExists(atPath: asset.rgb565URL.path)
        else {
            throw CocoaError(.fileNoSuchFile)
        }
        return asset
    }

    static func writeDeviceImage(_ image: NSImage, name: String) throws -> DeviceImageAsset {
        let pngURL = try writePNG(image, name: name)
        let rgb565URL = try writeRGB565(image, name: name)
        return DeviceImageAsset(pngURL: pngURL, rgb565URL: rgb565URL)
    }

    static func rgb565Data(
        from image: NSImage,
        width: Int,
        height: Int
    ) throws -> Data {
        let bitmap = try normalizedBitmap(
            from: image,
            width: width,
            height: height
        )
        return rgb565Data(from: bitmap)
    }

    static func rgb565AlphaData(
        from image: NSImage,
        width: Int,
        height: Int
    ) throws -> Data {
        let bitmap = try normalizedBitmap(
            from: image,
            width: width,
            height: height,
            clearBackground: true
        )
        var color = Data(capacity: width * height * 2)
        var alpha = Data(capacity: width * height)
        for y in 0..<height {
            for x in 0..<width {
                let sample = bitmap
                    .colorAt(x: x, y: y)?
                    .usingColorSpace(.sRGB) ?? .clear
                let red = UInt16(
                    max(
                        0,
                        min(
                            31,
                            Int(round(sample.redComponent * 31))
                        )
                    )
                )
                let green = UInt16(
                    max(
                        0,
                        min(
                            63,
                            Int(round(sample.greenComponent * 63))
                        )
                    )
                )
                let blue = UInt16(
                    max(
                        0,
                        min(
                            31,
                            Int(round(sample.blueComponent * 31))
                        )
                    )
                )
                let pixel =
                    UInt16((red << 11) | (green << 5) | blue)
                color.append(UInt8(pixel & 0xff))
                color.append(UInt8(pixel >> 8))
                alpha.append(
                    UInt8(
                        max(
                            0,
                            min(
                                255,
                                Int(
                                    round(
                                        sample.alphaComponent * 255
                                    )
                                )
                            )
                        )
                    )
                )
            }
        }
        color.append(alpha)
        return color
    }

    static func writePNG(_ image: NSImage, name: String) throws -> URL {
        let directory = try outputDirectory()
        let url = directory.appendingPathComponent("\(name).png")

        let bitmap = try normalizedDeviceBitmap(from: image)
        guard let data = bitmap.representation(using: .png, properties: [:]) else {
            throw ImageWriterError.couldNotEncodePNG
        }

        try data.write(to: url, options: .atomic)
        return url
    }

    private static func writeRGB565(_ image: NSImage, name: String) throws -> URL {
        let directory = try outputDirectory()
        let url = directory.appendingPathComponent("\(name).rgb565")

        let bitmap = try normalizedDeviceBitmap(from: image)
        let data = rgb565Data(from: bitmap)
        try data.write(to: url, options: .atomic)
        return url
    }

    private static func rgb565Data(from bitmap: NSBitmapImageRep) -> Data {
        var data = Data(capacity: bitmap.pixelsWide * bitmap.pixelsHigh * 2)
        for y in 0..<bitmap.pixelsHigh {
            for x in 0..<bitmap.pixelsWide {
                let color = bitmap.colorAt(x: x, y: y)?.usingColorSpace(.sRGB) ?? .black
                let red = UInt16(max(0, min(31, Int(round(color.redComponent * 31)))))
                let green = UInt16(max(0, min(63, Int(round(color.greenComponent * 63)))))
                let blue = UInt16(max(0, min(31, Int(round(color.blueComponent * 31)))))
                let pixel = UInt16((red << 11) | (green << 5) | blue)
                data.append(UInt8(pixel & 0xff))
                data.append(UInt8(pixel >> 8))
            }
        }
        return data
    }

    private static func normalizedDeviceBitmap(from image: NSImage) throws -> NSBitmapImageRep {
        try normalizedBitmap(
            from: image,
            width: devicePixelWidth,
            height: devicePixelHeight
        )
    }

    private static func normalizedBitmap(
        from image: NSImage,
        width: Int,
        height: Int,
        clearBackground: Bool = false
    ) throws -> NSBitmapImageRep {
        guard let bitmap = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: width,
            pixelsHigh: height,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 0,
            bitsPerPixel: 0
        ) else {
            throw ImageWriterError.couldNotReadPixels
        }

        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        (clearBackground ? NSColor.clear : NSColor.black).setFill()
        NSRect(x: 0, y: 0, width: width, height: height).fill()
        image.draw(
            in: NSRect(x: 0, y: 0, width: width, height: height),
            from: .zero,
            operation: .copy,
            fraction: 1.0
        )
        NSGraphicsContext.restoreGraphicsState()

        return bitmap
    }

    private static func outputDirectory() throws -> URL {
        let pictures = FileManager.default.urls(for: .picturesDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser
        let directory = pictures.appendingPathComponent("AirspaceInstrument", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory
    }
}

enum ImageWriterError: LocalizedError {
    case couldNotEncodePNG
    case couldNotReadPixels

    var errorDescription: String? {
        switch self {
        case .couldNotEncodePNG:
            "Could not encode the route map as PNG."
        case .couldNotReadPixels:
            "Could not read pixels from the route map."
        }
    }
}
