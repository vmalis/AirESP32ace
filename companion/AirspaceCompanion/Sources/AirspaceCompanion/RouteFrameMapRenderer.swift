// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreLocation
import Foundation

struct RouteFrameMapRenderer: FlightMapRendering {
    func renderMap(for flight: TrackedFlight, progress: Double, size: CGSize) async throws -> NSImage {
        let projection = RouteFrameProjection(
            origin: flight.origin.coordinate,
            destination: flight.destination.coordinate,
            size: size
        )
        let landPolygons = NaturalEarthLand.polygons
        let image = NSImage(size: size)
        image.lockFocus()
        defer { image.unlockFocus() }

        drawOcean(size: size)
        drawLandPolygons(landPolygons, projection: projection, size: size)
        drawGraticule(projection: projection, size: size)
        drawCoastlines(
            landPolygons,
            projection: projection,
            size: size
        )

        return image
    }

    private func drawOcean(size: CGSize) {
        let rect = CGRect(origin: .zero, size: size)
        NSColor(calibratedRed: 0.005, green: 0.012, blue: 0.024, alpha: 1).setFill()
        NSBezierPath(rect: rect).fill()

        let glow1 = NSGradient(colors: [
            NSColor(calibratedRed: 0.04, green: 0.16, blue: 0.25, alpha: 0.72),
            NSColor(calibratedRed: 0.02, green: 0.05, blue: 0.09, alpha: 0.18),
            NSColor.clear
        ])
        glow1?.draw(in: NSBezierPath(ovalIn: CGRect(x: -92, y: 70, width: 520, height: 420)), relativeCenterPosition: .zero)

        let glow2 = NSGradient(colors: [
            NSColor(calibratedRed: 0.08, green: 0.30, blue: 0.25, alpha: 0.28),
            NSColor.clear
        ])
        glow2?.draw(in: NSBezierPath(ovalIn: CGRect(x: 170, y: -70, width: 380, height: 310)), relativeCenterPosition: .zero)
    }

    private func drawLandPolygons(_ polygons: [[[GeoCoordinate]]], projection: RouteFrameProjection, size: CGSize) {
        let fill = NSBezierPath()
        fill.windingRule = .evenOdd

        for polygon in polygons {
            for ring in polygon where ring.count >= 3 {
                let projected = projection.projectAndClipRing(
                    ring,
                    to: CGRect(origin: .zero, size: size)
                        .insetBy(dx: -2, dy: -2)
                )
                guard projected.count >= 3 else { continue }
                fill.move(to: projected[0])
                for point in projected.dropFirst() {
                    fill.line(to: point)
                }
                fill.close()
            }
        }

        NSColor(calibratedRed: 0.035, green: 0.19, blue: 0.16, alpha: 0.86).setFill()
        fill.fill()
    }

    private func drawGraticule(projection: RouteFrameProjection, size: CGSize) {
        let lines = NSBezierPath()
        let step = projection.graticuleStepDegrees

        for latitude in stride(from: -80.0, through: 80.0, by: step) {
            appendProjectedLine(
                to: lines,
                coordinates: stride(from: -180.0, through: 180.0, by: max(0.5, step / 5)).map {
                    GeoCoordinate(latitude: latitude, longitude: $0)
                },
                projection: projection,
                size: size,
                maxJump: 82
            )
        }

        for longitude in stride(from: -180.0, through: 180.0, by: step) {
            appendProjectedLine(
                to: lines,
                coordinates: stride(from: -82.0, through: 82.0, by: max(0.5, step / 5)).map {
                    GeoCoordinate(latitude: $0, longitude: longitude)
                },
                projection: projection,
                size: size,
                maxJump: 82
            )
        }

        NSColor(calibratedRed: 0.44, green: 0.70, blue: 0.82, alpha: 0.16).setStroke()
        lines.lineWidth = 0.8
        lines.stroke()
    }

