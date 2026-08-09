// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class FallbackFlightStatusProviderTests: XCTestCase {
    func testFallsBackWhenFlightAwareFails() async throws {
        let provider = FallbackFlightStatusProvider(
            primary: ThrowingFlightProvider(),
            fallback: ReturningFlightProvider()
        )

        let result = try await provider.trackedFlight(
            for: "YP111",
            on: nil
        )

        XCTAssertEqual(result.marketingNumber, "YP111")
    }

    func testSuccessfulFlightAwareDoesNotCallAirLabsFallback() async throws {
        let provider = FallbackFlightStatusProvider(
            primary: MissingTelemetryFlightProvider(),
            fallback: UnexpectedFallbackFlightProvider()
        )

        let result = try await provider.trackedFlight(
            for: "YP113",
            on: nil
        )

        XCTAssertEqual(result.dataSourceSummary, "FlightAware AeroAPI")
        XCTAssertNil(result.lastPosition)
        XCTAssertNil(result.altitudeMeters)
        XCTAssertNil(result.groundSpeedKPH)
        XCTAssertNil(result.headingDegrees)
        XCTAssertEqual(result.routeProgress, 0.60)
        XCTAssertEqual(result.estimatedArrival?.timeIntervalSince1970, 2_000)
    }

    func testUsesOpenSkyTelemetryBeforeAirLabsFallback() async throws {
        let provider = FallbackFlightStatusProvider(
            primary: MissingTelemetryFlightProvider(),
            fallback: UnexpectedFallbackFlightProvider(),
            telemetryProvider: ReturningTelemetryProvider(),
            now: { Date(timeIntervalSince1970: 1_320) }
        )

        let result = try await provider.trackedFlight(
            for: "YP113",
            on: nil
        )

        XCTAssertEqual(
            result.dataSourceSummary,
            "FlightAware AeroAPI + OpenSky"
        )
        XCTAssertEqual(result.lastPosition?.latitude, 42.25)
        XCTAssertEqual(result.altitudeMeters, 11_120)
        XCTAssertEqual(result.groundSpeedKPH, 914)
        XCTAssertEqual(result.headingDegrees, 83)
        XCTAssertEqual(result.routeProgress ?? -1, 0.629, accuracy: 0.02)
    }

    func testSelectedLegUsesAirLabsOnlyToFillMissingGates() async throws {
        let departure = Date(timeIntervalSince1970: 10_000)
        let selection = FlightLegSelection(
            providerIdentifier: "APZ111-1",
            originCode: "ICN",
            destinationCode: "SFO",
            scheduledDeparture: departure
        )
        let provider = FallbackFlightStatusProvider(
            primary: SelectedFlightAwareProvider(),
            fallback: SelectedAirLabsGateProvider(departure: departure)
        )

        let result = try await provider.trackedFlight(
            for: "YP111",
            on: departure,
            matching: selection
        )

        XCTAssertEqual(result.originTerminal, "1")
        XCTAssertEqual(result.originGate, "32")
        XCTAssertEqual(result.destinationTerminal, "I")
        XCTAssertEqual(result.destinationGate, "A6")
        XCTAssertEqual(
            result.dataSourceSummary,
            "FlightAware AeroAPI + AirLabs"
        )
        XCTAssertEqual(result.origin.code, "ICN")
        XCTAssertEqual(result.destination.code, "SFO")
    }

    func testAirLabsMetadataIsMergedWithOpenSkyState() async throws {
        let now = Date()
        let departure = now.addingTimeInterval(-3 * 3600)
        let arrival = now.addingTimeInterval(8 * 3600)
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightFallbackURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightFallbackURLProtocol.handler = { request in
            if request.url?.host == "airlabs.co" {
                let payload: [String: Any] = [
                    "response": [
                        "hex": "abc123",
                        "flight_iata": "YP111",
                        "flight_icao": "APZ111",
                        "airline_iata": "YP",
                        "dep_iata": "ICN",
                        "dep_icao": "RKSI",
                        "arr_iata": "SFO",
                        "arr_icao": "KSFO",
                        "aircraft_icao": "B789",
                        "status": "en-route",
                        "lat": 45.0,
                        "lng": -170.0,
                        "alt": 10_000,
                        "dir": 80,
                        "speed": 850,
                        "v_speed": 0,
                        "updated": now.timeIntervalSince1970,
                        "dep_time_ts": departure.timeIntervalSince1970,
                        "arr_time_ts": arrival.timeIntervalSince1970,
                        "duration": 660,
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

            let empty: Any = NSNull()
            let row: [Any] = [
                "abc123", "APZ111", "KR",
                now.timeIntervalSince1970,
                now.timeIntervalSince1970,
                -168.5, 46.25, 10_500, false,
                250.0, 92.0, 1.2,
                empty, empty, empty, empty, empty, 6,
            ]
            let payload: [String: Any] = [
                "time": now.timeIntervalSince1970,
                "states": [row],
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

        let provider = AirLabsOpenSkyFlightStatusProvider(
            apiKey: "airlabs-test",
            openSkyClientID: "",
            openSkyClientSecret: "",
            session: session,
            now: { now }
        )
        let result = try await provider.trackedFlight(
            for: "YP111",
            on: nil
        )

        XCTAssertEqual(result.dataSourceSummary, "AirLabs + OpenSky")
        XCTAssertEqual(result.lastPosition?.latitude, 46.25)
        XCTAssertEqual(result.lastPosition?.longitude, -168.5)
        XCTAssertEqual(result.altitudeMeters, 10_500)
        XCTAssertEqual(result.groundSpeedKPH, 900)
        XCTAssertEqual(result.headingDegrees, 92)
        XCTAssertEqual(result.verticalPhase, "climbing")
        XCTAssertEqual(result.aircraftTypeCode, "B789")
    }

    func testAirLabsRouteUsesOpenSkyCallsignWhenHexIsMissing() async throws {
        let now = Date()
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightFallbackURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightFallbackURLProtocol.handler = { request in
            if request.url?.host == "airlabs.co" {
                let payload: [String: Any] = [
                    "response": [
                        "flight_iata": "YP113",
                        "flight_icao": "APZ113",
                        "airline_iata": "YP",
                        "dep_iata": "ICN",
                        "arr_iata": "SFO",
                        "aircraft_icao": "B789",
                        "status": "en-route",
                        "lat": 42.0,
                        "lng": -165.0,
                        "dep_actual_ts": now.addingTimeInterval(-7 * 3600)
                            .timeIntervalSince1970,
                        "arr_estimated_ts": now.addingTimeInterval(4 * 3600)
                            .timeIntervalSince1970,
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

            XCTAssertNotNil(
                URLComponents(url: request.url!, resolvingAgainstBaseURL: false)?
                    .queryItems?.first(where: { $0.name == "lamin" })
            )
            let row: [Any] = [
                "abc123", "APZ113 ", "KR",
                now.timeIntervalSince1970,
                now.timeIntervalSince1970,
                -164.5, 42.5, 11_250, false,
                252.0, 84.0, 0.1,
                NSNull(), NSNull(), NSNull(), NSNull(), NSNull(), 6,
            ]
            let payload: [String: Any] = [
                "time": now.timeIntervalSince1970,
                "states": [row],
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

        let provider = AirLabsOpenSkyFlightStatusProvider(
            apiKey: "airlabs-test",
            openSkyClientID: "",
            openSkyClientSecret: "",
            session: session,
            now: { now }
        )
        let result = try await provider.trackedFlight(
            for: "YP113",
            on: nil
        )

        XCTAssertEqual(result.dataSourceSummary, "AirLabs + OpenSky")
        XCTAssertEqual(result.altitudeMeters, 11_250)
        XCTAssertEqual(result.groundSpeedKPH, 907)
        XCTAssertEqual(result.headingDegrees, 84)
    }

    func testAirLabsLiveFallbackPreservesMetricUnits() async throws {
        let now = Date()
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightFallbackURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightFallbackURLProtocol.handler = { request in
            if request.url?.host == "airlabs.co" {
                let isLiveList = request.url?.path.hasSuffix("/flights") == true
                let response: Any = isLiveList
                    ? [[
                        "flight_iata": "YP115",
                        "flight_icao": "APZ115",
                        "lat": 51.2,
                        "lng": -154.1,
                        "alt": 11_278,
                        "speed": 904,
                        "dir": 83,
                        "v_speed": 0,
                        "updated": now.timeIntervalSince1970,
                        "status": "en-route",
                    ]]
                    : [
                        "flight_iata": "YP115",
                        "flight_icao": "APZ115",
                        "airline_iata": "YP",
                        "dep_iata": "ICN",
                        "arr_iata": "SFO",
                        "aircraft_icao": "B789",
                        "status": "en-route",
                        "lat": 51.2,
                        "lng": -154.1,
                        "dep_actual_ts": now.addingTimeInterval(-8 * 3600)
                            .timeIntervalSince1970,
                        "arr_estimated_ts": now.addingTimeInterval(3 * 3600)
                            .timeIntervalSince1970,
                    ]
                return (
                    HTTPURLResponse(
                        url: request.url!,
                        statusCode: 200,
                        httpVersion: nil,
                        headerFields: nil
                    )!,
                    try JSONSerialization.data(withJSONObject: [
                        "response": response
                    ])
                )
            }

            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: [
                    "time": now.timeIntervalSince1970,
                    "states": [],
                ])
            )
        }

        let result = try await AirLabsOpenSkyFlightStatusProvider(
            apiKey: "airlabs-test",
            openSkyClientID: "",
            openSkyClientSecret: "",
            session: session,
            now: { now }
        ).trackedFlight(for: "YP115", on: nil)

        // AirLabs already returns altitude in m and speed in km/h.
        XCTAssertEqual(result.dataSourceSummary, "AirLabs live fallback")
        XCTAssertEqual(result.altitudeMeters, 11_278)
        XCTAssertEqual(result.groundSpeedKPH, 904)
        XCTAssertEqual(result.headingDegrees, 83)
    }

    func testDatedAirLabsFlightSelectsCodeshareOnOriginLocalDay() async throws {
        var requestedCalendar = Calendar(identifier: .gregorian)
        requestedCalendar.timeZone = TimeZone(identifier: "America/Los_Angeles")!
        let selectedDate = requestedCalendar.date(
            from: DateComponents(year: 2026, month: 8, day: 5)
        )!
        var originCalendar = Calendar(identifier: .gregorian)
        originCalendar.timeZone = TimeZone(identifier: "Asia/Seoul")!
        let wrongDay = originCalendar.date(
            from: DateComponents(
                year: 2026, month: 8, day: 4, hour: 1
            )
        )!
        let correctDay = originCalendar.date(
            from: DateComponents(
                year: 2026, month: 8, day: 5, hour: 1
            )
        )!
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightFallbackURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightFallbackURLProtocol.handler = { request in
            XCTAssertTrue(request.url?.path.hasSuffix("/schedules") == true)
            let rows: [[String: Any]] = [
                [
                    "flight_iata": "OO123",
                    "flight_icao": "SKW123",
                    "cs_flight_iata": "UA998",
                    "dep_iata": "ICN",
                    "arr_iata": "SFO",
                    "dep_time_ts": wrongDay.timeIntervalSince1970,
                    "arr_time_ts": wrongDay.addingTimeInterval(11 * 3600)
                        .timeIntervalSince1970,
                    "status": "scheduled",
                    "alt": 0,
                    "speed": 0,
                    "dir": 0,
                ],
                [
                    "flight_iata": "OO123",
                    "flight_icao": "SKW123",
                    "cs_flight_iata": "UA998",
                    "dep_iata": "ICN",
                    "arr_iata": "SFO",
                    "dep_terminal": "1",
                    "dep_gate": "28",
                    "arr_terminal": "I",
                    "arr_gate": "A6",
                    "dep_time_ts": correctDay.timeIntervalSince1970,
                    "arr_time_ts": correctDay.addingTimeInterval(11 * 3600)
                        .timeIntervalSince1970,
                    "status": "scheduled",
                    "alt": 0,
                    "speed": 0,
                    "dir": 0,
                ],
            ]
            return (
                HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!,
                try JSONSerialization.data(withJSONObject: ["response": rows])
            )
        }

        let result = try await AirLabsOpenSkyFlightStatusProvider(
            apiKey: "airlabs-test",
            openSkyClientID: "",
            openSkyClientSecret: "",
            session: session,
            now: { selectedDate }
        ).trackedFlight(for: "UA998", on: selectedDate)

        XCTAssertEqual(result.marketingNumber, "UA998")
        XCTAssertEqual(result.operationalCallsign, "SKW123")
        XCTAssertEqual(
            result.scheduledDeparture?.timeIntervalSince1970,
            correctDay.timeIntervalSince1970
        )
        XCTAssertEqual(result.originTerminal, "1")
        XCTAssertEqual(result.originGate, "28")
        XCTAssertEqual(result.destinationTerminal, "I")
        XCTAssertEqual(result.destinationGate, "A6")
    }

    func testFutureRecurringWN3568OffersBothFridayLegs() async throws {
        var selectedCalendar = Calendar(identifier: .gregorian)
        selectedCalendar.timeZone = TimeZone(
            identifier: "America/Los_Angeles"
        )!
        let selectedDate = selectedCalendar.date(
            from: DateComponents(
                year: 2026,
                month: 8,
                day: 14,
                hour: 12
            )
        )!
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FlightFallbackURLProtocol.self]
        let session = URLSession(configuration: configuration)

        FlightFallbackURLProtocol.handler = { request in
            let path = request.url?.path ?? ""
            if path.hasSuffix("/schedules") {
                return Self.jsonResponse(
                    request,
                    object: ["response": []]
                )
            }
            if path.hasSuffix("/routes") {
                let routes: [[String: Any]] = [
                    [
                        "flight_iata": "WN3568",
                        "flight_icao": "SWA3568",
                        "airline_iata": "WN",
                        "dep_iata": "SJC",
                        "arr_iata": "BNA",
                        "dep_time": "07:00",
                        "arr_time": "13:20",
                        "days": ["thu", "fri"],
                    ],
                    [
                        "flight_iata": "WN3568",
                        "flight_icao": "SWA3568",
                        "airline_iata": "WN",
                        "dep_iata": "SAT",
                        "arr_iata": "DEN",
                        "dep_time": "18:30",
                        "arr_time": "20:00",
                        "days": "mon,thu,fri",
                    ],
                    [
                        "flight_iata": "WN3568",
                        "flight_icao": "SWA3568",
                        "airline_iata": "WN",
                        "dep_iata": "LAS",
                        "arr_iata": "PHX",
                        "dep_time": "09:00",
                        "arr_time": "10:10",
                        "days": ["sat"],
                    ],
                ]
                return Self.jsonResponse(
                    request,
                    object: ["response": routes]
                )
            }
            if path.hasSuffix("/airports") {
                let iata = URLComponents(
                    url: request.url!,
                    resolvingAgainstBaseURL: false
                )?.queryItems?.first(where: {
                    $0.name == "iata_code"
                })?.value ?? ""
                let airports: [String: [String: Any]] = [
                    "SJC": [
                        "name": "San Jose International Airport",
                        "iata_code": "SJC",
                        "lat": 37.3639,
                        "lng": -121.9289,
                        "city": "San Jose",
                        "timezone": "America/Los_Angeles",
                        "country_code": "US",
                    ],
                    "BNA": [
                        "name": "Nashville International Airport",
                        "iata_code": "BNA",
                        "lat": 36.1263,
                        "lng": -86.6774,
                        "city": "Nashville",
                        "timezone": "America/Chicago",
                        "country_code": "US",
                    ],
                    "SAT": [
                        "name": "San Antonio International Airport",
                        "iata_code": "SAT",
                        "lat": 29.5337,
                        "lng": -98.4698,
                        "city": "San Antonio",
                        "timezone": "America/Chicago",
                        "country_code": "US",
                    ],
                    "DEN": [
                        "name": "Denver International Airport",
                        "iata_code": "DEN",
                        "lat": 39.8561,
                        "lng": -104.6737,
                        "city": "Denver",
                        "timezone": "America/Denver",
                        "country_code": "US",
                    ],
                ]
                return Self.jsonResponse(
                    request,
                    object: ["response": airports[iata].map { [$0] } ?? []]
                )
            }
            if path.hasSuffix("/flights") {
                return Self.jsonResponse(
                    request,
                    object: ["response": []]
                )
            }
            XCTFail("Unexpected request: \(request.url?.absoluteString ?? "")")
            return Self.jsonResponse(request, object: ["response": []])
        }

        let provider = AirLabsOpenSkyFlightStatusProvider(
            apiKey: "airlabs-test",
            openSkyClientID: "",
            openSkyClientSecret: "",
            session: session,
            now: { selectedDate }
        )
        let legs = try await provider.flightLegs(
            for: "WN3568",
            on: selectedDate
        )

        XCTAssertEqual(legs.count, 2)
        XCTAssertEqual(
            Set(legs.map { "\($0.originCode)-\($0.destinationCode)" }),
            Set(["SJC-BNA", "SAT-DEN"])
        )

        let chosen = try XCTUnwrap(legs.first(where: {
            $0.originCode == "SJC" && $0.destinationCode == "BNA"
        }))
        let tracked = try await provider.trackedFlight(
            for: "WN3568",
            on: selectedDate,
            matching: chosen.selection
        )
        XCTAssertEqual(tracked.marketingNumber, "WN3568")
        XCTAssertEqual(tracked.origin.code, "SJC")
        XCTAssertEqual(tracked.destination.code, "BNA")
        XCTAssertEqual(tracked.dataSourceSummary, "AirLabs fallback")
    }

    private static func jsonResponse(
        _ request: URLRequest,
        object: Any
    ) -> (HTTPURLResponse, Data) {
        (
            HTTPURLResponse(
                url: request.url!,
                statusCode: 200,
                httpVersion: nil,
                headerFields: nil
            )!,
            try! JSONSerialization.data(withJSONObject: object)
        )
    }
}

private struct ThrowingFlightProvider: FlightStatusProviding {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight {
        throw FlightAwareProviderError.httpStatus(503, "Unavailable")
    }
}

private struct ReturningFlightProvider: FlightStatusProviding {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight {
        .airPremiaYP111
    }
}

private struct MissingTelemetryFlightProvider: FlightStatusProviding {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight {
        TrackedFlight(
            airlineName: "Air Premia",
            marketingNumber: "YP113",
            operationalCallsign: "APZ113",
            aircraftType: "Boeing 787-9",
            aircraftTypeCode: "B789",
            origin: .icn,
            destination: .sfo,
            scheduleSummary: "ICN → SFO",
            durationSummary: "11h 5m",
            distanceSummary: "3,250 km",
            liveStatusSummary: "En route",
            dataSourceSummary: "FlightAware AeroAPI",
            routeProgress: 0.60,
            lastPosition: nil,
            lastUpdated: Date(timeIntervalSince1970: 1_200),
            actualDeparture: Date(timeIntervalSince1970: 1_000),
            estimatedArrival: Date(timeIntervalSince1970: 2_000),
            routeDistanceKilometers: 9_100
        )
    }
}

private struct UnexpectedFallbackFlightProvider: FlightStatusProviding {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight {
        XCTFail("AirLabs must not run after a successful FlightAware lookup")
        throw FlightAwareProviderError.httpStatus(500, "Unexpected fallback")
    }
}

private struct SelectedFlightAwareProvider: FlightLegProviding {
    func flightLegs(
        for marketingNumber: String,
        on date: Date
    ) async throws -> [FlightLegCandidate] { [] }

    func trackedFlight(
        for marketingNumber: String,
        on date: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        TrackedFlight(
            airlineName: "Air Premia",
            marketingNumber: marketingNumber,
            operationalCallsign: "APZ111",
            aircraftType: "Boeing 787-9",
            aircraftTypeCode: "B789",
            origin: .icn,
            destination: .sfo,
            scheduleSummary: "ICN → SFO",
            durationSummary: "11h",
            distanceSummary: "9,100 km",
            liveStatusSummary: "Scheduled",
            dataSourceSummary: "FlightAware AeroAPI",
            routeProgress: nil,
            lastPosition: nil,
            lastUpdated: nil
        )
    }
}

private struct SelectedAirLabsGateProvider: FlightLegProviding {
    let departure: Date

    func flightLegs(
        for marketingNumber: String,
        on date: Date
    ) async throws -> [FlightLegCandidate] { [] }

    func trackedFlight(
        for marketingNumber: String,
        on date: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        TrackedFlight(
            airlineName: "Air Premia",
            marketingNumber: marketingNumber,
            operationalCallsign: "APZ111",
            aircraftType: "Boeing 787-9",
            aircraftTypeCode: "B789",
            origin: .icn,
            destination: .sfo,
            scheduleSummary: "ICN → SFO",
            durationSummary: "11h",
            distanceSummary: "9,100 km",
            liveStatusSummary: "Scheduled",
            dataSourceSummary: "AirLabs",
            routeProgress: nil,
            lastPosition: nil,
            lastUpdated: nil,
            scheduledDeparture: departure,
            originTerminal: "1",
            originGate: "32",
            destinationTerminal: "I",
            destinationGate: "A6"
        )
    }
}

private struct ReturningTelemetryProvider: TrackedFlightTelemetryProviding {
    func telemetry(for flight: TrackedFlight) async throws -> TrackedFlightTelemetry? {
        TrackedFlightTelemetry(
            coordinate: .init(latitude: 42.25, longitude: -165.5),
            altitudeMeters: 11_120,
            groundSpeedKPH: 914,
            headingDegrees: 83,
            verticalPhase: "cruising",
            updated: Date(timeIntervalSince1970: 1_300)
        )
    }
}

private final class FlightFallbackURLProtocol: URLProtocol, @unchecked Sendable {
    nonisolated(unsafe) static var handler: ((URLRequest) throws -> (HTTPURLResponse, Data))?

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        do {
            guard let handler = Self.handler else {
                throw URLError(.badServerResponse)
            }
            let (response, data) = try handler(request)
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        } catch {
            client?.urlProtocol(self, didFailWithError: error)
        }
    }

    override func stopLoading() {}
}
