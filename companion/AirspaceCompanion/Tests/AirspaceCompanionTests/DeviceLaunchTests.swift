// AirESP32ace
// Created by VM, August 2026
import Foundation
import XCTest
@testable import AirspaceCompanion

final class DeviceLaunchTests: XCTestCase {
    private func snapshot(stale: Bool = false, available: Bool = true) throws -> DeviceLaunchSnapshot {
        let json = """
        {"deviceLaunchesSupported":true,"enabledMask":4,"retryAfterUnix":0,"launches":[{
          "available":\(available),"stale":\(stale),"checkedUnix":1790604540,"httpStatus":200,
          "agency":"spacex","id":"7d1afb26-6f9c-429b-9ccf-29012fd1e519",
          "missionName":"STARLINK GROUP 31-1 (STARSHIP FLIGHT 14)","fullName":"Starship | Starlink Group 31-1 (Starship Flight 14)",
          "vehicle":"starship","site":"starbase","padName":"Orbital Launch Pad 2","locationName":"SpaceX Starbase, TX, USA",
          "precisionName":"Second","precisionAbbreviation":"SEC","statusName":"Payload Deployed","statusAbbreviation":"Deployed",
          "watchURL":"https://www.spacex.com/launches/","netUnix":1790599739,"updatedUnix":1790604486,
          "weatherProbabilityPercent":-1}]}
        """
        return try JSONDecoder().decode(DeviceLaunchSnapshot.self, from: Data(json.utf8))
    }
    func testFlight14DeviceSnapshotPreservesExactMissionAndCountdown() throws {
        let launch = try XCTUnwrap(snapshot().presentations.first)
        XCTAssertEqual(launch.id, "7d1afb26-6f9c-429b-9ccf-29012fd1e519")
        XCTAssertEqual(launch.agency, .spacex)
        XCTAssertEqual(launch.vehicle, .starship)
        XCTAssertEqual(launch.statusName, "Payload Deployed")
        XCTAssertTrue(launch.hasCountdownPrecision)
        XCTAssertNil(launch.weatherProbabilityPercent)
    }
    func testStaleAndOfflineLaunchesDoNotClaimALiveCountdown() throws {
        let launch = try XCTUnwrap(snapshot(stale: true).presentations.first)
        XCTAssertFalse(launch.hasCountdownPrecision)
        XCTAssertEqual(launch.statusAbbreviation, "STALE")
        let offline = try XCTUnwrap(snapshot().presentations.first).stalePresentation
        XCTAssertFalse(offline.hasCountdownPrecision)
        XCTAssertEqual(offline.statusAbbreviation, "STALE")
    }
    func testUnavailableLaunchNeverBecomesAPreviewMission() throws {
        XCTAssertTrue(try snapshot(available: false).presentations.isEmpty)
        XCTAssertTrue(TrackedLaunch.unavailable.id.isEmpty)
        XCTAssertFalse(TrackedLaunch.unavailable.hasCountdownPrecision)
    }
    func testSyncPrescribesAgenciesWithoutRuntimeSnapshots() throws {
        let data = try DeviceClient().encodedLaunchPreferences(agencies: [.spacex, .nasa])
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(Set(json.keys), Set(["style", "launchAgencies"]))
        XCTAssertEqual(json["launchAgencies"] as? [String], ["nasa", "spacex"])
        XCTAssertThrowsError(try DeviceClient().encodedLaunchPreferences(agencies: []))
        XCTAssertTrue(SharedConfiguration.Ownership.standard.device.contains("launchRuntime"))
        XCTAssertTrue(SharedConfiguration.Ownership.standard.companion.contains("launchAgencies"))
    }
    func testUSBLaunchPackagePreservesEveryTrackedFlightSlot() {
        XCTAssertEqual(USBConfigurationClient.packageRetainedSlotMask(assets: [], launchPackage: true), 15)
        XCTAssertEqual(USBConfigurationClient.packageRetainedSlotMask(assets: [], launchPackage: false), 0)
    }
}
