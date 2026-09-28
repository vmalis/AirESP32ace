// AirESP32ace
// Created by VM, August 2026
import Foundation

// A framework-free check of the actual production models, also usable on a
// Mac with Command Line Tools but no XCTest installation.
@main
struct LaunchSnapshotSmoke {
    static func main() throws {
        let body = """
        {"deviceLaunchesSupported":true,"enabledMask":4,"retryAfterUnix":0,"launches":[{
          "available":true,"stale":false,"checkedUnix":1790604540,"httpStatus":200,
          "agency":"spacex","id":"7d1afb26-6f9c-429b-9ccf-29012fd1e519",
          "missionName":"STARLINK GROUP 31-1 (STARSHIP FLIGHT 14)","fullName":"Starship | Starlink Group 31-1 (Starship Flight 14)",
          "vehicle":"starship","site":"starbase","padName":"Orbital Launch Pad 2","locationName":"SpaceX Starbase, TX, USA",
          "precisionName":"Second","precisionAbbreviation":"SEC","statusName":"Payload Deployed","statusAbbreviation":"Deployed",
          "watchURL":"https://www.spacex.com/launches/","netUnix":1790599739,"updatedUnix":1790604486,
          "weatherProbabilityPercent":-1}]}
        """
        let decoder = JSONDecoder()
        let fresh = try decoder.decode(DeviceLaunchSnapshot.self, from: Data(body.utf8))
        precondition(fresh.deviceLaunchesSupported && fresh.enabledMask == 4)
        precondition(fresh.presentations.count == 1)
        let launch = fresh.presentations[0]
        precondition(launch.id == "7d1afb26-6f9c-429b-9ccf-29012fd1e519")
        precondition(launch.agency == .spacex && launch.vehicle == .starship && launch.site == .starbase)
        precondition(launch.statusName == "Payload Deployed" && launch.hasCountdownPrecision)
        precondition(launch.weatherProbabilityPercent == nil)
        precondition(ISO8601DateFormatter().string(from: launch.net) == "2026-09-28T12:48:59Z")
        let stale = try decoder.decode(DeviceLaunchSnapshot.self, from:
            Data(body.replacingOccurrences(of: "\"stale\":false", with: "\"stale\":true").utf8))
        precondition(stale.presentations[0].statusAbbreviation == "STALE")
        precondition(!stale.presentations[0].hasCountdownPrecision)
        precondition(!launch.stalePresentation.hasCountdownPrecision)
        let absent = try decoder.decode(DeviceLaunchSnapshot.self, from:
            Data(body.replacingOccurrences(of: "\"available\":true", with: "\"available\":false").utf8))
        precondition(absent.presentations.isEmpty)
        precondition(TrackedLaunch.unavailable.id.isEmpty && !TrackedLaunch.unavailable.hasCountdownPrecision)
        print("Launch snapshot smoke check passed: Flight 14, UTC, stale/offline, and unavailable states.")
    }
}
