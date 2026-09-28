// AirESP32ace
// Created by VM, August 2026
import Foundation

struct DeviceLaunchSnapshot: Decodable, Sendable {
    struct Entry: Decodable, Sendable {
        let available: Bool
        let stale: Bool
        let checkedUnix: Double
        let httpStatus: Int
        let agency: String
        let id, missionName, fullName, vehicle, site, padName, locationName: String
        let precisionName, precisionAbbreviation, statusName, statusAbbreviation, watchURL: String
        let netUnix, updatedUnix: Double
        let weatherProbabilityPercent: Int

        var presentation: TrackedLaunch? {
            guard available, let agency = LaunchAgency(rawValue: agency),
                  let vehicle = LaunchVehicleKind(rawValue: vehicle),
                  let site = LaunchSiteKind(rawValue: site), netUnix > 0, !id.isEmpty else { return nil }
            return TrackedLaunch(id: id, missionName: missionName, fullName: fullName, agency: agency,
                vehicle: vehicle, site: site, padName: padName, locationName: locationName,
                net: Date(timeIntervalSince1970: netUnix),
                precisionName: stale ? "Unknown" : precisionName,
                precisionAbbreviation: stale ? "" : precisionAbbreviation,
                statusName: stale ? "STALE - awaiting update" : statusName,
                statusAbbreviation: stale ? "STALE" : statusAbbreviation,
                watchURL: URL(string: watchURL),
                lastUpdated: updatedUnix > 0 ? Date(timeIntervalSince1970: updatedUnix) : nil,
                currentUTCOffsetSeconds: TimeZone(identifier: site.timeZoneIdentifier)?.secondsFromGMT() ?? 0,
                weatherProbabilityPercent: (0...100).contains(weatherProbabilityPercent) ? weatherProbabilityPercent : nil)
        }
    }
    let deviceLaunchesSupported: Bool
    let enabledMask: Int
    let retryAfterUnix: Double
    let launches: [Entry]

    var presentations: [TrackedLaunch] { launches.compactMap(\.presentation).sorted { $0.net < $1.net } }
}

extension TrackedLaunch {
    static let unavailable = TrackedLaunch(id: "", missionName: "SCHEDULE PENDING", fullName: "Schedule pending",
        agency: .spacex, vehicle: .starship, site: .starbase, padName: "", locationName: "",
        net: Date(timeIntervalSince1970: 0), precisionName: "Unknown", precisionAbbreviation: "",
        statusName: "Waiting for device launch data", statusAbbreviation: "TBD", watchURL: nil,
        lastUpdated: nil, currentUTCOffsetSeconds: 0, weatherProbabilityPercent: nil)

    var stalePresentation: TrackedLaunch {
        TrackedLaunch(id: id, missionName: missionName, fullName: fullName, agency: agency,
            vehicle: vehicle, site: site, padName: padName, locationName: locationName,
            net: net, precisionName: "Unknown", precisionAbbreviation: "", statusName: "STALE - device unavailable",
            statusAbbreviation: "STALE", watchURL: watchURL, lastUpdated: lastUpdated,
            currentUTCOffsetSeconds: currentUTCOffsetSeconds, weatherProbabilityPercent: weatherProbabilityPercent)
    }
}
