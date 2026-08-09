// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class FlightAwareFlightStatusProviderTests: XCTestCase {
    func testAirPremiaMarketingNumbersUseICAOIdent() {
        XCTAssertEqual(
            FlightAwareFlightStatusProvider.flightAwareIdent(for: "YP113"),
            "APZ113"
        )
        XCTAssertEqual(
            FlightAwareFlightStatusProvider.flightAwareIdent(for: "YP0113"),
            "APZ113"
        )
        XCTAssertEqual(
            FlightAwareFlightStatusProvider.flightAwareIdent(for: "APZ113"),
            "APZ113"
        )
        XCTAssertEqual(
            FlightAwareFlightStatusProvider.flightAwareIdent(for: "ASA806"),
            "AS806"
        )
        XCTAssertEqual(FlightNumberIdentity.canonical("SWA0982"), "WN982")
    }

    func testMultipleSameDayLegsCanBeSelectedExactly() async throws {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightAwareURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightAwareURLProtocol.handler = { request in
            if request.url?.path.hasSuffix("/position") == true {
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 404,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    Data()
                )
            }
            if request.url?.path.hasSuffix(
                "/flights/SWA982-upcoming-leg"
            ) == true {
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 404,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    Data()
                )
            }
            XCTAssertTrue(request.url?.path.hasSuffix("/flights/WN982") == true)
            let payload: [String: Any] = [
                "flights": [
                    [
                        "ident": "SWA982",
                        "ident_icao": "SWA982",
                        "ident_iata": "WN982",
                        "fa_flight_id": "SWA982-completed-leg",
                        "cancelled": false,
                        "origin": ["code_iata": "ICN"],
                        "destination": ["code_iata": "SFO"],
                        "scheduled_out": "2026-08-07T00:00:00Z",
                        "actual_out": "2026-08-07T00:04:00Z",
                        "actual_in": "2026-08-07T11:40:00Z",
                        "status": "Arrived",
                    ],
                    [
                        "ident": "SWA982",
                        "ident_icao": "SWA982",
                        "ident_iata": "WN982",
                        "fa_flight_id": "SWA982-upcoming-leg",
                        "cancelled": false,
                        "origin": ["code_iata": "SFO"],
                        "destination": ["code_iata": "ICN"],
                        "scheduled_out": "2026-08-07T20:00:00Z",
                        "scheduled_in": "2026-08-08T08:00:00Z",
                        "status": "Scheduled",
                    ],
                ]
            ]
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: payload)
            )
        }

        let selectedDate = ISO8601DateFormatter().date(
            from: "2026-08-07T12:00:00Z"
        )!
        let provider = FlightAwareFlightStatusProvider(
            apiKey: "test",
            session: session,
            now: { selectedDate }
        )
        let legs = try await provider.flightLegs(
            for: "SWA982",
            on: selectedDate
        )

        XCTAssertEqual(legs.count, 2)
        let upcoming = try XCTUnwrap(legs.first {
            $0.providerIdentifier == "SWA982-upcoming-leg"
        })
        XCTAssertFalse(upcoming.isComplete)
        XCTAssertEqual(upcoming.originCode, "SFO")
        XCTAssertEqual(upcoming.destinationCode, "ICN")

        let tracked = try await provider.trackedFlight(
            for: "WN982",
            on: selectedDate,
            matching: upcoming.selection
        )
        XCTAssertEqual(tracked.origin.code, "SFO")
        XCTAssertEqual(tracked.destination.code, "ICN")
    }

    func testPositionTelemetryIsNormalizedFromFlightAwareUnits() async throws {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightAwareURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightAwareURLProtocol.handler = { request in
            let payload: [String: Any]
            if request.url?.path.hasSuffix("/position") == true {
                payload = [
                    "last_position": [
                        "latitude": 51.25,
                        "longitude": -154.10,
                        "timestamp": "2026-08-01T16:00:00Z",
                        "altitude": 370,
                        "groundspeed": 488,
                        "heading": 83,
                        "altitude_change": "C",
                    ]
                ]
            } else {
                payload = [
                    "flights": [[
                        "ident": "APZ113",
                        "ident_icao": "APZ113",
                        "ident_iata": "YP113",
                        "fa_flight_id": "APZ113-test",
                        "cancelled": false,
                        "origin": [
                            "code_iata": "ICN",
                            "code_icao": "RKSI",
                        ],
                        "terminal_origin": "1",
                        "gate_origin": "28",
                        "destination": [
                            "code_iata": "SFO",
                            "code_icao": "KSFO",
                        ],
                        "terminal_destination": "I",
                        "gate_destination": "A6",
                        "actual_out": "2026-08-01T08:00:00Z",
                        "estimated_in": "2026-08-01T20:00:00Z",
                        "status": "En Route",
                        "aircraft_type": "B789",
                        "route_distance": 4_910,
                    ]]
                ]
            }
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: payload)
            )
        }

        let result = try await FlightAwareFlightStatusProvider(
            apiKey: "test",
            session: session,
            now: { Date(timeIntervalSince1970: 1_785_600_000) }
        ).trackedFlight(for: "YP113", on: nil)

        // FlightAware position altitude is hundreds of feet and speed is kt.
        XCTAssertEqual(result.altitudeMeters, 11_278)
        XCTAssertEqual(result.groundSpeedKPH, 904)
        XCTAssertEqual(result.headingDegrees, 83)
        XCTAssertEqual(result.verticalPhase, "climbing")
        XCTAssertEqual(result.originTerminal, "1")
        XCTAssertEqual(result.originGate, "28")
        XCTAssertEqual(result.destinationTerminal, "I")
        XCTAssertEqual(result.destinationGate, "A6")
    }

    func testSimilarFlightNumberCannotWinFallbackSelection() async throws {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightAwareURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightAwareURLProtocol.handler = { request in
            XCTAssertFalse(request.url?.path.hasSuffix("/position") == true)
            let payload: [String: Any] = [
                "flights": [
                    [
                        "ident": "UAL1582",
                        "ident_icao": "UAL1582",
                        "ident_iata": "UA1582",
                        "cancelled": false,
                        "origin": ["code_iata": "ICN"],
                        "destination": ["code_iata": "SFO"],
                        "scheduled_out": "2026-08-05T08:00:00Z",
                        "scheduled_in": "2026-08-05T19:00:00Z",
                        "status": "Scheduled",
                    ],
                    [
                        "ident": "UA582",
                        "ident_icao": "UA582",
                        "ident_iata": "UA582",
                        "cancelled": false,
                        "origin": ["code_iata": "SFO"],
                        "destination": ["code_iata": "ICN"],
                        "scheduled_out": "2026-08-05T09:00:00Z",
                        "scheduled_in": "2026-08-05T20:00:00Z",
                        "status": "Scheduled",
                    ],
                ]
            ]
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: payload)
            )
        }

        let result = try await FlightAwareFlightStatusProvider(
            apiKey: "test",
            session: session,
            now: { Date(timeIntervalSince1970: 1_785_600_000) }
        ).trackedFlight(for: "UA582", on: nil)

        XCTAssertEqual(result.marketingNumber, "UA582")
        XCTAssertEqual(result.origin.code, "SFO")
        XCTAssertEqual(result.destination.code, "ICN")
    }

    func testExactOccurrenceEnrichesMissingGateMetadata() async throws {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightAwareURLProtocol.self]
        let session = URLSession(configuration: configuration)
        let now = ISO8601DateFormatter().date(
            from: "2026-08-08T16:00:00Z"
        )!

        FlightAwareURLProtocol.handler = { request in
            let path = request.url?.path ?? ""
            if path.contains("/airports/") {
                let isSan = path.hasSuffix("/SAN")
                let payload: [String: Any] = [
                    "code_iata": isSan ? "SAN" : "SJC",
                    "code_icao": isSan ? "KSAN" : "KSJC",
                    "name": isSan ? "San Diego International" :
                        "San Jose Mineta International",
                    "city": isSan ? "San Diego" : "San Jose",
                    "country_code": "US",
                    "latitude": isSan ? 32.7338 : 37.3639,
                    "longitude": isSan ? -117.1933 : -121.9289,
                    "timezone": "America/Los_Angeles",
                ]
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 200,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    try JSONSerialization.data(withJSONObject: payload)
                )
            }
            if path.hasSuffix("/position") {
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 404,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    Data()
                )
            }
            let base: [String: Any] = [
                "ident": "SWA1140",
                "ident_icao": "SWA1140",
                "ident_iata": "WN1140",
                "fa_flight_id": "SWA1140-20260808-leg",
                "cancelled": false,
                "origin": ["code_iata": "SAN"],
                "destination": ["code_iata": "SJC"],
                "scheduled_out": "2026-08-08T20:00:00Z",
                "estimated_out": "2026-08-08T20:10:00Z",
                "scheduled_in": "2026-08-08T21:30:00Z",
                "estimated_in": "2026-08-08T21:38:00Z",
                "status": "Scheduled",
            ]
            let payload: Any
            if path.hasSuffix("/flights/SWA1140-20260808-leg") {
                let query = URLComponents(
                    url: request.url!,
                    resolvingAgainstBaseURL: false
                )?.queryItems ?? []
                XCTAssertEqual(
                    query.first(where: { $0.name == "include_ex_data" })?
                        .value,
                    "true"
                )
                var exact = base
                exact["terminal_origin"] = "1"
                exact["gate_origin"] = "12"
                exact["terminal_destination"] = "B"
                exact["gate_destination"] = "24"
                payload = exact
            } else {
                XCTAssertTrue(path.hasSuffix("/flights/WN1140"))
                payload = ["flights": [base]]
            }
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: payload)
            )
        }

        let result = try await FlightAwareFlightStatusProvider(
            apiKey: "test",
            session: session,
            now: { now }
        ).trackedFlight(for: "WN1140", on: now)

        XCTAssertEqual(result.originTerminal, "1")
        XCTAssertEqual(result.originGate, "12")
        XCTAssertEqual(result.destinationTerminal, "B")
        XCTAssertEqual(result.destinationGate, "24")
        XCTAssertEqual(result.estimatedDeparture?.timeIntervalSince1970,
                       ISO8601DateFormatter().date(
                           from: "2026-08-08T20:10:00Z"
                       )?.timeIntervalSince1970)
        XCTAssertEqual(result.estimatedArrival?.timeIntervalSince1970,
                       ISO8601DateFormatter().date(
                           from: "2026-08-08T21:38:00Z"
                       )?.timeIntervalSince1970)
    }

    func testFutureFlightWithStaleArrivedStatusRemainsScheduled() async throws {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightAwareURLProtocol.self]
        let session = URLSession(configuration: configuration)
        let now = ISO8601DateFormatter().date(
            from: "2026-08-08T12:00:00Z"
        )!

        FlightAwareURLProtocol.handler = { request in
            if request.url?.path.contains("/airports/") == true {
                let isSan = request.url?.path.hasSuffix("/SAN") == true
                let payload: [String: Any] = [
                    "code_iata": isSan ? "SAN" : "SJC",
                    "code_icao": isSan ? "KSAN" : "KSJC",
                    "name": isSan ? "San Diego International" :
                        "San Jose Mineta International",
                    "city": isSan ? "San Diego" : "San Jose",
                    "country_code": "US",
                    "latitude": isSan ? 32.7338 : 37.3639,
                    "longitude": isSan ? -117.1933 : -121.9289,
                    "timezone": "America/Los_Angeles",
                ]
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 200,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    try JSONSerialization.data(withJSONObject: payload)
                )
            }
            let payload: [String: Any] = [
                "flights": [[
                    "ident": "SWA3565",
                    "ident_icao": "SWA3565",
                    "ident_iata": "WN3565",
                    "cancelled": false,
                    "origin": ["code_iata": "SAN"],
                    "destination": ["code_iata": "SJC"],
                    "scheduled_out": "2026-08-08T20:00:00Z",
                    "scheduled_in": "2026-08-08T21:30:00Z",
                    "status": "Arrived",
                ]]
            ]
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: payload)
            )
        }

        let provider = FlightAwareFlightStatusProvider(
            apiKey: "test",
            session: session,
            now: { now }
        )
        let legs = try await provider.flightLegs(for: "WN3565", on: now)
        let leg = try XCTUnwrap(legs.first)
        XCTAssertFalse(leg.isComplete)
        XCTAssertEqual(leg.statusSummary, "Scheduled")

        let tracked = try await provider.trackedFlight(
            for: "WN3565",
            on: now,
            matching: leg.selection
        )
        XCTAssertEqual(tracked.journeyPhase, "preflight")
        XCTAssertNil(tracked.actualArrival)
    }
}

private final class FlightAwareURLProtocol: URLProtocol, @unchecked Sendable {
    nonisolated(unsafe) static var handler:
        ((URLRequest) throws -> (HTTPURLResponse, Data))?

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(
        for request: URLRequest
    ) -> URLRequest { request }

    override func startLoading() {
        do {
            guard let handler = Self.handler else {
                throw URLError(.badServerResponse)
            }
            let (response, data) = try handler(request)
            client?.urlProtocol(
                self,
                didReceive: response,
                cacheStoragePolicy: .notAllowed
            )
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        } catch {
            client?.urlProtocol(self, didFailWithError: error)
        }
    }

    override func stopLoading() {}
}
