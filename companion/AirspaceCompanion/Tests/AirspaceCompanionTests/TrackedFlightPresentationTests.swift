// AirESP32ace
// Created by VM, August 2026

import AppKit
import XCTest
@testable import AirspaceCompanion

private func munichFlight(city: String = "") -> TrackedFlight {
    TrackedFlight(
        airlineName: "Lufthansa", marketingNumber: "LH459",
        operationalCallsign: "DLH459", aircraftType: "Airbus A350-900",
        aircraftTypeCode: "A359", origin: .sfo,
        destination: Airport(
            code: "MUC", name: "Munich International Airport", city: city,
            coordinate: .init(latitude: 48.3538, longitude: 11.7861),
            countryCode: "DE", timeZoneIdentifier: "Europe/Berlin"
        ),
        scheduleSummary: "SFO → MUC", durationSummary: "11h",
        distanceSummary: "9,436 km", liveStatusSummary: "Scheduled",
        dataSourceSummary: "Test schedule", routeProgress: 0.2,
        lastPosition: .init(latitude: 40, longitude: -110), lastUpdated: nil,
        altitudeMeters: 5193, groundSpeedKPH: 750, headingDegrees: 45,
        verticalPhase: "climbing"
    )
}

final class TrackedFlightPresentationTests: XCTestCase {
    func testCachedAirportCityFallbacksDoNotRequireRefetching() throws {
        let legacy = munichFlight()
        let restored = try JSONDecoder().decode(
            TrackedFlight.self, from: JSONEncoder().encode(legacy)
        )
        XCTAssertEqual(restored.destination.city, "")
        XCTAssertEqual(restored.destination.displayCity, "Munich")
        for code in ["MUC", "EDDM", " muc "] {
            let airport = Airport(code: code, name: "Airport", city: code,
                                  coordinate: .init(latitude: 48.3538, longitude: 11.7861))
            XCTAssertEqual(airport.displayCity, "Munich")
        }
        XCTAssertEqual(Airport(code: "DUS", name: "Airport", city: "",
                               coordinate: .init(latitude: 51.2895, longitude: 6.7668)).displayCity, "Düsseldorf")
        XCTAssertEqual(munichFlight(city: "Provider City").destination.displayCity, "Provider City")
        XCTAssertEqual(Airport(code: "XYZ", name: "Some Airport", city: "",
                               coordinate: .init()).displayCity, "")
    }

    func testWorldwideIATAAndICAOCityLookupAndIdentityGuards() throws {
        XCTAssertGreaterThan(AirportCityCatalog.entries.count, 18_000,
                             "Missing catalog in \(AppResources.bundle.bundleURL.path)")
        let examples = [
            ("LHR", "EGLL", "London"), ("CDG", "LFPG", "Paris"),
            ("HND", "RJTT", "Tokyo"), ("JNB", "FAOR", "Johannesburg"),
            ("AKL", "NZAA", "Auckland"), ("GRU", "SBGR", "São Paulo"),
            ("CLT", "KCLT", "Charlotte"), ("CAE", "KCAE", "Columbia")
        ]
        for (iata, icao, city) in examples {
            let entry = try XCTUnwrap(AirportCityCatalog.entries[iata])
            for code in [iata, icao, " \(iata.lowercased()) "] {
                let airport = Airport(code: code, name: "Not the city", city: "",
                                      coordinate: .init(latitude: entry.latitude, longitude: entry.longitude),
                                      countryCode: entry.country)
                XCTAssertEqual(airport.displayCity, city, code)
            }
            let wrongCountry = Airport(code: iata, name: "Airport", city: "",
                                       coordinate: .init(latitude: entry.latitude, longitude: entry.longitude),
                                       countryCode: "ZZ")
            XCTAssertEqual(wrongCountry.displayCity, "")
            let wrongLocation = Airport(code: iata, name: "Airport", city: "", coordinate: .init())
            XCTAssertEqual(wrongLocation.displayCity, "")
        }
    }

