// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreImage
import CoreImage.CIFilterBuiltins

enum GraphiteMapProcessor {
    static func process(_ image: NSImage) -> NSImage {
        guard let tiff = image.tiffRepresentation,
              let input = CIImage(data: tiff)
        else {
            return image
        }

        let sharpen = CIFilter.sharpenLuminance()
        sharpen.inputImage = input
        sharpen.sharpness = 0.46
        guard let output = sharpen.outputImage else { return image }

        let context = CIContext(options: [.useSoftwareRenderer: false])
        guard let cgImage = context.createCGImage(output, from: output.extent) else {
            return image
        }
        return schematicImage(from: cgImage, size: image.size)
    }

    static func drawRadarInstrumentTone(size: CGSize) {
        NSColor(calibratedWhite: 0.0, alpha: 0.32).setFill()
        NSBezierPath(
            rect: CGRect(origin: .zero, size: size)
        ).fill()

        let leftGlow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.12, alpha: 0.024),
            NSColor(calibratedWhite: 0.035, alpha: 0.020),
            .clear,
        ])
        leftGlow?.draw(
            in: NSBezierPath(
                ovalIn: CGRect(
                    x: size.width * -0.24,
                    y: size.height * 0.08,
                    width: size.width * 0.92,
                    height: size.height * 0.88
                )
            ),
            relativeCenterPosition: NSPoint(x: -0.18, y: 0.08)
        )

        let rightGlow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.10, alpha: 0.018),
            .clear,
        ])
        rightGlow?.draw(
            in: NSBezierPath(
                ovalIn: CGRect(
                    x: size.width * 0.39,
                    y: size.height * 0.18,
                    width: size.width * 0.84,
                    height: size.height * 0.71
                )
            ),
            relativeCenterPosition: NSPoint(x: 0.12, y: -0.04)
        )

        NSColor(calibratedWhite: 0.0, alpha: 0.16).setFill()
        NSBezierPath(
            rect: CGRect(origin: .zero, size: size)
        ).fill()
    }

    private static func schematicImage(from cgImage: CGImage, size: CGSize) -> NSImage {
        let width = cgImage.width
        let height = cgImage.height
        let bytesPerRow = width * 4
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo =
            CGBitmapInfo.byteOrder32Big.rawValue |
            CGImageAlphaInfo.premultipliedLast.rawValue
        var source = [UInt8](repeating: 0, count: bytesPerRow * height)

        source.withUnsafeMutableBytes { pixels in
            guard let context = CGContext(
                data: pixels.baseAddress,
                width: width,
                height: height,
                bitsPerComponent: 8,
                bytesPerRow: bytesPerRow,
                space: colorSpace,
                bitmapInfo: bitmapInfo
            ) else {
                return
            }
            context.draw(
                cgImage,
                in: CGRect(x: 0, y: 0, width: width, height: height)
            )
        }

        let pixelCount = width * height
        var luminance = [Float](repeating: 0, count: pixelCount)
        for y in 0..<height {
            for x in 0..<width {
                let index = y * bytesPerRow + x * 4
                let red = Double(source[index]) / 255
                let green = Double(source[index + 1]) / 255
                let blue = Double(source[index + 2]) / 255
                luminance[y * width + x] = Float(
                    0.2126 * red + 0.7152 * green + 0.0722 * blue
                )
            }
        }

        let localAverage = boxBlur(
            luminance,
            width: width,
            height: height,
            radius: 9
        )
        var pixels = [UInt8](repeating: 255, count: bytesPerRow * height)
        for y in 0..<height {
            for x in 0..<width {
                let pixel = y * width + x
                let index = y * bytesPerRow + x * 4
                let lum = Double(luminance[pixel])
                let localLum = Double(localAverage[pixel])
                let contrast = max(
                    0,
                    Double(luminance[pixel] - localAverage[pixel])
                )

                let land = smoothstep(0.018, 0.170, localLum)
                let mapInk = smoothstep(0.070, 0.430, lum)
                let linework = smoothstep(0.010, 0.078, contrast)
                let text = smoothstep(0.430, 0.780, lum)
                let highlight = smoothstep(0.066, 0.145, contrast)
                let graphite =
                    2.0 +
                    land * 4.0 +
                    mapInk * 12.0 +
                    linework * 46.0 +
                    highlight * 24.0 +
                    text * 11.0
                let gray = byte(
                    graphite +
                    linework * 9.0 +
                    highlight * 7.0 +
                    text * 7.0
                )

                pixels[index] = gray
                pixels[index + 1] = gray
                pixels[index + 2] = gray
                pixels[index + 3] = 255
            }
        }

        guard let provider = CGDataProvider(
            data: NSData(bytes: pixels, length: pixels.count)
        ),
        let schematic = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: bytesPerRow,
            space: colorSpace,
            bitmapInfo: CGBitmapInfo(rawValue: bitmapInfo),
            provider: provider,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        ) else {
            return NSImage(cgImage: cgImage, size: size)
        }

        return NSImage(cgImage: schematic, size: size)
    }

    private static func boxBlur(
        _ source: [Float],
        width: Int,
        height: Int,
        radius: Int
    ) -> [Float] {
        var horizontal = [Float](repeating: 0, count: source.count)
        var prefix = [Float](repeating: 0, count: max(width, height) + 1)

        for y in 0..<height {
            prefix[0] = 0
            for x in 0..<width {
                prefix[x + 1] = prefix[x] + source[y * width + x]
            }
            for x in 0..<width {
                let start = max(0, x - radius)
                let end = min(width - 1, x + radius)
                horizontal[y * width + x] =
                    (prefix[end + 1] - prefix[start]) /
                    Float(end - start + 1)
            }
        }

        var blurred = [Float](repeating: 0, count: source.count)
        for x in 0..<width {
            prefix[0] = 0
            for y in 0..<height {
                prefix[y + 1] = prefix[y] + horizontal[y * width + x]
            }
            for y in 0..<height {
                let start = max(0, y - radius)
                let end = min(height - 1, y + radius)
                blurred[y * width + x] =
                    (prefix[end + 1] - prefix[start]) /
                    Float(end - start + 1)
            }
        }
        return blurred
    }

    private static func smoothstep(
        _ edge0: Double,
        _ edge1: Double,
        _ value: Double
    ) -> Double {
        let t = min(1, max(0, (value - edge0) / (edge1 - edge0)))
        return t * t * (3 - 2 * t)
    }

    private static func byte(_ value: Double) -> UInt8 {
        UInt8(min(255, max(0, value)).rounded())
    }
}
