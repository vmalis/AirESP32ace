// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreImage
import CoreImage.CIFilterBuiltins
import CoreLocation
import Foundation
import MapKit

struct LocalRadarMapRenderer: Sendable {
    static let sanJose = CLLocationCoordinate2D(latitude: 37.3382, longitude: -121.8863)
    static let supportedRadiusMiles: [Double] = [10, 20, 50]

    private static let metersPerMile = 1_609.344
    private static let metersPerDegreeLatitude = 111_320.0

    private enum SchematicLineKind {
        case shoreline
        case freeway
        case bridge
    }

    private struct SchematicLine {
        let kind: SchematicLineKind
        let name: String?
        let points: [CLLocationCoordinate2D]
    }

    private struct SchematicLabel {
        let text: String
        let coordinate: CLLocationCoordinate2D
        let minRadiusMiles: Double
        let maxRadiusMiles: Double
        let size: CGFloat
    }

    func renderMap(
        center: CLLocationCoordinate2D = LocalRadarMapRenderer.sanJose,
        radiusMiles: Double = 30,
        size: CGSize = CGSize(width: 466, height: 466)
    ) async throws -> NSImage {
        try await drawMapKitRadarPlate(center: center, radiusMiles: radiusMiles, size: size)
    }

    private func drawVectorRadarPlate(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) -> NSImage {
        let image = NSImage(size: size)
        image.lockFocus()
        defer { image.unlockFocus() }

        NSColor(calibratedWhite: 0.002, alpha: 1).setFill()
        NSBezierPath(rect: CGRect(origin: .zero, size: size)).fill()

        drawVectorWaterTone(center: center, radiusMiles: radiusMiles, size: size)
        drawSchematicLines(center: center, radiusMiles: radiusMiles, size: size)
        drawSchematicLabels(center: center, radiusMiles: radiusMiles, size: size)
        drawInstrumentTone(size: size)
        drawCenterReference(point: CGPoint(x: size.width / 2, y: size.height / 2))

        return image
    }

    private func drawMapKitRadarPlate(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) async throws -> NSImage {
        let snapshotScale: CGFloat = 2.0
        let options = MKMapSnapshotter.Options()
        options.size = CGSize(width: size.width * snapshotScale, height: size.height * snapshotScale)
        options.region = MKCoordinateRegion(
            center: center,
            latitudinalMeters: radiusMiles * 2.0 * Self.metersPerMile,
            longitudinalMeters: radiusMiles * 2.0 * Self.metersPerMile
        )
        let configuration = MKStandardMapConfiguration(elevationStyle: .flat, emphasisStyle: .muted)
        configuration.pointOfInterestFilter = .excludingAll
        configuration.showsTraffic = false
        options.preferredConfiguration = configuration
        options.appearance = NSAppearance(named: .darkAqua)

        let snapshot = try await snapshot(with: options)
        return drawRadarPlate(snapshot: snapshot, center: center, radiusMiles: radiusMiles, size: size)
    }

