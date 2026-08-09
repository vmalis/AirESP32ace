// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class DeviceProvisioningTests: XCTestCase {
    func testProvisioningCSVUsesFirmwareNVSKeysAndEscapesValues() {
        let configuration = fixture(
            homeSSID: "Studio, Wi-Fi",
            homePassword: "quote\"comma,pass"
        )
        let csv = configuration.nvsCSV

        XCTAssertTrue(csv.contains("aircfg,namespace,,"))
        XCTAssertTrue(csv.contains("home_ssid,data,string,\"Studio, Wi-Fi\""))
        XCTAssertTrue(csv.contains("home_pass,data,string,\"quote\"\"comma,pass\""))
        XCTAssertTrue(csv.contains("rad_range,data,u16,20"))
        XCTAssertTrue(csv.contains("revision,data,u32,1"))
    }

    func testProvisioningCSVBuildsACompleteNVSPartitionWhenIDFToolsExist() throws {
        guard let python = DeviceFirmwareResources.python,
              let generator = DeviceFirmwareResources.nvsGenerator
        else {
            throw XCTSkip("ESP-IDF Python tools are not installed")
        }

        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(
                "AirspaceProvisioningTest-\(UUID().uuidString)",
                isDirectory: true
            )
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }

        let csv = directory.appendingPathComponent("config.csv")
        let binary = directory.appendingPathComponent("config.bin")
        try fixture().nvsCSV.write(
            to: csv,
            atomically: true,
            encoding: .utf8
        )

        let process = Process()
        let output = Pipe()
        process.executableURL = python
        process.arguments = [
            generator.path,
            "generate",
            csv.path,
            binary.path,
            "0x6000",
        ]
        process.standardOutput = output
        process.standardError = output
        try process.run()
        process.waitUntilExit()

        let diagnostics = String(
            data: output.fileHandleForReading.readDataToEndOfFile(),
            encoding: .utf8
        ) ?? ""
        XCTAssertEqual(process.terminationStatus, 0, diagnostics)
        let attributes = try FileManager.default.attributesOfItem(
            atPath: binary.path
        )
        XCTAssertEqual(attributes[.size] as? Int, 0x6000)
    }

    private func fixture(
        homeSSID: String = "Studio Wi-Fi",
        homePassword: String = "password"
    ) -> DeviceProvisioningConfiguration {
        DeviceProvisioningConfiguration(
            activeProfile: "home",
            homeSSID: homeSSID,
            homePassword: homePassword,
            hotspotSSID: "Personal Hotspot",
            hotspotPassword: "hotspot-password",
            locationKey: "SJC",
            locationLabel: "San Jose",
            latitude: "37.3382",
            longitude: "-121.8863",
            timezone: "PST8PDT,M3.2.0,M11.1.0",
            metar: "KSJC",
            radarHalfDegrees: "0.65",
            aircraftLabels: true,
            radarWedge: true,
            accent: "green",
            brightnessPercent: 80,
            radarRadiusMiles: 20,
            radarCityKeys: ["SJC", "SFO", "SAN", "JFK"],
            clockCityKeys: ["SJC", "JFK"],
            openSkyEnabled: true,
            openSkyClientID: "client",
            openSkyClientSecret: "secret",
            airLabsAPIKey: "airlabs",
            flightAwareAvailable: true
        )
    }
}
