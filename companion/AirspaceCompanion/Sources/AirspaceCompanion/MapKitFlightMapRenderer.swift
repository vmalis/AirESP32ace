// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreLocation
import Foundation
@preconcurrency import MapKit

extension MKMapSnapshotter.Snapshot: @unchecked @retroactive Sendable {}

protocol FlightMapRendering: Sendable {
    func renderMap(
        for flight: TrackedFlight,
        progress: Double,
        size: CGSize
    ) async throws -> NSImage
}

struct MapKitFlightMapRenderer: FlightMapRendering {
    static let deviceRoutePointCount = 24

    func renderMap(
        for flight: TrackedFlight,
        progress: Double,
        size: CGSize
    ) async throws -> NSImage {
        _ = progress
        let route = Self.routeCoordinates(for: flight)
        let projection = NorthUpRouteProjection(
            route: route,
            size: size
        )

        let snapshotScale: CGFloat = 2.35
        let options = MKMapSnapshotter.Options()
        options.size = CGSize(
            width: (size.width * snapshotScale).rounded(),
            height: (size.height * snapshotScale).rounded()
        )
        options.mapRect = projection.mapRect
        let configuration = MKStandardMapConfiguration(
            elevationStyle: .flat,
            emphasisStyle: .muted
        )
        configuration.pointOfInterestFilter = .excludingAll
        configuration.showsTraffic = false
        options.preferredConfiguration = configuration
        options.appearance = NSAppearance(named: .darkAqua)

        let snapshot = try await snapshot(with: options)
        return drawNorthUpMap(snapshot: snapshot, size: size)
    }

    static func routeScreenPoints(
        for flight: TrackedFlight,
        size: CGSize = CGSize(width: 466, height: 466)
    ) -> [CGPoint] {
        let route = routeCoordinates(for: flight)
        return NorthUpRouteProjection(
            route: route,
            size: size
        ).screenPoints(
            for: route,
            count: deviceRoutePointCount
        )
    }

    private static func routeCoordinates(
        for flight: TrackedFlight
    ) -> [CLLocationCoordinate2D] {
        GreatCircle.points(
            from: flight.origin.coordinate,
            to: flight.destination.coordinate,
            count: 180
        )
    }

    private func snapshot(
        with options: MKMapSnapshotter.Options
    ) async throws -> MKMapSnapshotter.Snapshot {
        let snapshotter = MKMapSnapshotter(options: options)
        return try await withCheckedThrowingContinuation {
            continuation in
            let race = MapSnapshotRace(continuation)
            snapshotter.start { snapshot, error in
                if let snapshot {
                    race.resolve(.success(snapshot))
                } else {
                    race.resolve(
                        .failure(
                            error ?? MapRenderError.noSnapshot
                        )
                    )
                }
            }
            DispatchQueue.global(qos: .userInitiated)
                .asyncAfter(deadline: .now() + 20) {
                    race.resolve(
                        .failure(MapRenderError.timedOut)
                    )
                }
        }
    }

    private func drawNorthUpMap(
        snapshot: MKMapSnapshotter.Snapshot,
        size: CGSize
    ) -> NSImage {
        let image = NSImage(size: size)
        image.lockFocus()
        defer { image.unlockFocus() }

        NSColor.black.setFill()
        NSBezierPath(
            rect: CGRect(origin: .zero, size: size)
        ).fill()

        let processedMap = GraphiteMapProcessor.process(
            snapshot.image
        )
        drawAspectFill(
            processedMap,
            in: CGRect(origin: .zero, size: size)
        )
        GraphiteMapProcessor.drawRadarInstrumentTone(size: size)
        return image
    }

    private func drawAspectFill(
        _ image: NSImage,
        in destination: CGRect
    ) {
        let sourceSize = image.size
        guard sourceSize.width > 0, sourceSize.height > 0 else {
            return
        }
        let scale = max(
            destination.width / sourceSize.width,
            destination.height / sourceSize.height
        )
        let drawSize = CGSize(
            width: sourceSize.width * scale,
            height: sourceSize.height * scale
        )
        image.draw(
            in: CGRect(
                x: destination.midX - drawSize.width / 2,
                y: destination.midY - drawSize.height / 2,
                width: drawSize.width,
                height: drawSize.height
            ),
            from: CGRect(origin: .zero, size: sourceSize),
            operation: .sourceOver,
            fraction: 1
        )
    }
}