    private func drawCoastlines(
        _ polygons: [[[GeoCoordinate]]],
        projection: RouteFrameProjection,
        size: CGSize
    ) {
        let landGlow = NSBezierPath()
        let coast = NSBezierPath()

        for polygon in polygons {
            for ring in polygon where ring.count >= 2 {
                // Use the same Natural Earth rings as the land fill. Tight
                // clipping prevents projection seams from masquerading as a
                // second route and keeps only adjacent coastline vertices.
                appendProjectedLine(
                    to: landGlow,
                    coordinates: ring,
                    projection: projection,
                    size: size,
                    maxJump: 132
                )
                appendProjectedLine(
                    to: coast,
                    coordinates: ring,
                    projection: projection,
                    size: size,
                    maxJump: 132
                )
            }
        }

        NSColor(calibratedRed: 0.07, green: 0.26, blue: 0.22, alpha: 0.30).setStroke()
        landGlow.lineWidth = 9
        landGlow.lineCapStyle = .round
        landGlow.lineJoinStyle = .round
        landGlow.stroke()

        NSColor(calibratedRed: 0.30, green: 0.62, blue: 0.58, alpha: 0.60).setStroke()
        coast.lineWidth = 1.25
        coast.lineCapStyle = .round
        coast.lineJoinStyle = .round
        coast.stroke()
    }

    private func appendProjectedLine(
        to path: NSBezierPath,
        coordinates: [GeoCoordinate],
        projection: RouteFrameProjection,
        size: CGSize,
        maxJump: CGFloat
    ) {
        var hasRun = false
        var previous: CGPoint?

        for coordinate in coordinates {
            guard let point = projection.project(coordinate),
                  point.x >= -36, point.x <= size.width + 36,
                  point.y >= -36, point.y <= size.height + 36
            else {
                hasRun = false
                previous = nil
                continue
            }

            if let previous, hypot(point.x - previous.x, point.y - previous.y) > maxJump {
                hasRun = false
            }

            if hasRun {
                path.line(to: point)
            } else {
                path.move(to: point)
                hasRun = true
            }
            previous = point
        }
    }

    private func drawRoute(_ route: [CLLocationCoordinate2D], projection: RouteFrameProjection) {
        let path = NSBezierPath()
        appendProjectedLine(
            to: path,
            coordinates: route.map { GeoCoordinate(latitude: $0.latitude, longitude: $0.longitude) },
            projection: projection,
            size: projection.size,
            maxJump: 80
        )

        NSColor(calibratedRed: 0.15, green: 0.88, blue: 1.0, alpha: 0.38).setStroke()
        path.lineWidth = 13
        path.lineCapStyle = .round
        path.lineJoinStyle = .round
        path.stroke()

        NSColor(calibratedRed: 0.75, green: 0.99, blue: 1.0, alpha: 0.96).setStroke()
        path.lineWidth = 3.6
        path.stroke()
    }

    private func drawAirplane(route: [CLLocationCoordinate2D], progress: Double, projection: RouteFrameProjection) {
        let clamped = min(1.0, max(0.0, progress))
        let index = min(route.count - 1, max(0, Int(round(clamped * Double(route.count - 1)))))
        let point = projection.project(GeoCoordinate(latitude: route[index].latitude, longitude: route[index].longitude))
            ?? CGPoint(
                x: projection.originPoint.x,
                y: projection.originPoint.y + (projection.destinationPoint.y - projection.originPoint.y) * clamped
            )

        let glow = NSBezierPath(ovalIn: CGRect(x: point.x - 28, y: point.y - 28, width: 56, height: 56))
        NSColor(calibratedRed: 0.42, green: 0.96, blue: 1.0, alpha: 0.23).setFill()
        glow.fill()

        drawAircraftGlyph(at: point, angle: .pi / 2)
    }

