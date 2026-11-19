// AirESP32ace
// Created by VM, August 2026

import XCTest
@testable import AirspaceCompanion

final class RadarLocationCatalogTests: XCTestCase {
    func testExtraChoicesDoNotChangeDefaultEight() {
        XCTAssertEqual(RadarLocationPreset.all.count, 8)
        XCTAssertEqual(RadarLocationPreset.catalog.count, 10)
        XCTAssertFalse(RadarLocationPreset.all.contains { ["CLT", "CAE"].contains($0.key) })
        XCTAssertEqual(Set(RadarLocationPreset.catalog.map(\.key)).count, 10)
    }

    func testNewChoicesCarryCompleteDeviceDefinitions() throws {
        for (key, station) in [("CLT", "KCLT"), ("CAE", "KCAE")] {
            let location = try XCTUnwrap(RadarLocationPreset.preset(for: key))
            let definition = location.definition
            XCTAssertEqual(definition.metar, station)
            XCTAssertEqual(definition.timezone, "EST5EDT,M3.2.0,M11.1.0")
            XCTAssertEqual(RadarLocationPreset(definition: definition).key, key)
            XCTAssertNotEqual(Double(definition.lat), 0)
            XCTAssertNotEqual(Double(definition.lon), 0)
        }
    }
}
