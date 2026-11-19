// AirESP32ace
// Created by VM, November 2026

import AppKit
import XCTest
@testable import AirspaceCompanion

final class WeatherCityArtworkTests: XCTestCase {
    private func temporaryDirectory() throws -> URL {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: false)
        addTeardownBlock { try FileManager.default.removeItem(at: dir) }
        return dir
    }

    func testAllEightOriginalPayloadsAreByteExact() throws {
        let root = ProjectAssetCatalog.assetRoot
        let noOverrides = try temporaryDirectory()
        for key in ["SJC", "SFO", "SAN", "JFK", "MOW", "LED", "DUS", "ICN"] {
            let original = try Data(contentsOf: root.appendingPathComponent("weather_city_icons/\(key).rga"))
            XCTAssertEqual(original.count, 49_152)
            XCTAssertEqual(try ProjectAssetCatalog.weatherCityIconData(
                for: key.lowercased(), assetRoot: root, customDirectory: noOverrides
            ), original, key)
        }
    }

    func testUserOverrideWinsAndWorksForCityWithoutBundledArt() throws {
        let overrides = try temporaryDirectory()
        let bitmap = try XCTUnwrap(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 2, pixelsHigh: 2,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0))
        let rgba = try XCTUnwrap(bitmap.bitmapData)
        for y in 0..<2 {
            for x in 0..<2 {
                let i = y * bitmap.bytesPerRow + x * 4
                let value: UInt8 = x == y ? 255 : 0
                rgba[i] = value
                rgba[i + 1] = value
                rgba[i + 2] = 0
                rgba[i + 3] = value
            }
        }
        let png = try XCTUnwrap(bitmap.representation(using: .png, properties: [:]))
        for city in ["SJC", "CLT"] {
            let file = overrides.appendingPathComponent("\(city).png")
            try png.write(to: file)
            let expected = try ImageWriter.rgb565AlphaData(from: XCTUnwrap(NSImage(contentsOf: file)),
                                                         width: 128, height: 128)
            XCTAssertEqual(expected.count, 49_152)
            XCTAssertTrue(expected.suffix(128 * 128).contains(255))
            XCTAssertEqual(try ProjectAssetCatalog.weatherCityIconData(for: city, customDirectory: overrides), expected)
        }
        XCTAssertNil(try ProjectAssetCatalog.weatherCityIconData(for: "CAE", customDirectory: overrides))
    }

    func testMissingOrTruncatedOriginalCannotMasqueradeAsSuccessfulRepair() throws {
        let root = try temporaryDirectory()
        let dir = root.appendingPathComponent("weather_city_icons")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: false)
        XCTAssertThrowsError(try ProjectAssetCatalog.weatherCityIconData(for: "DUS", assetRoot: root, customDirectory: root))
        try Data([1, 2, 3]).write(to: dir.appendingPathComponent("DUS.rga"))
        XCTAssertThrowsError(try ProjectAssetCatalog.weatherCityIconData(for: "DUS", assetRoot: root, customDirectory: root))
        XCTAssertNil(try ProjectAssetCatalog.weatherCityIconData(for: "../DUS", assetRoot: root, customDirectory: root))
    }
}