    private func drawAircraftGlyph(at point: CGPoint, angle: CGFloat) {
        let transform = NSAffineTransform()
        transform.translateX(by: point.x, yBy: point.y)
        transform.rotate(byRadians: angle)

        let silhouette = NSBezierPath()
        silhouette.move(to: NSPoint(x: 29, y: 0))
        silhouette.line(to: NSPoint(x: 8, y: 5))
        silhouette.line(to: NSPoint(x: -4, y: 26))
        silhouette.curve(
            to: NSPoint(x: -10, y: 22),
            controlPoint1: NSPoint(x: -7, y: 27),
            controlPoint2: NSPoint(x: -10, y: 26)
        )
        silhouette.line(to: NSPoint(x: -6, y: 6))
        silhouette.line(to: NSPoint(x: -18, y: 4))
        silhouette.line(to: NSPoint(x: -24, y: 13))
        silhouette.curve(
            to: NSPoint(x: -30, y: 9),
            controlPoint1: NSPoint(x: -27, y: 14),
            controlPoint2: NSPoint(x: -30, y: 13)
        )
        silhouette.line(to: NSPoint(x: -24, y: 0))
        silhouette.line(to: NSPoint(x: -30, y: -9))
        silhouette.curve(
            to: NSPoint(x: -24, y: -13),
            controlPoint1: NSPoint(x: -30, y: -13),
            controlPoint2: NSPoint(x: -27, y: -14)
        )
        silhouette.line(to: NSPoint(x: -18, y: -4))
        silhouette.line(to: NSPoint(x: -6, y: -6))
        silhouette.line(to: NSPoint(x: -10, y: -22))
        silhouette.curve(
            to: NSPoint(x: -4, y: -26),
            controlPoint1: NSPoint(x: -10, y: -26),
            controlPoint2: NSPoint(x: -7, y: -27)
        )
        silhouette.line(to: NSPoint(x: 8, y: -5))
        silhouette.close()

        let transformedSilhouette = transform.transform(silhouette)

        NSGraphicsContext.saveGraphicsState()
        let glow = NSShadow()
        glow.shadowBlurRadius = 18
        glow.shadowOffset = .zero
        glow.shadowColor = NSColor(calibratedRed: 0.40, green: 0.95, blue: 1.0, alpha: 0.72)
        glow.set()
        NSColor.white.setFill()
        transformedSilhouette.fill()
        NSGraphicsContext.restoreGraphicsState()

        NSColor(calibratedRed: 0.82, green: 1.0, blue: 1.0, alpha: 0.98).setFill()
        transformedSilhouette.fill()

        NSColor(calibratedRed: 0.16, green: 0.64, blue: 0.78, alpha: 0.86).setStroke()
        transformedSilhouette.lineWidth = 1.4
        transformedSilhouette.lineJoinStyle = .round
        transformedSilhouette.stroke()

        let spine = NSBezierPath()
        spine.move(to: NSPoint(x: -20, y: 0))
        spine.line(to: NSPoint(x: 22, y: 0))
        let transformedSpine = transform.transform(spine)
        NSColor(calibratedRed: 0.05, green: 0.32, blue: 0.42, alpha: 0.32).setStroke()
        transformedSpine.lineWidth = 1.1
        transformedSpine.lineCapStyle = .round
        transformedSpine.stroke()
    }

    private func drawAirportMarker(at point: CGPoint, color: NSColor) {
        color.withAlphaComponent(0.96).setFill()
        NSBezierPath(ovalIn: CGRect(x: point.x - 6, y: point.y - 6, width: 12, height: 12)).fill()
    }

    private func drawCaption(flight: TrackedFlight, progress: Double, size: CGSize) {
        let card = CGRect(x: size.width - 178, y: size.height * 0.45, width: 132, height: 58)
        NSColor(calibratedRed: 0.02, green: 0.05, blue: 0.08, alpha: 0.72).setFill()
        NSBezierPath(roundedRect: card, xRadius: 24, yRadius: 24).fill()

        (flight.marketingNumber as NSString).draw(
            at: CGPoint(x: card.minX + 20, y: card.minY + 30),
            withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 18, weight: .bold),
                .foregroundColor: NSColor.white
            ]
        )

        ("\(flight.operationalCallsign) · \(Int(round(progress * 100)))%" as NSString).draw(
            at: CGPoint(x: card.minX + 20, y: card.minY + 13),
            withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .medium),
                .foregroundColor: NSColor(calibratedRed: 0.54, green: 0.88, blue: 1.0, alpha: 0.9)
            ]
        )
    }
}

