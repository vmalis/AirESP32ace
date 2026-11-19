// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class DeviceTrackerTests: XCTestCase {
    func testBlankMacCredentialsDoNotClearDeviceFlightAwareKey() throws {
        let credentials = DeviceClient.DeviceConfigPayload.Credentials(
            openSky: .init(enabled: true, clientId: nil, clientSecret: nil),
            airLabs: .init(apiKey: nil),
            flightAware: .init(available: nil, apiKey: nil)
        )
        let object = try XCTUnwrap(JSONSerialization.jsonObject(
            with: JSONEncoder().encode(credentials)) as? [String: Any])
        let flightAware = try XCTUnwrap(object["flightAware"] as? [String: Any])
        XCTAssertTrue(flightAware.isEmpty)
    }

    func testPackagedAppAllowsPrivateDeviceHTTPWithoutDisablingInternetATS() throws {
        let scriptURL = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("scripts/build_app_bundle.sh")
        let script = try String(contentsOf: scriptURL, encoding: .utf8)
        let start = try XCTUnwrap(script.range(of: "<?xml version=\"1.0\""))
        let end = try XCTUnwrap(script.range(of: "</plist>", range: start.lowerBound..<script.endIndex))
        let data = Data(script[start.lowerBound..<end.upperBound].utf8)
        let plist = try XCTUnwrap(PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any])
        let ats = try XCTUnwrap(plist["NSAppTransportSecurity"] as? [String: Any])
        XCTAssertEqual(ats["NSAllowsLocalNetworking"] as? Bool, true)
        XCTAssertNil(ats["NSAllowsArbitraryLoads"])
        XCTAssertFalse(try XCTUnwrap(plist["NSLocalNetworkUsageDescription"] as? String).isEmpty)
        let exceptions = try XCTUnwrap(ats["NSExceptionDomains"] as? [String: [String: Any]])
        XCTAssertEqual(Set(exceptions.keys), Set(["10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"]))
        for entry in exceptions.values {
            XCTAssertEqual(entry["NSExceptionAllowsInsecureHTTPLoads"] as? Bool, true)
        }
    }

    func testQueueCarriesEveryExactOccurrenceAndGeographicRoute() throws {
        let departure = Date(timeIntervalSince1970: 1_789_252_560)
        var flight = TrackedFlight.airPremiaYP111
        flight.scheduledDeparture = departure
        let plans = (0..<4).map { index in
            PlannedFlight(flightNumber: flight.marketingNumber, departureDate: departure,
                scheduledDeparture: departure.addingTimeInterval(Double(index) * 86400),
                deviceSlot: index, selectedOriginCode: "ICN", selectedDestinationCode: "SFO",
                selectedDeparture: departure.addingTimeInterval(Double(index) * 86400),
                selectedProviderIdentifier: "exact-flight-\(index)")
        }
        let data = try DeviceClient().encodedTrackerQueue(
            flights: plans.map { (plan: $0, flight: flight) },
            credentials: .init(openSky: .init(enabled: true, clientId: "test-id", clientSecret: "test-secret"),
                airLabs: .init(apiKey: "test-airlabs"), flightAware: .init(available: true, apiKey: "test-flightaware")))
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let queue = try XCTUnwrap(root["trackerQueue"] as? [[String: Any]])
        XCTAssertEqual(queue.count, 4)
        XCTAssertLessThan(data.count, 20 * 1024) // Both Wi-Fi and native USB accept the same JSON.
        for (index, item) in queue.enumerated() {
            let tracking = try XCTUnwrap(item["tracking"] as? [String: Any])
            XCTAssertEqual(item["deviceSlot"] as? Int, index)
            XCTAssertEqual(tracking["providerId"] as? String, "exact-flight-\(index)")
            XCTAssertEqual(tracking["planId"] as? String, plans[index].id.uuidString)
            XCTAssertEqual(tracking["originLongitude"] as? Double, flight.origin.coordinate.longitude)
            XCTAssertEqual(tracking["destinationLongitude"] as? Double, flight.destination.coordinate.longitude)
            XCTAssertEqual((item["routePoints"] as? [Any])?.count, 24)
        }
        let credentials = try XCTUnwrap(root["trackerCredentials"] as? [String: Any])
        XCTAssertEqual((credentials["flightAware"] as? [String: Any])?["apiKey"] as? String, "test-flightaware")
    }

    func testDeviceRuntimeUpdatesPreviewAndPreservesSelectedRoute() throws {
        let json = """
        {"deviceTrackingSupported":true,"preparedFlightCount":2,
         "flightNumber":"YP111","status":"En route (estimated)","journeyPhase":"enroute",
         "routeProgress":0.52,"altitudeMeters":10973,"speedKPH":1015,"headingDegrees":264,
         "checkedUnix":1789275211,"source":"OpenSky","estimatedPosition":true,
         "estimatedArrivalUnix":1789297740,"destinationGate":"10"}
        """
        let snapshot = try JSONDecoder().decode(DeviceTrackerSnapshot.self, from: Data(json.utf8))
        let initial = TrackedFlight.airPremiaYP111
        let flight = snapshot.updating(initial)
        XCTAssertEqual(flight.routeProgress, 0.52)
        XCTAssertEqual(flight.groundSpeedKPH, 1015)
        XCTAssertEqual(flight.destinationGate, "10")
        XCTAssertEqual(flight.origin, initial.origin)
        XCTAssertEqual(flight.destination, initial.destination)
        XCTAssertTrue(flight.dataSourceSummary.contains("estimated"))
    }

    func testEmptyDeviceQueueIsReadableWithoutInventingAFlight() throws {
        let snapshot = try JSONDecoder().decode(DeviceTrackerSnapshot.self,
            from: Data(#"{"deviceTrackingSupported":true,"preparedFlightCount":0}"#.utf8))
        XCTAssertNil(snapshot.planId)
        XCTAssertNil(snapshot.routeProgress)
    }
}