    private func drawVectorWaterTone(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) {
        let waterLines = Self.schematicLines.filter { $0.kind == .shoreline }
        for line in waterLines {
            drawProjectedPolyline(
                line.points,
                center: center,
                radiusMiles: radiusMiles,
                size: size,
                color: NSColor(calibratedWhite: 0.30, alpha: 0.16),
                width: radiusMiles <= 20 ? 1.2 : 1.0
            )
        }

        let coastGlow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.16, alpha: 0.10),
            NSColor(calibratedWhite: 0.04, alpha: 0.04),
            .clear
        ])
        coastGlow?.draw(
            in: NSBezierPath(ovalIn: CGRect(x: -96, y: 64, width: 292, height: 344)),
            relativeCenterPosition: NSPoint(x: -0.22, y: 0.12)
        )
    }

    private func drawSchematicLines(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) {
        for line in Self.schematicLines where line.kind != .shoreline {
            let baseWidth: CGFloat
            let baseAlpha: CGFloat
            switch line.kind {
            case .shoreline:
                baseWidth = 1.0
                baseAlpha = 0.14
            case .freeway:
                baseWidth = radiusMiles <= 10 ? 2.3 : (radiusMiles <= 50 ? 1.8 : 1.35)
                baseAlpha = radiusMiles <= 20 ? 0.52 : 0.42
            case .bridge:
                baseWidth = radiusMiles <= 50 ? 1.55 : 1.2
                baseAlpha = 0.32
            }

            drawProjectedPolyline(
                line.points,
                center: center,
                radiusMiles: radiusMiles,
                size: size,
                color: NSColor(calibratedWhite: 0.24, alpha: baseAlpha * 0.30),
                width: baseWidth + 2.2
            )
            drawProjectedPolyline(
                line.points,
                center: center,
                radiusMiles: radiusMiles,
                size: size,
                color: NSColor(calibratedWhite: 0.62, alpha: baseAlpha),
                width: baseWidth
            )

            if let name = line.name, radiusMiles <= 50 {
                drawLineShield(
                    name,
                    near: line.points[line.points.count / 2],
                    center: center,
                    radiusMiles: radiusMiles,
                    size: size
                )
            }
        }
    }

    private func drawSchematicLabels(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) {
        for label in Self.schematicLabels {
            guard radiusMiles >= label.minRadiusMiles, radiusMiles <= label.maxRadiusMiles else {
                continue
            }
            let point = project(label.coordinate, center: center, radiusMiles: radiusMiles, size: size)
            guard point.x > -40, point.x < size.width + 40, point.y > -18, point.y < size.height + 18 else {
                continue
            }

            let attrs: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: label.size, weight: label.text == "San Jose" ? .semibold : .medium),
                .foregroundColor: NSColor(calibratedWhite: label.text == "San Jose" ? 0.78 : 0.60, alpha: label.text == "San Jose" ? 0.86 : 0.58),
                .kern: 0.4
            ]
            let text = NSAttributedString(string: label.text, attributes: attrs)
            let textSize = text.size()
            text.draw(at: CGPoint(x: point.x - textSize.width / 2, y: point.y - textSize.height / 2))
        }
    }

    private func drawEnhancedCityLabels(
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) {
        for label in Self.schematicLabels {
            guard radiusMiles >= label.minRadiusMiles, radiusMiles <= label.maxRadiusMiles else {
                continue
            }
            guard shouldEnhanceCityLabel(label.text, radiusMiles: radiusMiles) else {
                continue
            }

            let point = project(label.coordinate, center: center, radiusMiles: radiusMiles, size: size)
            guard point.x > -46, point.x < size.width + 46, point.y > -20, point.y < size.height + 20 else {
                continue
            }

            let isPrimary = label.text == "San Jose" || label.text == "San Francisco" || label.text == "Oakland"
            let fontSize = label.size + (isPrimary ? 2.0 : 0.8)
            let fontWeight: NSFont.Weight = isPrimary ? .semibold : .medium
            let text = NSAttributedString(
                string: label.text,
                attributes: [
                    .font: NSFont.systemFont(ofSize: fontSize, weight: fontWeight),
                    .foregroundColor: NSColor(calibratedWhite: isPrimary ? 0.78 : 0.62, alpha: isPrimary ? 0.90 : 0.74),
                    .kern: 0.25
                ]
            )
            let halo = NSAttributedString(
                string: label.text,
                attributes: [
                    .font: NSFont.systemFont(ofSize: fontSize, weight: fontWeight),
                    .foregroundColor: NSColor(calibratedWhite: 0.0, alpha: 0.78),
                    .kern: 0.25
                ]
            )
            let textSize = text.size()
            let origin = CGPoint(x: point.x - textSize.width / 2, y: point.y - textSize.height / 2)

            for offset in [
                CGPoint(x: -1.0, y: 0.0),
                CGPoint(x: 1.0, y: 0.0),
                CGPoint(x: 0.0, y: -1.0),
                CGPoint(x: 0.0, y: 1.0)
            ] {
                halo.draw(at: CGPoint(x: origin.x + offset.x, y: origin.y + offset.y))
            }
            text.draw(at: origin)
        }
    }

    private func shouldEnhanceCityLabel(_ text: String, radiusMiles: Double) -> Bool {
        switch text {
        case "San Jose":
            return true
        case "San Francisco":
            return radiusMiles >= 35
        case "Oakland":
            return radiusMiles >= 70
        default:
            return false
        }
    }

    private func drawProjectedPolyline(
        _ coordinates: [CLLocationCoordinate2D],
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize,
        color: NSColor,
        width: CGFloat
    ) {
        guard coordinates.count >= 2 else {
            return
        }

        let path = NSBezierPath()
        path.lineCapStyle = .round
        path.lineJoinStyle = .round
        path.lineWidth = width

        var didMove = false
        for coordinate in coordinates {
            let point = project(coordinate, center: center, radiusMiles: radiusMiles, size: size)
            if !didMove {
                path.move(to: point)
                didMove = true
            } else {
                path.line(to: point)
            }
        }

        color.setStroke()
        path.stroke()
    }

    private func drawLineShield(
        _ name: String,
        near coordinate: CLLocationCoordinate2D,
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) {
        let point = project(coordinate, center: center, radiusMiles: radiusMiles, size: size)
        guard point.x > 12, point.x < size.width - 12, point.y > 12, point.y < size.height - 12 else {
            return
        }

        let width: CGFloat = name.count > 2 ? 24 : 20
        let rect = CGRect(x: point.x - width / 2, y: point.y - 8, width: width, height: 16)
        let capsule = NSBezierPath(roundedRect: rect, xRadius: 8, yRadius: 8)
        NSColor(calibratedWhite: 0.015, alpha: 0.78).setFill()
        capsule.fill()
        NSColor(calibratedWhite: 0.66, alpha: 0.26).setStroke()
        capsule.lineWidth = 0.8
        capsule.stroke()

        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 8, weight: .semibold),
            .foregroundColor: NSColor(calibratedWhite: 0.72, alpha: 0.58)
        ]
        let text = NSAttributedString(string: name, attributes: attrs)
        let textSize = text.size()
        text.draw(at: CGPoint(x: point.x - textSize.width / 2, y: point.y - textSize.height / 2 - 0.5))
    }

    private func project(
        _ coordinate: CLLocationCoordinate2D,
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) -> CGPoint {
        let radiusMeters = max(1.0, radiusMiles * Self.metersPerMile)
        let latitudeMeters = (coordinate.latitude - center.latitude) * Self.metersPerDegreeLatitude
        let longitudeMeters = (coordinate.longitude - center.longitude)
            * Self.metersPerDegreeLatitude
            * cos(center.latitude * .pi / 180.0)
        let pixelsPerMeter = min(size.width, size.height) / 2.0 / radiusMeters
        return CGPoint(
            x: size.width / 2.0 + longitudeMeters * pixelsPerMeter,
            y: size.height / 2.0 + latitudeMeters * pixelsPerMeter
        )
    }

    private func snapshot(with options: MKMapSnapshotter.Options) async throws -> MKMapSnapshotter.Snapshot {
        try await withCheckedThrowingContinuation { continuation in
            MKMapSnapshotter(options: options).start { snapshot, error in
                if let snapshot {
                    continuation.resume(returning: snapshot)
                } else {
                    continuation.resume(throwing: error ?? MapRenderError.noSnapshot)
                }
            }
        }
    }

    private func drawRadarPlate(
        snapshot: MKMapSnapshotter.Snapshot,
        center: CLLocationCoordinate2D,
        radiusMiles: Double,
        size: CGSize
    ) -> NSImage {
        let image = NSImage(size: size)
        image.lockFocus()
        defer { image.unlockFocus() }

        NSColor(calibratedWhite: 0.002, alpha: 1).setFill()
        NSBezierPath(rect: CGRect(origin: .zero, size: size)).fill()

        processedSnapshotImage(snapshot.image).draw(
            in: CGRect(origin: .zero, size: size),
            from: CGRect(origin: .zero, size: snapshot.image.size),
            operation: .sourceOver,
            fraction: 1.0
        )

        drawInstrumentTone(size: size)
        drawEnhancedCityLabels(center: center, radiusMiles: radiusMiles, size: size)
        drawCenterReference(snapshot: snapshot, coordinate: center, size: size)

        return image
    }

    private func processedSnapshotImage(_ image: NSImage) -> NSImage {
        guard let tiff = image.tiffRepresentation,
              let input = CIImage(data: tiff)
        else {
            return image
        }

        let sharpen = CIFilter.sharpenLuminance()
        sharpen.inputImage = input
        sharpen.sharpness = 0.46

        guard let output = sharpen.outputImage else {
            return image
        }

        let context = CIContext(options: [.useSoftwareRenderer: false])
        guard let cgImage = context.createCGImage(output, from: output.extent) else {
            return image
        }

        return schematicImage(from: cgImage, size: image.size)
    }

    private func schematicImage(from cgImage: CGImage, size: CGSize) -> NSImage {
        let width = cgImage.width
        let height = cgImage.height
        let bytesPerRow = width * 4
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue
        var source = [UInt8](repeating: 0, count: bytesPerRow * height)

        source.withUnsafeMutableBytes { pixels in
            if let context = CGContext(
                data: pixels.baseAddress,
                width: width,
                height: height,
                bitsPerComponent: 8,
                bytesPerRow: bytesPerRow,
                space: colorSpace,
                bitmapInfo: bitmapInfo
            ) {
                context.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))
            }
        }

        let pixelCount = width * height
        var luminance = [Float](repeating: 0, count: pixelCount)
        for y in 0..<height {
            for x in 0..<width {
                let i = y * bytesPerRow + x * 4
                let r = Double(source[i]) / 255.0
                let g = Double(source[i + 1]) / 255.0
                let b = Double(source[i + 2]) / 255.0
                luminance[y * width + x] = Float(0.2126 * r + 0.7152 * g + 0.0722 * b)
            }
        }

        let localAverage = boxBlur(luminance, width: width, height: height, radius: 9)
        var output = [UInt8](repeating: 255, count: bytesPerRow * height)
        for y in 0..<height {
            for x in 0..<width {
                let p = y * width + x
                let i = y * bytesPerRow + x * 4
                let lum = Double(luminance[p])
                let localLum = Double(localAverage[p])
                let contrast = max(0.0, Double(luminance[p] - localAverage[p]))

                let landTone = smoothstep(edge0: 0.018, edge1: 0.170, value: localLum)
                let mapInk = smoothstep(edge0: 0.070, edge1: 0.430, value: lum)
                let linework = smoothstep(edge0: 0.010, edge1: 0.078, value: contrast)
                let brightText = smoothstep(edge0: 0.430, edge1: 0.780, value: lum)
                let highlight = smoothstep(edge0: 0.066, edge1: 0.145, value: contrast)

                let graphite = 2.0
                    + landTone * 4.0
                    + mapInk * 12.0
                    + linework * 46.0
                    + highlight * 24.0
                    + brightText * 11.0
                let roadInk = linework * 9.0 + highlight * 7.0
                let labelInk = brightText * 7.0
                let gray = byte(graphite + roadInk + labelInk)

                output[i] = gray
                output[i + 1] = gray
                output[i + 2] = gray
                output[i + 3] = 255
            }
        }

        guard let provider = CGDataProvider(data: NSData(bytes: output, length: output.count)),
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
              )
        else {
            return NSImage(cgImage: cgImage, size: size)
        }

        return NSImage(cgImage: schematic, size: size)
    }

    private func keepConnectedFeatures(
        _ alpha: [Float],
        width: Int,
        height: Int,
        threshold: Float,
        minPixels: Int
    ) -> [Float] {
        var kept = [Float](repeating: 0, count: alpha.count)
        var visited = [Bool](repeating: false, count: alpha.count)
        var stack = [Int]()
        var component = [Int]()
        stack.reserveCapacity(512)
        component.reserveCapacity(512)

        for start in 0..<alpha.count {
            guard !visited[start], alpha[start] >= threshold else {
                continue
            }

            stack.removeAll(keepingCapacity: true)
            component.removeAll(keepingCapacity: true)
            visited[start] = true
            stack.append(start)

            while let current = stack.popLast() {
                component.append(current)
                let x = current % width
                let y = current / width

                for ny in max(0, y - 1)...min(height - 1, y + 1) {
                    for nx in max(0, x - 1)...min(width - 1, x + 1) {
                        let next = ny * width + nx
                        if !visited[next], alpha[next] >= threshold {
                            visited[next] = true
                            stack.append(next)
                        }
                    }
                }
            }

            if component.count >= minPixels {
                for index in component {
                    kept[index] = alpha[index]
                }
            }
        }

        return kept
    }

    private func boxBlur(_ source: [Float], width: Int, height: Int, radius: Int) -> [Float] {
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
                horizontal[y * width + x] = (prefix[end + 1] - prefix[start]) / Float(end - start + 1)
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
                blurred[y * width + x] = (prefix[end + 1] - prefix[start]) / Float(end - start + 1)
            }
        }

        return blurred
    }

    private func smoothstep(edge0: Double, edge1: Double, value: Double) -> Double {
        let t = min(1.0, max(0.0, (value - edge0) / (edge1 - edge0)))
        return t * t * (3.0 - 2.0 * t)
    }

    private func byte(_ value: Double) -> UInt8 {
        UInt8(min(255.0, max(0.0, value)).rounded())
    }

    private static let schematicLines: [SchematicLine] = [
        SchematicLine(
            kind: .shoreline,
            name: nil,
            points: [
                CLLocationCoordinate2D(latitude: 37.82, longitude: -122.48),
                CLLocationCoordinate2D(latitude: 37.72, longitude: -122.43),
                CLLocationCoordinate2D(latitude: 37.62, longitude: -122.37),
                CLLocationCoordinate2D(latitude: 37.51, longitude: -122.28),
                CLLocationCoordinate2D(latitude: 37.43, longitude: -122.18),
                CLLocationCoordinate2D(latitude: 37.39, longitude: -122.08),
                CLLocationCoordinate2D(latitude: 37.37, longitude: -122.02)
            ]
        ),
        SchematicLine(
            kind: .shoreline,
            name: nil,
            points: [
                CLLocationCoordinate2D(latitude: 37.88, longitude: -122.32),
                CLLocationCoordinate2D(latitude: 37.80, longitude: -122.28),
                CLLocationCoordinate2D(latitude: 37.70, longitude: -122.20),
                CLLocationCoordinate2D(latitude: 37.61, longitude: -122.13),
                CLLocationCoordinate2D(latitude: 37.52, longitude: -122.06),
                CLLocationCoordinate2D(latitude: 37.44, longitude: -122.00),
                CLLocationCoordinate2D(latitude: 37.35, longitude: -121.95)
            ]
        ),
        SchematicLine(
            kind: .shoreline,
            name: nil,
            points: [
                CLLocationCoordinate2D(latitude: 37.76, longitude: -122.52),
                CLLocationCoordinate2D(latitude: 37.58, longitude: -122.50),
                CLLocationCoordinate2D(latitude: 37.42, longitude: -122.43),
                CLLocationCoordinate2D(latitude: 37.24, longitude: -122.36),
                CLLocationCoordinate2D(latitude: 37.08, longitude: -122.24),
                CLLocationCoordinate2D(latitude: 36.97, longitude: -122.03)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "101",
            points: [
                CLLocationCoordinate2D(latitude: 37.78, longitude: -122.41),
                CLLocationCoordinate2D(latitude: 37.65, longitude: -122.40),
                CLLocationCoordinate2D(latitude: 37.55, longitude: -122.31),
                CLLocationCoordinate2D(latitude: 37.45, longitude: -122.17),
                CLLocationCoordinate2D(latitude: 37.39, longitude: -122.07),
                CLLocationCoordinate2D(latitude: 37.36, longitude: -121.98),
                CLLocationCoordinate2D(latitude: 37.34, longitude: -121.89),
                CLLocationCoordinate2D(latitude: 37.29, longitude: -121.83),
                CLLocationCoordinate2D(latitude: 37.16, longitude: -121.65),
                CLLocationCoordinate2D(latitude: 37.01, longitude: -121.57)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "280",
            points: [
                CLLocationCoordinate2D(latitude: 37.77, longitude: -122.43),
                CLLocationCoordinate2D(latitude: 37.62, longitude: -122.43),
                CLLocationCoordinate2D(latitude: 37.51, longitude: -122.32),
                CLLocationCoordinate2D(latitude: 37.42, longitude: -122.18),
                CLLocationCoordinate2D(latitude: 37.35, longitude: -122.04),
                CLLocationCoordinate2D(latitude: 37.32, longitude: -121.96),
                CLLocationCoordinate2D(latitude: 37.32, longitude: -121.88)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "880",
            points: [
                CLLocationCoordinate2D(latitude: 37.80, longitude: -122.27),
                CLLocationCoordinate2D(latitude: 37.70, longitude: -122.16),
                CLLocationCoordinate2D(latitude: 37.60, longitude: -122.04),
                CLLocationCoordinate2D(latitude: 37.50, longitude: -121.98),
                CLLocationCoordinate2D(latitude: 37.42, longitude: -121.93),
                CLLocationCoordinate2D(latitude: 37.34, longitude: -121.89)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "680",
            points: [
                CLLocationCoordinate2D(latitude: 37.98, longitude: -122.14),
                CLLocationCoordinate2D(latitude: 37.86, longitude: -122.06),
                CLLocationCoordinate2D(latitude: 37.73, longitude: -121.97),
                CLLocationCoordinate2D(latitude: 37.60, longitude: -121.88),
                CLLocationCoordinate2D(latitude: 37.48, longitude: -121.92),
                CLLocationCoordinate2D(latitude: 37.35, longitude: -121.86)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "87",
            points: [
                CLLocationCoordinate2D(latitude: 37.42, longitude: -121.95),
                CLLocationCoordinate2D(latitude: 37.37, longitude: -121.92),
                CLLocationCoordinate2D(latitude: 37.32, longitude: -121.88),
                CLLocationCoordinate2D(latitude: 37.25, longitude: -121.87)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "85",
            points: [
                CLLocationCoordinate2D(latitude: 37.43, longitude: -122.08),
                CLLocationCoordinate2D(latitude: 37.37, longitude: -122.05),
                CLLocationCoordinate2D(latitude: 37.33, longitude: -121.99),
                CLLocationCoordinate2D(latitude: 37.25, longitude: -121.88),
                CLLocationCoordinate2D(latitude: 37.20, longitude: -121.80)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "237",
            points: [
                CLLocationCoordinate2D(latitude: 37.42, longitude: -122.05),
                CLLocationCoordinate2D(latitude: 37.41, longitude: -121.99),
                CLLocationCoordinate2D(latitude: 37.42, longitude: -121.92),
                CLLocationCoordinate2D(latitude: 37.43, longitude: -121.88)
            ]
        ),
        SchematicLine(
            kind: .freeway,
            name: "17",
            points: [
                CLLocationCoordinate2D(latitude: 37.34, longitude: -121.94),
                CLLocationCoordinate2D(latitude: 37.25, longitude: -121.99),
                CLLocationCoordinate2D(latitude: 37.13, longitude: -121.98),
                CLLocationCoordinate2D(latitude: 36.98, longitude: -122.03)
            ]
        ),
        SchematicLine(
            kind: .bridge,
            name: "92",
            points: [
                CLLocationCoordinate2D(latitude: 37.56, longitude: -122.32),
                CLLocationCoordinate2D(latitude: 37.58, longitude: -122.24),
                CLLocationCoordinate2D(latitude: 37.61, longitude: -122.14),
                CLLocationCoordinate2D(latitude: 37.64, longitude: -122.08)
            ]
        ),
        SchematicLine(
            kind: .bridge,
            name: "84",
            points: [
                CLLocationCoordinate2D(latitude: 37.47, longitude: -122.14),
                CLLocationCoordinate2D(latitude: 37.50, longitude: -122.08),
                CLLocationCoordinate2D(latitude: 37.53, longitude: -122.02),
                CLLocationCoordinate2D(latitude: 37.55, longitude: -121.98)
            ]
        )
    ]

    private static let schematicLabels: [SchematicLabel] = [
        SchematicLabel(text: "San Jose", coordinate: CLLocationCoordinate2D(latitude: 37.3382, longitude: -121.8863), minRadiusMiles: 0, maxRadiusMiles: 120, size: 13),
        SchematicLabel(text: "Santa Clara", coordinate: CLLocationCoordinate2D(latitude: 37.3541, longitude: -121.9552), minRadiusMiles: 0, maxRadiusMiles: 28, size: 8),
        SchematicLabel(text: "Cupertino", coordinate: CLLocationCoordinate2D(latitude: 37.3230, longitude: -122.0322), minRadiusMiles: 0, maxRadiusMiles: 24, size: 8),
        SchematicLabel(text: "Milpitas", coordinate: CLLocationCoordinate2D(latitude: 37.4323, longitude: -121.8996), minRadiusMiles: 0, maxRadiusMiles: 28, size: 8),
        SchematicLabel(text: "Sunnyvale", coordinate: CLLocationCoordinate2D(latitude: 37.3688, longitude: -122.0363), minRadiusMiles: 8, maxRadiusMiles: 42, size: 8),
        SchematicLabel(text: "Mountain View", coordinate: CLLocationCoordinate2D(latitude: 37.3861, longitude: -122.0839), minRadiusMiles: 12, maxRadiusMiles: 48, size: 8),
        SchematicLabel(text: "Palo Alto", coordinate: CLLocationCoordinate2D(latitude: 37.4419, longitude: -122.1430), minRadiusMiles: 15, maxRadiusMiles: 58, size: 8),
        SchematicLabel(text: "Fremont", coordinate: CLLocationCoordinate2D(latitude: 37.5485, longitude: -121.9886), minRadiusMiles: 18, maxRadiusMiles: 70, size: 8),
        SchematicLabel(text: "San Francisco", coordinate: CLLocationCoordinate2D(latitude: 37.7749, longitude: -122.4194), minRadiusMiles: 35, maxRadiusMiles: 120, size: 14),
        SchematicLabel(text: "Oakland", coordinate: CLLocationCoordinate2D(latitude: 37.8044, longitude: -122.2712), minRadiusMiles: 42, maxRadiusMiles: 120, size: 11),
        SchematicLabel(text: "Berkeley", coordinate: CLLocationCoordinate2D(latitude: 37.8715, longitude: -122.2730), minRadiusMiles: 45, maxRadiusMiles: 95, size: 8),
        SchematicLabel(text: "Santa Cruz", coordinate: CLLocationCoordinate2D(latitude: 36.9741, longitude: -122.0308), minRadiusMiles: 36, maxRadiusMiles: 120, size: 9),
        SchematicLabel(text: "Livermore", coordinate: CLLocationCoordinate2D(latitude: 37.6819, longitude: -121.7680), minRadiusMiles: 38, maxRadiusMiles: 100, size: 8),
        SchematicLabel(text: "Gilroy", coordinate: CLLocationCoordinate2D(latitude: 37.0058, longitude: -121.5683), minRadiusMiles: 44, maxRadiusMiles: 120, size: 8),
        SchematicLabel(text: "Monterey", coordinate: CLLocationCoordinate2D(latitude: 36.6002, longitude: -121.8947), minRadiusMiles: 80, maxRadiusMiles: 140, size: 9),
        SchematicLabel(text: "Stockton", coordinate: CLLocationCoordinate2D(latitude: 37.9577, longitude: -121.2908), minRadiusMiles: 80, maxRadiusMiles: 140, size: 9)
    ]

    private func drawInstrumentTone(size: CGSize) {
        NSColor(calibratedWhite: 0.0, alpha: 0.32).setFill()
        NSBezierPath(rect: CGRect(origin: .zero, size: size)).fill()

        let bayGlow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.12, alpha: 0.024),
            NSColor(calibratedWhite: 0.035, alpha: 0.020),
            .clear
        ])
        bayGlow?.draw(
            in: NSBezierPath(ovalIn: CGRect(x: -110, y: 35, width: 430, height: 410)),
            relativeCenterPosition: NSPoint(x: -0.18, y: 0.08)
        )

        let landGlow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.10, alpha: 0.018),
            .clear
        ])
        landGlow?.draw(
            in: NSBezierPath(ovalIn: CGRect(x: 180, y: 84, width: 390, height: 330)),
            relativeCenterPosition: NSPoint(x: 0.12, y: -0.04)
        )

        NSColor(calibratedWhite: 0.0, alpha: 0.16).setFill()
        NSBezierPath(rect: CGRect(origin: .zero, size: size)).fill()
    }

    private func drawCenterReference(snapshot: MKMapSnapshotter.Snapshot, coordinate: CLLocationCoordinate2D, size: CGSize) {
        let raw = snapshot.point(for: coordinate)
        let sx = size.width / max(1, snapshot.image.size.width)
        let sy = size.height / max(1, snapshot.image.size.height)
        let point = CGPoint(x: raw.x * sx, y: raw.y * sy)

        drawCenterReference(point: point)
    }

    private func drawCenterReference(point: CGPoint) {

        let glow = NSGradient(colors: [
            NSColor(calibratedWhite: 0.58, alpha: 0.10),
            .clear
        ])
        glow?.draw(
            in: NSBezierPath(ovalIn: CGRect(x: point.x - 24, y: point.y - 24, width: 48, height: 48)),
            relativeCenterPosition: .zero
        )

        NSColor(calibratedWhite: 0.70, alpha: 0.50).setFill()
        NSBezierPath(ovalIn: CGRect(x: point.x - 3, y: point.y - 3, width: 6, height: 6)).fill()
    }

}