private struct RouteFrameProjection {
    let size: CGSize
    let originPoint: CGPoint
    let destinationPoint: CGPoint

    private let originVector: Vector3
    private let alongVector: Vector3
    private let normalVector: Vector3
    private let totalAngle: Double
    private let crossTrackHalfAngle: Double

    var graticuleStepDegrees: Double {
        let span = totalAngle.radiansToDegrees
        if span < 12 { return 2 }
        if span < 32 { return 5 }
        if span < 70 { return 10 }
        return 15
    }

    init(origin: CLLocationCoordinate2D, destination: CLLocationCoordinate2D, size: CGSize) {
        self.size = size
        self.originPoint = CGPoint(x: size.width * 0.50, y: size.height * 0.155)
        self.destinationPoint = CGPoint(x: size.width * 0.50, y: size.height * 0.845)

        let originVector = Vector3(coordinate: origin)
        let destinationVector = Vector3(coordinate: destination)
        let normalVector = originVector.cross(destinationVector).normalized
        let alongVector = normalVector.cross(originVector).normalized

        self.originVector = originVector
        self.alongVector = alongVector
        self.normalVector = normalVector
        let totalAngle = max(
            0.001,
            atan2(
                destinationVector.dot(alongVector),
                destinationVector.dot(originVector)
            )
        )
        self.totalAngle = totalAngle
        // The old fixed 80-degree-wide corridor was suitable for a Pacific
        // crossing but reduced short routes to an almost empty ocean layer.
        // Keep the frame close to square in angular terms, with practical
        // limits for both regional and intercontinental flights.
        self.crossTrackHalfAngle = min(
            40.0.degreesToRadians,
            max(4.0.degreesToRadians, totalAngle * 0.62)
        )
    }

    func project(_ coordinate: GeoCoordinate) -> CGPoint? {
        let components = projectionComponents(coordinate)
        let along = components.along

        guard along > -totalAngle * 0.20, along < totalAngle * 1.20 else {
            return nil
        }

        return screenPoint(
            along: along,
            crossTrack: components.crossTrack
        )
    }

    func projectAndClipRing(
        _ coordinates: [GeoCoordinate],
        to rect: CGRect
    ) -> [CGPoint] {
        guard coordinates.count >= 3 else { return [] }

        var samples: [(along: Double, crossTrack: Double)] = []
        samples.reserveCapacity(coordinates.count)
        var previousAlong: Double?
        for coordinate in coordinates {
            var component = projectionComponents(coordinate)
            if let previousAlong {
                while component.along - previousAlong > .pi {
                    component.along -= 2 * .pi
                }
                while component.along - previousAlong < -.pi {
                    component.along += 2 * .pi
                }
            }
            samples.append(component)
            previousAlong = component.along
        }

        // Shift an antimeridian-unwrapped polygon to the copy closest to the
        // route corridor. This keeps Alaska/Russia and other seam-crossing
        // land masses continuous without drawing a false chord over the map.
        let meanAlong = samples.reduce(0) { $0 + $1.along } /
            Double(samples.count)
        let corridorCenter = totalAngle * 0.5
        let wrap = round(
            (corridorCenter - meanAlong) / (2 * .pi)
        ) * (2 * .pi)
        let points = samples.map {
            screenPoint(
                along: $0.along + wrap,
                crossTrack: $0.crossTrack
            )
        }
        return Self.clipPolygon(points, to: rect)
    }

    private func projectionComponents(
        _ coordinate: GeoCoordinate
    ) -> (along: Double, crossTrack: Double) {
        let point = Vector3(
            latitude: coordinate.latitude,
            longitude: coordinate.longitude
        )
        return (
            atan2(
                point.dot(alongVector),
                point.dot(originVector)
            ),
            asin(
                max(-1.0, min(1.0, point.dot(normalVector)))
            )
        )
    }

    private func screenPoint(
        along: Double,
        crossTrack: Double
    ) -> CGPoint {
        let yProgress = along / totalAngle
        return CGPoint(
            x: size.width * 0.50 -
                (crossTrack / crossTrackHalfAngle) *
                size.width * 0.43,
            y: originPoint.y +
                (destinationPoint.y - originPoint.y) *
                yProgress
        )
    }

