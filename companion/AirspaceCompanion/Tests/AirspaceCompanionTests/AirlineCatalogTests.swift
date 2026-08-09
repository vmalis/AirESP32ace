// AirESP32ace
// Created by VM, August 2026

import XCTest
@testable import AirspaceCompanion

final class AirlineCatalogTests: XCTestCase {
    func testMajorAirlineCatalogIsLargeAndUnique() {
        let entries = ProjectAssetCatalog.majorAirlines
        XCTAssertGreaterThanOrEqual(entries.count, 100)
        XCTAssertEqual(Set(entries.map(\.code)).count, entries.count)
    }

    func testAirlineCodesKeepNumericCharacters() {
        XCTAssertEqual(
            ProjectAssetCatalog.normalizedAirlineCode("9e"),
            "9E"
        )
        XCTAssertEqual(
            ProjectAssetCatalog.normalizedAirlineCode("6E  "),
            "6E"
        )
    }

    func testAtLeastOneHundredCatalogLogosAreBundled() {
        let root = ProjectAssetCatalog.assetRoot
        let manager = FileManager.default
        let covered = ProjectAssetCatalog.majorAirlines.filter {
            entry in
            ["airline_logos", "cargo_logos"].contains {
                directory in
                manager.fileExists(
                    atPath: root
                        .appendingPathComponent(directory)
                        .appendingPathComponent("\(entry.code).png")
                        .path
                )
            }
        }
        XCTAssertGreaterThanOrEqual(covered.count, 100)
    }
}