struct NorthUpRouteProjection {
    let mapRect: MKMapRect
    let size: CGSize

    private let worldWidth = MKMapSize.world.width

    init(
        route: [CLLocationCoordinate2D],
        size: CGSize
    ) {
        self.size = size
        let points = Self.unwrappedMapPoints(route)
        let xs = points.map(\.x)
        let ys = points.map(\.y)
        let minX = xs.min() ?? 0
        let maxX = xs.max() ?? MKMapSize.world.width
        let minY = ys.min() ?? 0
        let maxY = ys.max() ?? MKMapSize.world.height
        let width = maxX - minX
        let height = maxY - minY
        let world = MKMapSize.world.width
        let side = min(
            world * 0.84,
            max(
                world * 0.035,
                max(width, height) * 1.34
            )
        )
        let centerX = (minX + maxX) / 2
        let rawCenterY = (minY + maxY) / 2
        let centerY = min(
            world - side / 2,
            max(side / 2, rawCenterY)
        )
        mapRect = MKMapRect(
            x: centerX - side / 2,
            y: centerY - side / 2,
            width: side,
            height: side
        )
    }

    func screenPoints(
        for route: [CLLocationCoordinate2D],
        count: Int
    ) -> [CGPoint] {
        let mapPoints = Self.unwrappedMapPoints(route)
        guard !mapPoints.isEmpty else { return [] }
        let sampleCount = max(2, count)
        return (0..<sampleCount).map { index in
            let fraction = Double(index) /
                Double(sampleCount - 1)
            let sourceIndex = min(
                mapPoints.count - 1,
                Int(
                    round(
                        fraction *
                            Double(mapPoints.count - 1)
                    )
                )
            )
            return screenPoint(for: mapPoints[sourceIndex])
        }
    }

    func screenPoint(
        for coordinate: CLLocationCoordinate2D
    ) -> CGPoint {
        var point = MKMapPoint(coordinate)
        point.x +=
            round((mapRect.midX - point.x) / worldWidth) *
            worldWidth
        return screenPoint(for: point)
    }

    private func screenPoint(for point: MKMapPoint) -> CGPoint {
        CGPoint(
            x:
                (point.x - mapRect.minX) /
                mapRect.width * size.width,
            y:
                (point.y - mapRect.minY) /
                mapRect.height * size.height
        )
    }

    private static func unwrappedMapPoints(
        _ route: [CLLocationCoordinate2D]
    ) -> [MKMapPoint] {
        let world = MKMapSize.world.width
        var previousX: Double?
        return route.map { coordinate in
            var point = MKMapPoint(coordinate)
            if let previousX {
                while point.x - previousX > world / 2 {
                    point.x -= world
                }
                while point.x - previousX < -world / 2 {
                    point.x += world
                }
            }
            previousX = point.x
            return point
        }
    }
}

private final class MapSnapshotRace: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation:
        CheckedContinuation<MKMapSnapshotter.Snapshot, Error>?

    init(
        _ continuation:
            CheckedContinuation<MKMapSnapshotter.Snapshot, Error>
    ) {
        self.continuation = continuation
    }

    func resolve(
        _ result: Result<MKMapSnapshotter.Snapshot, Error>
    ) {
        lock.lock()
        guard let continuation else {
            lock.unlock()
            return
        }
        self.continuation = nil
        lock.unlock()
        continuation.resume(with: result)
    }
}

enum MapRenderError: LocalizedError {
    case noSnapshot
    case timedOut

    var errorDescription: String? {
        switch self {
        case .noSnapshot:
            "MapKit did not return a snapshot."
        case .timedOut:
            "MapKit route map timed out after 20 seconds."
        }
    }
}