    private static func clipPolygon(
        _ polygon: [CGPoint],
        to rect: CGRect
    ) -> [CGPoint] {
        enum Edge {
            case left, right, bottom, top
        }
        func inside(_ point: CGPoint, edge: Edge) -> Bool {
            switch edge {
            case .left: return point.x >= rect.minX
            case .right: return point.x <= rect.maxX
            case .bottom: return point.y >= rect.minY
            case .top: return point.y <= rect.maxY
            }
        }
        func intersection(
            _ start: CGPoint,
            _ end: CGPoint,
            edge: Edge
        ) -> CGPoint {
            let dx = end.x - start.x
            let dy = end.y - start.y
            switch edge {
            case .left, .right:
                let x = edge == .left ? rect.minX : rect.maxX
                let t = abs(dx) < 0.000_001 ? 0 : (x - start.x) / dx
                return CGPoint(x: x, y: start.y + t * dy)
            case .bottom, .top:
                let y = edge == .bottom ? rect.minY : rect.maxY
                let t = abs(dy) < 0.000_001 ? 0 : (y - start.y) / dy
                return CGPoint(x: start.x + t * dx, y: y)
            }
        }

        var output = polygon
        for edge in [Edge.left, .right, .bottom, .top] {
            let input = output
            output.removeAll(keepingCapacity: true)
            guard var start = input.last else { break }
            for end in input {
                let startInside = inside(start, edge: edge)
                let endInside = inside(end, edge: edge)
                if endInside {
                    if !startInside {
                        output.append(
                            intersection(start, end, edge: edge)
                        )
                    }
                    output.append(end)
                } else if startInside {
                    output.append(
                        intersection(start, end, edge: edge)
                    )
                }
                start = end
            }
        }
        return output
    }
}

enum NaturalEarthLand {
    static let polygons: [[[GeoCoordinate]]] = load()

    private static func load() -> [[[GeoCoordinate]]] {
        for url in candidateURLs() {
            guard let data = try? Data(contentsOf: url),
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let features = object["features"] as? [[String: Any]]
            else {
                continue
            }

            var result: [[[GeoCoordinate]]] = []
            result.reserveCapacity(features.count)

            for feature in features {
                guard let geometry = feature["geometry"] as? [String: Any],
                      let type = geometry["type"] as? String
                else {
                    continue
                }

                if type == "Polygon" {
                    let polygon = parsePolygon(geometry["coordinates"] as Any)
                    if !polygon.isEmpty {
                        result.append(polygon)
                    }
                } else if type == "MultiPolygon",
                          let rawPolygons = geometry["coordinates"] as? [Any] {
                    for rawPolygon in rawPolygons {
                        let polygon = parsePolygon(rawPolygon)
                        if !polygon.isEmpty {
                            result.append(polygon)
                        }
                    }
                }
            }

            if !result.isEmpty {
                return result
            }
        }

        return fallbackPacificLand
    }

    private static func parsePolygon(_ raw: Any) -> [[GeoCoordinate]] {
        guard let rawRings = raw as? [Any] else { return [] }
        return rawRings.compactMap { rawRing in
            guard let rawPositions = rawRing as? [Any] else { return nil }
            let coordinates = rawPositions.compactMap { rawPosition -> GeoCoordinate? in
                guard let rawPair = rawPosition as? [Any],
                      rawPair.count >= 2,
                      let longitude = number(rawPair[0]),
                      let latitude = number(rawPair[1])
                else {
                    return nil
                }
                return GeoCoordinate(latitude: latitude, longitude: longitude)
            }
            return coordinates.count >= 3 ? coordinates : nil
        }
    }

    private static func number(_ value: Any) -> Double? {
        if let double = value as? Double {
            return double
        }
        if let number = value as? NSNumber {
            return number.doubleValue
        }
        return nil
    }

