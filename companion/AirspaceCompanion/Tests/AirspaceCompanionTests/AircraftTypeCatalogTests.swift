// AirESP32ace
// Created by VM, August 2026

import XCTest
@testable import AirspaceCompanion

final class AircraftTypeCatalogTests: XCTestCase {
    func testExactICAODesignatorsResolveToExpectedFamilies() {
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("T204"),
            .init(
                code: "T204",
                label: "Tupolev Tu-204 / Tu-214",
                iconSlug: "tupolev-tu-204",
                exactCode: true
            )
        )
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("B738")?.iconSlug,
            "boeing-737"
        )
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("DH8D")?.iconSlug,
            "dash-8"
        )
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("A139")?.iconSlug,
            "leonardo-aw139"
        )
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("A359")?.label,
            "Airbus A350-900"
        )
        XCTAssertEqual(
            AircraftTypeCatalog.resolve("A35K")?.label,
            "Airbus A350-1000"
        )
    }

    func testHumanModelNamesAreFallbackAliases() {
        let result = AircraftTypeCatalog.resolve("Tupolev Tu-214")
        XCTAssertNil(result?.code)
        XCTAssertEqual(result?.iconSlug, "tupolev-tu-204")
        XCTAssertEqual(result?.exactCode, false)
    }

    func testUnknownICAOCodeIsPreservedWithoutInventingAType() {
        let result = AircraftTypeCatalog.resolve("ZZZZ")
        XCTAssertEqual(result?.code, "ZZZZ")
        XCTAssertEqual(result?.label, "ZZZZ")
        XCTAssertNil(result?.iconSlug)
    }

    func testMissingTypeDoesNotFallBackToAirPremia787() {
        XCTAssertNil(AircraftTypeCatalog.resolve(nil))
        XCTAssertNil(AircraftTypeCatalog.resolve("  "))
    }

    func testCompanionLibraryLoadsTheRealProjectAircraftAssets() {
        XCTAssertEqual(ProjectAssetCatalog.aircraftEntries.count, 92)

        let tupolev = ProjectAssetCatalog.aircraftEntries.first {
            $0.slug == "tupolev-tu-204"
        }
        XCTAssertEqual(tupolev?.codes, ["T204", "T214"])
        XCTAssertNotNil(
            tupolev.flatMap(ProjectAssetCatalog.aircraftImage(for:))
        )
    }

    @MainActor
    func testCompanionLibraryLoadsBundledAirlineLogo() async {
        let skyWest = await ProjectAssetCatalog.airlineLogo(for: "OO")
        XCTAssertNotNil(skyWest)
    }
}