    func testCountdownGateAtOneHourAndDelayedDeparture() {
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        var flight = munichFlight()
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: nil, at: now), [.flightGlobe])
        flight.scheduledDeparture = now.addingTimeInterval(3601)
        XCTAssertTrue(flight.showsScheduledCountdown(at: now))
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now), [.flightGlobe])
        flight.scheduledDeparture = now.addingTimeInterval(3600)
        XCTAssertFalse(flight.showsScheduledCountdown(at: now))
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now).count, 3)
        flight.estimatedDeparture = now.addingTimeInterval(7200)
        XCTAssertTrue(flight.isDepartureDelayed)
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now), [.flightGlobe])
        flight.actualDeparture = now.addingTimeInterval(-30)
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now).count, 3)
        flight.actualArrival = now.addingTimeInterval(-7199)
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now).count, 3)
        flight.actualArrival = now.addingTimeInterval(-7200)
        XCTAssertEqual(DevicePreviewPage.trackerPages(for: flight, at: now), [.flightGlobe])
    }

    func testScheduledSnapshotClearsWrongOccurrenceMotionButKeepsAirborneData() {
        let flight = munichFlight()
        let clean = flight.reconciledTerminalState()
        XCTAssertEqual(clean.routeProgress, 0)
        XCTAssertNil(clean.lastPosition)
        XCTAssertNil(clean.altitudeMeters)
        XCTAssertNil(clean.groundSpeedKPH)
        XCTAssertNil(clean.headingDegrees)
        XCTAssertNil(clean.verticalPhase)
        var airborne = flight
        airborne.actualDeparture = Date().addingTimeInterval(-3600)
        XCTAssertEqual(airborne.reconciledTerminalState().altitudeMeters, 5193)
    }

    func testSyncSendsResolvedCitiesAndNoPreflightMotion() throws {
        var flight = munichFlight()
        flight.scheduledDeparture = Date().addingTimeInterval(48 * 3600)
        flight.estimatedDeparture = flight.scheduledDeparture?.addingTimeInterval(1800)
        let data = try DeviceClient().encodedState(
            style: .cartographicGlass, routeProgress: 0.4, flight: flight
        )
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let payload = try XCTUnwrap(root["flight"] as? [String: Any])
        XCTAssertEqual(payload["originCity"] as? String, "San Francisco")
        XCTAssertEqual(payload["destinationCity"] as? String, "Munich")
        XCTAssertEqual(root["routeProgress"] as? Double, 0)
        XCTAssertNil(payload["altitudeMeters"])
        XCTAssertNil(payload["speedKPH"])
        XCTAssertEqual(payload["delayed"] as? Bool, true)
    }
}

@MainActor
final class TrackedFlightPresentationRenderTests: XCTestCase {
    private func render(_ flight: TrackedFlight, page: DevicePreviewPage) throws -> Data {
        let image = try XCTUnwrap(NativeInstrumentPreview.snapshot(
            page: page, flight: flight, localRadarMapImage: nil,
            geodesicMapImage: nil, routeFrameMapImage: nil, airlineLogoImage: nil,
            routeProgress: flight.routeProgress ?? 0, locationName: "San Jose",
            accent: .green, launch: .preview, trackedTimePage: 0
        ))
        let bitmap = try XCTUnwrap(NSBitmapImageRep(data: XCTUnwrap(image.tiffRepresentation)))
        return try XCTUnwrap(bitmap.representation(using: .png, properties: [:]))
    }

    func testEveryDirectTrackerPageRespectsCountdownAndDelay() throws {
        var flight = munichFlight()
        flight.scheduledDeparture = Date().addingTimeInterval(48 * 3600)
        flight.estimatedDeparture = flight.scheduledDeparture?.addingTimeInterval(1800)
        let directory = URL(fileURLWithPath: "/private/tmp/airesp-muc-previews", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let countdown = try render(flight, page: .flightGlobe)
        try countdown.write(to: directory.appendingPathComponent("delayed-countdown.png"))
        XCTAssertEqual(try render(flight, page: .flightFrame), countdown)
        XCTAssertEqual(try render(flight, page: .flightDetail), countdown)
    }

    func testDetailsUsesPrescribedCityRatherThanRadarLookup() throws {
        var flight = munichFlight()
        flight.actualDeparture = Date().addingTimeInterval(-3600)
        let image = try render(flight, page: .flightDetail)
        let directory = URL(fileURLWithPath: "/private/tmp/airesp-muc-previews", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try image.write(to: directory.appendingPathComponent("munich-detail.png"))
        var different = munichFlight(city: "Test City")
        different.actualDeparture = flight.actualDeparture
        XCTAssertNotEqual(try render(different, page: .flightDetail), image,
                          "Detail must consume the supplied endpoint city, not a Radar-only lookup")
    }
}
