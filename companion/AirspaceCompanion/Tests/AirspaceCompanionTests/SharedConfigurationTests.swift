// AirESP32ace
// Created by VM, August 2026

import XCTest
@testable import AirspaceCompanion

final class SharedConfigurationTests: XCTestCase {
    func testOwnershipKeepsEditableAccentPaletteCompanionAuthoritative() {
        let ownership = SharedConfiguration.Ownership.standard

        XCTAssertTrue(ownership.companion.contains("display.accentColors"))
        XCTAssertFalse(ownership.device.contains("display.accentColors"))
        XCTAssertTrue(ownership.companion.contains("wifiProfiles"))
        XCTAssertFalse(ownership.device.contains("wifiProfiles"))
        XCTAssertTrue(ownership.device.contains("display.accentColor"))
        XCTAssertTrue(ownership.device.contains("display.weatherUnits"))
        XCTAssertFalse(ownership.companion.contains("display.weatherUnits"))
        XCTAssertFalse(ownership.device.contains("display"))
    }

    func testEveryDeviceLocationUsesItsOriginalCityArtwork() {
        for location in RadarLocationPreset.all {
            XCTAssertNotNil(
                ProjectAssetCatalog.cityImage(for: location.key),
                "Missing city artwork for \(location.key)"
            )
        }
    }

    func testTrackedFlightPlanRoundTripsThroughSharedConfigShape() throws {
        let date = Date(timeIntervalSince1970: 1_785_542_400)
        let plan = PlannedFlight(
            id: UUID(uuidString: "124856A0-6423-4D9B-9938-D53CA59A1120")!,
            flightNumber: "YP111",
            departureDate: date,
            addedAt: date.addingTimeInterval(-60),
            statusSummary: "Scheduled"
        )

        let encoded = try JSONEncoder().encode(
            SharedConfiguration.TrackedFlightPlan(plan)
        )
        let decoded = try JSONDecoder().decode(
            SharedConfiguration.TrackedFlightPlan.self,
            from: encoded
        ).plannedFlight

        XCTAssertEqual(decoded.id, plan.id)
        XCTAssertEqual(decoded.flightNumber, "YP111")
        XCTAssertEqual(decoded.departureDate, date)
        XCTAssertEqual(decoded.statusSummary, "Scheduled")
    }

    func testDefaultWorldClockLineupHasFiveUniqueKnownCities() {
        let defaults = RadarLocationPreset.defaultClockCityKeys
        XCTAssertEqual(defaults.count, 5)
        XCTAssertEqual(Set(defaults).count, defaults.count)
        XCTAssertTrue(
            defaults.allSatisfy {
                RadarLocationPreset.preset(for: $0) != nil
            }
        )
    }

    func testDockScheduleRoundTripsInOrder() throws {
        let schedule: [SharedConfiguration.DockScheduleEntry] = [
            .init(screen: .radar, durationSeconds: 20),
            .init(screen: .eclipse, durationSeconds: 10),
            .init(screen: .iss, durationSeconds: 45),
        ]
        let encoded = try JSONEncoder().encode(schedule)
        let decoded = try JSONDecoder().decode(
            [SharedConfiguration.DockScheduleEntry].self,
            from: encoded
        )
        XCTAssertEqual(decoded, schedule)
        XCTAssertEqual(decoded.map(\.screen), [.radar, .eclipse, .iss])
    }
}
