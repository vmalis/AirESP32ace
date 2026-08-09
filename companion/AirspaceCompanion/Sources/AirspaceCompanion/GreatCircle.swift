// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation
import MapKit

enum GreatCircle {
    static func points(
        from start: CLLocationCoordinate2D,
        to end: CLLocationCoordinate2D,
        count: Int = 96
    ) -> [CLLocationCoordinate2D] {
        let startLat = start.latitude.degreesToRadians
        let startLon = start.longitude.degreesToRadians
        let endLat = end.latitude.degreesToRadians
        let endLon = end.longitude.degreesToRadians

        let delta = 2 * asin(
            sqrt(
                pow(sin((endLat - startLat) / 2), 2)
                + cos(startLat) * cos(endLat) * pow(sin((endLon - startLon) / 2), 2)
            )
        )

        guard delta > 0 else { return [start, end] }

        return (0..<max(count, 2)).map { index in
            let fraction = Double(index) / Double(max(count - 1, 1))
            let a = sin((1 - fraction) * delta) / sin(delta)
            let b = sin(fraction * delta) / sin(delta)

            let x = a * cos(startLat) * cos(startLon) + b * cos(endLat) * cos(endLon)
            let y = a * cos(startLat) * sin(startLon) + b * cos(endLat) * sin(endLon)
            let z = a * sin(startLat) + b * sin(endLat)

            let lat = atan2(z, sqrt(x * x + y * y)).radiansToDegrees
            let lon = atan2(y, x).radiansToDegrees.normalizedLongitude
            return CLLocationCoordinate2D(latitude: lat, longitude: lon)
        }
    }

    static func pacificRegion(for points: [CLLocationCoordinate2D]) -> MKCoordinateRegion {
        guard !points.isEmpty else {
            return MKCoordinateRegion(
                center: CLLocationCoordinate2D(latitude: 52, longitude: -178),
                span: MKCoordinateSpan(latitudeDelta: 58, longitudeDelta: 145)
            )
        }

        let lats = points.map(\.latitude)
        let normalizedLons = points.map { longitudeForPacificMap($0.longitude) }
        let minLat = lats.min() ?? 25
        let maxLat = lats.max() ?? 65
        let minLon = normalizedLons.min() ?? 120
        let maxLon = normalizedLons.max() ?? 235

        let centerLat = (minLat + maxLat) / 2
        let centerLon = ((minLon + maxLon) / 2).normalizedLongitude
        let latDelta = max(28, (maxLat - minLat) * 1.55)
        let lonDelta = max(92, (maxLon - minLon) * 1.34)

        return MKCoordinateRegion(
            center: CLLocationCoordinate2D(latitude: centerLat, longitude: centerLon),
            span: MKCoordinateSpan(latitudeDelta: latDelta, longitudeDelta: lonDelta)
        )
    }

    static func longitudeForPacificMap(_ longitude: Double) -> Double {
        longitude < 0 ? longitude + 360 : longitude
    }

    static func nearestProgress(
        of position: CLLocationCoordinate2D,
        from start: CLLocationCoordinate2D,
        to end: CLLocationCoordinate2D
    ) -> Double {
        let totalAngle = angularDistance(from: start, to: end)
        guard totalAngle > 1e-9 else { return 0 }

        let positionAngle = angularDistance(
            from: start,
            to: position
        )
        let routeBearing = initialBearing(
            from: start,
            to: end
        )
        let positionBearing = initialBearing(
            from: start,
            to: position
        )
        let alongTrack = atan2(
            sin(positionAngle) *
                cos(positionBearing - routeBearing),
            cos(positionAngle)
        )
        guard alongTrack.isFinite else { return 0 }
        return min(1, max(0, alongTrack / totalAngle))
    }

    private static func angularDistance(
        from lhs: CLLocationCoordinate2D,
        to rhs: CLLocationCoordinate2D
    ) -> Double {
        let dLat = (rhs.latitude - lhs.latitude).degreesToRadians
        let dLon = (rhs.longitude - lhs.longitude).degreesToRadians
        let lat1 = lhs.latitude.degreesToRadians
        let lat2 = rhs.latitude.degreesToRadians

        let a = pow(sin(dLat / 2), 2)
            + cos(lat1) * cos(lat2) * pow(sin(dLon / 2), 2)
        return 2 * atan2(
            sqrt(max(0, a)),
            sqrt(max(0, 1 - a))
        )
    }

    private static func initialBearing(
        from lhs: CLLocationCoordinate2D,
        to rhs: CLLocationCoordinate2D
    ) -> Double {
        let lat1 = lhs.latitude.degreesToRadians
        let lat2 = rhs.latitude.degreesToRadians
        let deltaLongitude =
            (rhs.longitude - lhs.longitude).degreesToRadians
        return atan2(
            sin(deltaLongitude) * cos(lat2),
            cos(lat1) * sin(lat2) -
                sin(lat1) * cos(lat2) * cos(deltaLongitude)
        )
    }
}

private extension Double {
    var degreesToRadians: Double { self * .pi / 180 }
    var radiansToDegrees: Double { self * 180 / .pi }

    var normalizedLongitude: Double {
        var lon = self
        while lon > 180 { lon -= 360 }
        while lon < -180 { lon += 360 }
        return lon
    }
}