    private static func candidateURLs() -> [URL] {
        var urls: [URL] = []
        urls.append(contentsOf: [
            Bundle.module.url(forResource: "ne_50m_land", withExtension: "geojson"),
            Bundle.module.url(forResource: "ne_110m_land", withExtension: "geojson")
        ].compactMap { $0 })

        let current = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        #if DEBUG
        let sourceDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        urls.append(contentsOf: [
            sourceDirectory.appendingPathComponent("Resources/ne_50m_land.geojson"),
            sourceDirectory.appendingPathComponent("Resources/ne_110m_land.geojson")
        ])
        #endif
        urls.append(contentsOf: [
            current.appendingPathComponent("Sources/AirspaceCompanion/Resources/ne_50m_land.geojson"),
            current.appendingPathComponent("Sources/AirspaceCompanion/Resources/ne_110m_land.geojson"),
            current.appendingPathComponent("companion/AirspaceCompanion/Sources/AirspaceCompanion/Resources/ne_50m_land.geojson"),
            current.appendingPathComponent("companion/AirspaceCompanion/Sources/AirspaceCompanion/Resources/ne_110m_land.geojson")
        ])
        return urls
    }

    private static let fallbackPacificLand: [[[GeoCoordinate]]] = [
        [[
            GeoCoordinate(latitude: 28, longitude: 120),
            GeoCoordinate(latitude: 35, longitude: 126),
            GeoCoordinate(latitude: 39, longitude: 141),
            GeoCoordinate(latitude: 46, longitude: 150),
            GeoCoordinate(latitude: 56, longitude: 160),
            GeoCoordinate(latitude: 61, longitude: 176),
            GeoCoordinate(latitude: 52, longitude: 178),
            GeoCoordinate(latitude: 41, longitude: 154),
            GeoCoordinate(latitude: 35, longitude: 138),
            GeoCoordinate(latitude: 28, longitude: 120)
        ]],
        [[
            GeoCoordinate(latitude: 52, longitude: -176),
            GeoCoordinate(latitude: 60, longitude: -165),
            GeoCoordinate(latitude: 61, longitude: -148),
            GeoCoordinate(latitude: 54, longitude: -136),
            GeoCoordinate(latitude: 48, longitude: -126),
            GeoCoordinate(latitude: 36, longitude: -121),
            GeoCoordinate(latitude: 31, longitude: -116),
            GeoCoordinate(latitude: 41, longitude: -132),
            GeoCoordinate(latitude: 48, longitude: -150),
            GeoCoordinate(latitude: 52, longitude: -176)
        ]]
    ]
}

struct GeoCoordinate: Sendable {
    let latitude: Double
    let longitude: Double
}

private struct Vector3: Sendable {
    let x: Double
    let y: Double
    let z: Double

    init(x: Double, y: Double, z: Double) {
        self.x = x
        self.y = y
        self.z = z
    }

    init(coordinate: CLLocationCoordinate2D) {
        self.init(latitude: coordinate.latitude, longitude: coordinate.longitude)
    }

    init(latitude: Double, longitude: Double) {
        let lat = latitude.degreesToRadians
        let lon = longitude.degreesToRadians
        self.x = cos(lat) * cos(lon)
        self.y = cos(lat) * sin(lon)
        self.z = sin(lat)
    }

    var normalized: Vector3 {
        let length = max(0.0000001, sqrt(dot(self)))
        return Vector3(x: x / length, y: y / length, z: z / length)
    }

    func dot(_ other: Vector3) -> Double {
        x * other.x + y * other.y + z * other.z
    }

    func cross(_ other: Vector3) -> Vector3 {
        Vector3(
            x: y * other.z - z * other.y,
            y: z * other.x - x * other.z,
            z: x * other.y - y * other.x
        )
    }
}

private extension Double {
    var degreesToRadians: Double { self * .pi / 180.0 }
    var radiansToDegrees: Double { self * 180.0 / .pi }
}

private extension NSBezierPath {
    func strokeAircraft(width: CGFloat, color: NSColor, alpha: CGFloat) {
        color.withAlphaComponent(alpha).setStroke()
        lineWidth = width
        lineCapStyle = .round
        lineJoinStyle = .round
        stroke()
    }
}
