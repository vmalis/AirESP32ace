// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import XCTest
@testable import AirspaceCompanion

final class GreatCircleTests: XCTestCase {
    func testNearestProgressUsesTheFullRouteIncludingEndpoints() {
        let start = CLLocationCoordinate2D(
            latitude: 37.4602,
            longitude: 126.4407
        )
        let end = CLLocationCoordinate2D(
            latitude: 37.6213,
            longitude: -122.3790
        )
        let route = GreatCircle.points(
            from: start,
            to: end,
            count: 101
        )

        XCTAssertEqual(
            GreatCircle.nearestProgress(
                of: start,
                from: start,
                to: end
            ),
            0,
            accuracy: 0.000_001
        )
        XCTAssertEqual(
            GreatCircle.nearestProgress(
                of: route[50],
                from: start,
                to: end
            ),
            0.5,
            accuracy: 0.005
        )
        XCTAssertEqual(
            GreatCircle.nearestProgress(
                of: end,
                from: start,
                to: end
            ),
            1,
            accuracy: 0.000_001
        )
    }

    func testNorthUpProjectionKeepsGeographicNorthAboveSouth() {
        let newYork = CLLocationCoordinate2D(
            latitude: 40.6413,
            longitude: -73.7781
        )
        let capeTown = CLLocationCoordinate2D(
            latitude: -33.9715,
            longitude: 18.6021
        )
        let route = GreatCircle.points(
            from: newYork,
            to: capeTown,
            count: 180
        )
        let projection = NorthUpRouteProjection(
            route: route,
            size: CGSize(width: 466, height: 466)
        )
        let north = projection.screenPoint(
            for: CLLocationCoordinate2D(
                latitude: 10,
                longitude: -25
            )
        )
        let south = projection.screenPoint(
            for: CLLocationCoordinate2D(
                latitude: -10,
                longitude: -25
            )
        )

        XCTAssertLessThan(north.y, south.y)
        XCTAssertEqual(north.x, south.x, accuracy: 0.001)
    }

    func testTrackedRoutePointsStayInsideDeviceForBothRouteShapes() {
        let jfk = Airport(
            code: "JFK",
            name: "John F. Kennedy International",
            city: "New York",
            coordinate: CLLocationCoordinate2D(
                latitude: 40.6413,
                longitude: -73.7781
            ),
            countryCode: "US"
        )
        let cpt = Airport(
            code: "CPT",
            name: "Cape Town International",
            city: "Cape Town",
            coordinate: CLLocationCoordinate2D(
                latitude: -33.9715,
                longitude: 18.6021
            ),
            countryCode: "ZA"
        )
        let northSouthFlight = TrackedFlight(
            airlineName: "Test",
            marketingNumber: "TEST1",
            operationalCallsign: "TEST1",
            aircraftType: "Boeing 787-9",
            aircraftTypeCode: "B789",
            origin: jfk,
            destination: cpt,
            scheduleSummary: "-",
            durationSummary: "-",
            distanceSummary: "-",
            liveStatusSummary: "-",
            dataSourceSummary: "-",
            routeProgress: 0.5,
            lastPosition: nil,
            lastUpdated: nil
        )

        for flight in [
            TrackedFlight.airPremiaYP111,
            northSouthFlight,
        ] {
            let points =
                MapKitFlightMapRenderer.routeScreenPoints(
                    for: flight
                )
            XCTAssertEqual(
                points.count,
                MapKitFlightMapRenderer.deviceRoutePointCount
            )
            for point in points {
                XCTAssertGreaterThanOrEqual(point.x, 0)
                XCTAssertLessThanOrEqual(point.x, 466)
                XCTAssertGreaterThanOrEqual(point.y, 0)
                XCTAssertLessThanOrEqual(point.y, 466)
            }
        }
    }
}
