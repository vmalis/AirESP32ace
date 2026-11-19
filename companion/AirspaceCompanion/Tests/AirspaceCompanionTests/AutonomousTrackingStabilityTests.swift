// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class AutonomousTrackingStabilityTests: XCTestCase {
    func testEasternDSTTransitionAndFixedZone() throws {
        let start = Date(timeIntervalSince1970: 1_793_426_400)
        let end = Date(timeIntervalSince1970: 1_793_599_200)
        let steps = try DeviceClient.offsetSteps(zoneID: "America/New_York", from: start, through: end)
        XCTAssertEqual(steps, [[1_793_426_400, -14_400], [1_793_512_800, -18_000]])
        XCTAssertEqual(try DeviceClient.offsetSteps(zoneID: "Asia/Seoul", from: start, through: end),
                       [[1_793_426_400,32_400]])
    }

    func testArrivalOrCancellationYieldsToAnImminentConnection() {
        let now = Date()
        var notice = PlannedFlight(flightNumber: "XX101", departureDate: now,
                                   actualArrival: now.addingTimeInterval(-600))
        var next = PlannedFlight(flightNumber: "XX102", departureDate: now,
                                 scheduledDeparture: now.addingTimeInterval(1800))
        XCTAssertEqual(PlannedFlightPriority.select(from: [notice,next], currentPlanID: notice.id, at: now)?.id, next.id)
        notice.actualArrival = nil
        notice.statusSummary = "Cancelled"
        notice.lastRefreshed = now.addingTimeInterval(-200)
        XCTAssertEqual(PlannedFlightPriority.select(from: [notice,next], at: now)?.id, next.id)
        next.estimatedDeparture = now.addingTimeInterval(18000)
        XCTAssertEqual(PlannedFlightPriority.select(from: [notice,next], at: now)?.id, notice.id)
        XCTAssertEqual(PlannedFlightPriority.select(from: [notice,next], at: now.addingTimeInterval(7200))?.id, next.id)
    }

    func testWithdrawnAndOmittedGateAndSampleAge() throws {
        let wire = Data(#"{"deviceTrackingSupported":true,"preparedFlightCount":1,"checkedUnix":1000,"originGate":"","status":"Cancelled"}"#.utf8)
        let snapshot = try JSONDecoder().decode(DeviceTrackerSnapshot.self, from: wire)
        var flight = TrackedFlight.airPremiaYP111
        flight.originGate = "A6"; flight.destinationGate = "B7"
        let updated = snapshot.updating(flight)
        XCTAssertNil(updated.originGate)
        XCTAssertEqual(updated.destinationGate,"B7")
        XCTAssertEqual(updated.lastUpdated,Date(timeIntervalSince1970: 1000))
        XCTAssertEqual(updated.journeyPhase, "cancelled")
        XCTAssertFalse(updated.isActivelyTrackable)
    }

    func testDivertedFlightNeverFabricatesArrivalAtBookedAirport() throws {
        for (status, expectedPhase) in [("Diverted","diverted"),("Landed after diversion","arrived")] {
            let payload: [String: Any] = ["deviceTrackingSupported":true, "preparedFlightCount":1,
                "status":status, "routeProgress":0.72, "checkedUnix":1000]
            let snapshot = try JSONDecoder().decode(DeviceTrackerSnapshot.self,
                from: JSONSerialization.data(withJSONObject: payload))
            let flight = snapshot.updating(.airPremiaYP111).reconciledTerminalState()
            XCTAssertEqual(flight.journeyPhase,expectedPhase)
            XCTAssertEqual(flight.routeProgress,0)
            XCTAssertNil(flight.lastPosition)
        }
    }
}
