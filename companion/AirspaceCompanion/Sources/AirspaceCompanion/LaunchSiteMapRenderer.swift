// AirESP32ace
// Created by VM, August 2026

import AppKit
import CoreLocation
import Foundation
import MapKit

struct LaunchSiteMapRenderer: Sendable {
    func renderMap(
        site: LaunchSiteKind,
        size: CGSize = CGSize(width: 466, height: 466)
    ) async throws -> NSImage {
        let scale: CGFloat = 2
        let options = MKMapSnapshotter.Options()
        options.size = CGSize(
            width: size.width * scale,
            height: size.height * scale
        )
        options.region = MKCoordinateRegion(
            center: site.coordinate,
            latitudinalMeters:
                site.mapRadiusKilometers * 2_000,
            longitudinalMeters:
                site.mapRadiusKilometers * 2_000
        )
        let configuration = MKStandardMapConfiguration(
            elevationStyle: .flat,
            emphasisStyle: .muted
        )
        configuration.pointOfInterestFilter = .excludingAll
        configuration.showsTraffic = false
        options.preferredConfiguration = configuration
        options.appearance = NSAppearance(named: .darkAqua)

        let snapshot = try await snapshot(options)
        return compose(
            GraphiteMapProcessor.process(snapshot.image),
            size: size
        )
    }

    private func snapshot(
        _ options: MKMapSnapshotter.Options
    ) async throws -> MKMapSnapshotter.Snapshot {
        try await withCheckedThrowingContinuation {
            continuation in
            MKMapSnapshotter(options: options).start {
                snapshot,
                error in
                if let snapshot {
                    continuation.resume(
                        returning: snapshot
                    )
                } else {
                    continuation.resume(
                        throwing:
                            error ??
                            LaunchSiteMapError.noSnapshot
                    )
                }
            }
        }
    }

    private func compose(
        _ source: NSImage,
        size: CGSize
    ) -> NSImage {
        let image = NSImage(size: size)
        image.lockFocus()
        defer { image.unlockFocus() }

        NSColor.black.setFill()
        NSRect(origin: .zero, size: size).fill()
        source.draw(
            in: NSRect(origin: .zero, size: size),
            from: NSRect(
                origin: .zero,
                size: source.size
            ),
            operation: .sourceOver,
            fraction: 1
        )
        GraphiteMapProcessor.drawRadarInstrumentTone(
            size: size
        )

        let vignette = NSGradient(colors: [
            .clear,
            NSColor.black.withAlphaComponent(0.08),
            NSColor.black.withAlphaComponent(0.42),
        ])
        vignette?.draw(
            in: NSBezierPath(
                ovalIn: NSRect(
                    x: -16,
                    y: -16,
                    width: size.width + 32,
                    height: size.height + 32
                )
            ),
            relativeCenterPosition: .zero
        )
        return image
    }
}

enum LaunchSiteMapError: LocalizedError {
    case noSnapshot

    var errorDescription: String? {
        "MapKit did not return a launch-site map."
    }
}
