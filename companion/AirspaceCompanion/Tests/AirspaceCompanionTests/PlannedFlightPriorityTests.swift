// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import XCTest
@testable import AirspaceCompanion

final class PlannedFlightPriorityTests: XCTestCase {
    func testCurrentFlightRemainsSelectedWhenAnotherFlightOverlaps() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let first = plan(
            "YP111",
            departure: now.addingTimeInterval(-3 * 3600),
            arrival: now.addingTimeInterval(7 * 3600)
        )
        let newer = plan(
            "UA858",
            departure: now.addingTimeInterval(-1 * 3600),
            arrival: now.addingTimeInterval(10 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [first, newer],
                currentPlanID: first.id,
                at: now
            )?.id,
            first.id
        )
    }

    func testCurrentFlightRemainsSelectedAfterEstimatedArrivalPasses() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let staleActive = plan(
            "YP111",
            departure: now.addingTimeInterval(-12 * 3600),
            arrival: now.addingTimeInterval(-30 * 60)
        )
        let next = plan(
            "UA858",
            departure: now.addingTimeInterval(-10 * 60),
            arrival: now.addingTimeInterval(10 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [staleActive, next],
                currentPlanID: staleActive.id,
                at: now
            )?.id,
            staleActive.id
        )
    }

    func testDaysOldUnfinishedFlightCannotBlockNextFlight() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        var staleActive = plan(
            "WN2622",
            departure: now.addingTimeInterval(-5 * 24 * 3600),
            arrival: now.addingTimeInterval(-4 * 24 * 3600)
        )
        staleActive.actualDeparture = staleActive.scheduledDeparture
        staleActive.statusSummary = "En route · 51%"
        let next = plan(
            "UA858",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(12 * 3600)
        )

        XCTAssertTrue(
            staleActive.hasExceededUnconfirmedArrivalWindow(at: now)
        )
        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [staleActive, next],
                currentPlanID: staleActive.id,
                at: now
            )?.id,
            next.id
        )
    }

    func testOldestUnderwayFlightWinsWithoutSavedSelection() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let first = plan(
            "YP111",
            departure: now.addingTimeInterval(-3 * 3600),
            arrival: now.addingTimeInterval(7 * 3600)
        )
        let newer = plan(
            "UA858",
            departure: now.addingTimeInterval(-1 * 3600),
            arrival: now.addingTimeInterval(10 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(from: [newer, first], at: now)?.id,
            first.id
        )
    }

    func testEarlierUpcomingFlightCanReplaceLaterUpcomingSelection() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let later = plan(
            "YP111",
            departure: now.addingTimeInterval(8 * 3600),
            arrival: now.addingTimeInterval(19 * 3600)
        )
        let earlier = plan(
            "WN982",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(4 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [later, earlier],
                currentPlanID: later.id,
                at: now
            )?.id,
            earlier.id
        )
    }

    func testEarliestUpcomingFlightIsPrepared() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let later = plan(
            "LH455",
            departure: now.addingTimeInterval(48 * 3600),
            arrival: now.addingTimeInterval(59 * 3600)
        )
        let next = plan(
            "YP111",
            departure: now.addingTimeInterval(20 * 3600),
            arrival: now.addingTimeInterval(31 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(from: [later, next], at: now)?.id,
            next.id
        )
    }

    func testCompletedFlightOlderThanRetentionDoesNotRetainPriority() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        var completed = plan(
            "YP111",
            departure: now.addingTimeInterval(-4 * 3600),
            arrival: now.addingTimeInterval(6 * 3600)
        )
        completed.isComplete = true
        completed.actualArrival = now.addingTimeInterval(-3 * 3600)
        let next = plan(
            "OO3497",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(5 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(from: [completed, next], at: now)?.id,
            next.id
        )
    }

    func testRecentExplicitArrivalRemainsVisible() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        var arrived = plan(
            "YP111",
            departure: now.addingTimeInterval(-12 * 3600),
            arrival: now.addingTimeInterval(-1 * 3600)
        )
        arrived.actualArrival = now.addingTimeInterval(-30 * 60)
        let next = plan(
            "OO3497",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(5 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [arrived, next],
                currentPlanID: arrived.id,
                at: now
            )?.id,
            arrived.id
        )
    }

    func testExplicitArrivalExpiresAfterTwoHours() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        var arrived = plan(
            "YP111",
            departure: now.addingTimeInterval(-12 * 3600),
            arrival: now.addingTimeInterval(-3 * 3600)
        )
        arrived.actualArrival = now.addingTimeInterval(-2 * 3600 - 1)
        let next = plan(
            "OO3497",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(5 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightPriority.select(
                from: [arrived, next],
                currentPlanID: arrived.id,
                at: now
            )?.id,
            next.id
        )
        XCTAssertTrue(arrived.isExpiredAfterArrival(at: now))
    }

    func testCompletedRouteWithStaleEnRouteStatusIsReconciledAsArrived() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let eta = now.addingTimeInterval(-90 * 60)
        let stale = trackedFlight(
            status: "En route · 100%",
            progress: 1,
            departure: now.addingTimeInterval(-4 * 3600),
            arrival: eta
        )

        XCTAssertEqual(stale.journeyPhase(at: now), "arrived")
        let reconciled = stale.reconciledTerminalState(at: now)
        XCTAssertEqual(reconciled.liveStatusSummary, "Arrived")
        XCTAssertEqual(reconciled.routeProgress, 1)
        XCTAssertEqual(reconciled.actualArrival, eta)
        XCTAssertEqual(reconciled.altitudeMeters, 0)
        XCTAssertEqual(reconciled.groundSpeedKPH, 0)
        XCTAssertEqual(reconciled.verticalPhase, "landed")
        XCTAssertEqual(reconciled.lastPosition, Airport.sfo.coordinate)
        XCTAssertEqual(
            reconciled.postArrivalExpiryDate(at: now),
            eta.addingTimeInterval(2 * 3600)
        )
    }

    func testTerminalInferenceDoesNotCompleteRecentOrPartialFlight() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let recent = trackedFlight(
            status: "En route · 100%",
            progress: 1,
            departure: now.addingTimeInterval(-2 * 3600),
            arrival: now.addingTimeInterval(-20 * 60)
        )
        let partial = trackedFlight(
            status: "En route · 98%",
            progress: 0.98,
            departure: now.addingTimeInterval(-4 * 3600),
            arrival: now.addingTimeInterval(-90 * 60)
        )

        XCTAssertEqual(recent.journeyPhase(at: now), "enroute")
        XCTAssertEqual(partial.journeyPhase(at: now), "enroute")
    }

    func testDaysOldPartialRouteIsReconciledAndExpired() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let eta = now.addingTimeInterval(-4 * 24 * 3600)
        let stale = trackedFlight(
            status: "En route · 51%",
            progress: 0.51,
            departure: now.addingTimeInterval(-5 * 24 * 3600),
            arrival: eta
        )

        XCTAssertEqual(stale.journeyPhase(at: now), "arrived")
        let reconciled = stale.reconciledTerminalState(at: now)
        XCTAssertEqual(reconciled.routeProgress, 1)
        XCTAssertEqual(reconciled.actualArrival, eta)
        XCTAssertEqual(reconciled.verticalPhase, "landed")
        XCTAssertTrue(reconciled.isExpiredAfterArrival(at: now))
    }

    func testScheduleRejectsOverlappingFlight() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let existing = plan(
            "WN982",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(5 * 3600)
        )
        let overlapping = plan(
            "AS806",
            departure: now.addingTimeInterval(4 * 3600),
            arrival: now.addingTimeInterval(7 * 3600)
        )

        XCTAssertEqual(
            PlannedFlightSchedule.conflict(
                for: overlapping,
                among: [existing]
            )?.id,
            existing.id
        )
    }

    func testScheduleAllowsBackToBackFlights() {
        let now = Date(timeIntervalSince1970: 2_000_000)
        let existing = plan(
            "WN982",
            departure: now.addingTimeInterval(2 * 3600),
            arrival: now.addingTimeInterval(5 * 3600)
        )
        let next = plan(
            "AS806",
            departure: now.addingTimeInterval(5 * 3600),
            arrival: now.addingTimeInterval(8 * 3600)
        )

        XCTAssertNil(
            PlannedFlightSchedule.conflict(for: next, among: [existing])
        )
    }

    func testCircularFlagAssetsHaveExpectedDeviceSize() throws {
        let requiredCodes = [
            "RU", "US", "KR", "CN", "CA", "DE",
            "FR", "TR", "GB", "ES", "FI", "HK",
            "MX", "JP", "SE", "DK", "NL", "AT",
            "SG", "AE",
        ]
        var renderedFlags: [Data] = []
        let outputDirectory = URL(
            fileURLWithPath:
                "/private/tmp/airspace-route-flags",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )
        for code in requiredCodes {
            let data = try FlagRenderer.rgb565Flag(
                countryCode: code
            )
            XCTAssertEqual(
                data.count,
                40 * 40 * 2,
                "\(code) flag did not match device size"
            )
            renderedFlags.append(data)
            try data.write(
                to: outputDirectory.appendingPathComponent(
                    "\(code.lowercased()).rgb"
                )
            )
        }
        XCTAssertEqual(
            Set(renderedFlags).count,
            requiredCodes.count,
            "Required flags must render as distinct artwork"
        )
    }

    private func plan(
        _ number: String,
        departure: Date,
        arrival: Date
    ) -> PlannedFlight {
        PlannedFlight(
            flightNumber: number,
            departureDate: departure,
            scheduledDeparture: departure,
            estimatedArrival: arrival
        )
    }

    private func trackedFlight(
        status: String,
        progress: Double,
        departure: Date,
        arrival: Date
    ) -> TrackedFlight {
        TrackedFlight(
            airlineName: "Southwest Airlines",
            marketingNumber: "WN2622",
            operationalCallsign: "SWA2622",
            aircraftType: "Boeing 737-800",
            aircraftTypeCode: "B738",
            origin: Airport(
                code: "SJC",
                name: "San Jose Mineta International",
                city: "San Jose",
                coordinate: CLLocationCoordinate2D(latitude: 37.3639, longitude: -121.9289),
                countryCode: "US",
                timeZoneIdentifier: "America/Los_Angeles"
            ),
            destination: Airport.sfo,
            scheduleSummary: "SJC → SFO",
            durationSummary: "1h",
            distanceSummary: "0 km",
            liveStatusSummary: status,
            dataSourceSummary: "FlightAware AeroAPI",
            routeProgress: progress,
            lastPosition: Airport.sfo.coordinate,
            lastUpdated: departure,
            altitudeMeters: 10_000,
            groundSpeedKPH: 800,
            headingDegrees: 310,
            verticalPhase: "cruise",
            actualDeparture: departure,
            estimatedArrival: arrival
        )
    }
}
