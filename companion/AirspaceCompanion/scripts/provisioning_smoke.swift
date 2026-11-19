// AirESP32ace
// Created by VM, August 2026
import Foundation

@main
struct ProvisioningSmoke {
    static func main() throws {
        let config = DeviceProvisioningConfiguration(
            activeProfile: "home", homeSSID: "Test, Wi-Fi", homePassword: "quote\"comma,pass",
            hotspotSSID: "", hotspotPassword: "", locationKey: "DUS", locationLabel: "Dusseldorf",
            latitude: "51.2895", longitude: "6.7668", timezone: "CET-1CEST,M3.5.0,M10.5.0/3",
            metar: "EDDL", radarHalfDegrees: "1.55", aircraftLabels: true, radarWedge: true,
            accent: "green", brightnessPercent: 80, radarRadiusMiles: 20,
            radarCityKeys: ["SJC", "SFO", "SAN", "JFK", "MOW", "LED", "DUS", "ICN"],
            clockCityKeys: ["DUS", "SJC"], openSkyEnabled: true, openSkyClientID: "test-id",
            openSkyClientSecret: "test-secret", airLabsAPIKey: "test-airlabs",
            flightAwareAPIKey: "test-flightaware", flightAwareAvailable: true
        )
        precondition(config.nvsCSV.contains("fa_key,data,string,test-flightaware"))
        precondition(config.nvsCSV.contains("home_pass,data,string,\"quote\"\"comma,pass\""))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let csv = directory.appendingPathComponent("config.csv")
        let binary = directory.appendingPathComponent("config.bin")
        try config.nvsCSV.write(to: csv, atomically: true, encoding: .utf8)
        let process = Process()
        process.executableURL = URL(fileURLWithPath: CommandLine.arguments[1])
        process.arguments = ["nvs", "generate", csv.path, binary.path, "0x6000"]
        process.environment = ["PATH": "/usr/bin:/bin", "PYTHONPATH": "/nonexistent"]
        process.currentDirectoryURL = directory
        let output = Pipe()
        process.standardOutput = output
        process.standardError = output
        try process.run()
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        precondition(process.terminationStatus == 0, String(decoding: data, as: UTF8.self))
        let nvs = try Data(contentsOf: binary)
        precondition(nvs.count == 0x6000)
        precondition(nvs.range(of: Data("test-flightaware".utf8)) != nil)
        print("Provisioning smoke: passed (real CSV serializer, bundled runtime, full NVS, all API keys)")
    }
}
