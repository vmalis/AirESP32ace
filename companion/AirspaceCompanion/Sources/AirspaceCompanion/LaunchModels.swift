// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

enum LaunchAgency: String, Codable, CaseIterable {
    case nasa
    case spacex
    case roscosmos

    var displayName: String {
        switch self {
        case .nasa: "NASA"
        case .spacex: "SPACEX"
        case .roscosmos: "ROSCOSMOS"
        }
    }
}

enum LaunchVehicleKind: String, Codable, CaseIterable {
    case sls
    case falcon9
    case soyuz
    case starship

    var displayName: String {
        switch self {
        case .sls: "SLS / ORION"
        case .falcon9: "FALCON 9 / DRAGON"
        case .soyuz: "SOYUZ MS"
        case .starship: "STARSHIP"
        }
    }
}

enum LaunchSiteKind: String, Codable, CaseIterable {
    case florida
    case starbase
    case baikonur

    var displayName: String {
        switch self {
        case .florida: "FLORIDA SPACE COAST"
        case .starbase: "STARBASE, TEXAS"
        case .baikonur: "BAIKONUR COSMODROME"
        }
    }

    var coordinate: CLLocationCoordinate2D {
        switch self {
        case .florida:
            CLLocationCoordinate2D(
                latitude: 28.6083,
                longitude: -80.6041
            )
        case .starbase:
            CLLocationCoordinate2D(
                latitude: 25.9971,
                longitude: -97.1566
            )
        case .baikonur:
            CLLocationCoordinate2D(
                latitude: 45.9960,
                longitude: 63.5640
            )
        }
    }

    var mapRadiusKilometers: Double {
        482.803
    }

    var timeZoneIdentifier: String {
        switch self {
        case .florida: "America/New_York"
        case .starbase: "America/Chicago"
        case .baikonur: "Asia/Qyzylorda"
        }
    }

    var assetName: String {
        "launch_\(rawValue)"
    }
}

struct TrackedLaunch: Codable, Equatable, Identifiable {
    let id: String
    let missionName: String
    let fullName: String
    let agency: LaunchAgency
    let vehicle: LaunchVehicleKind
    let site: LaunchSiteKind
    let padName: String
    let locationName: String
    let net: Date
    let precisionName: String
    let precisionAbbreviation: String
    let statusName: String
    let statusAbbreviation: String
    let watchURL: URL?
    let lastUpdated: Date?
    let currentUTCOffsetSeconds: Int
    let weatherProbabilityPercent: Int?

    var hasCountdownPrecision: Bool {
        let precision =
            precisionName.lowercased()
        return precision.contains("second") ||
            precision.contains("minute") ||
            precision.contains("hour")
    }

    static let preview = TrackedLaunch(
        id: "preview-artemis",
        missionName: "ARTEMIS III",
        fullName: "SLS Block 1 | Artemis III",
        agency: .nasa,
        vehicle: .sls,
        site: .florida,
        padName: "LC-39B",
        locationName: "Kennedy Space Center",
        net: Date().addingTimeInterval(64 * 86_400 + 8_437),
        precisionName: "Hour",
        precisionAbbreviation: "H",
        statusName: "Go for Launch",
        statusAbbreviation: "GO",
        watchURL: URL(string: "https://www.nasa.gov/nasatv/"),
        lastUpdated: Date(),
        currentUTCOffsetSeconds: -14_400,
        weatherProbabilityPercent: 82
    )
}
