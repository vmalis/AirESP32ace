// AirESP32ace
// Created by VM, August 2026

import Foundation

struct SharedConfiguration: Codable, Equatable, Sendable {
    static let schemaVersion = 1

    struct Ownership: Codable, Equatable, Sendable {
        let device: [String]
        let companion: [String]

        static let standard = Ownership(
            device: [
                "trackedFlightRuntime",
                "launchRuntime",
                "activeWifiProfile",
                "location",
                "display.aircraftLabels",
                "display.radarWedge",
                "display.issFlags",
                "display.weatherUnits",
                "display.accentColor",
                "display.brightnessPercent",
                "radarRadiusMiles",
            ],
            companion: [
                "wifiProfiles",
                "credentials",
                "display.accentColors",
                "radarCities",
                "radarLocations",
                "clockCities",
                "launchAgencies",
                "dockSchedule",
                "mainScreens",
                "trackedFlights",
                "activeTrackedFlightId",
            ]
        )
    }

    struct Device: Codable, Equatable, Sendable {
        struct WiFiProfiles: Codable, Equatable, Sendable {
            let home: WiFiProfile
            let hotspot: WiFiProfile
        }

        struct WiFiProfile: Codable, Equatable, Sendable {
            let ssid: String
            let hasPassword: Bool
        }

        struct Location: Codable, Equatable, Sendable {
            let key: String
            let label: String
            let lat: String
            let lon: String
            let timezone: String
            let metar: String
            let radarHalfDegrees: String
        }

        struct Display: Codable, Equatable, Sendable {
            let aircraftLabels: Bool
            let radarWedge: Bool
            var issFlags: Bool? = nil
            var weatherUnits: String? = nil
            let accentColor: String
            var accentColors: [String: String]? = nil
            var brightnessPercent: Int? = nil
        }

        struct Services: Codable, Equatable, Sendable {
            let openSkyReady: Bool
            let airLabsReady: Bool
            let flightAwareReady: Bool
        }

        let revision: UInt32
        let activeWifiProfile: String
        let wifiProfiles: WiFiProfiles
        let location: Location
        let display: Display
        let radarRadiusMiles: Int
        let services: Services
    }

    struct Companion: Codable, Equatable, Sendable {
        var radarCities: [String]?
        var radarLocations: [LocationDefinition]?
        var clockCities: [String]
        var launchAgencies: [String]?
        var dockSchedule: [DockScheduleEntry]?
        var mainScreens: [MainScreen]?
        var trackedFlights: [TrackedFlightPlan]
        var activeTrackedFlightId: UUID?
        var lastSyncedDeviceRevision: UInt32
    }

    struct LocationDefinition: Codable, Equatable, Sendable {
        var key: String
        var label: String
        var lat: String
        var lon: String
        var timezone: String
        var metar: String
        var radarHalfDegrees: String
    }

    enum DockScreen: String, Codable, CaseIterable, Identifiable, Sendable {
        case radar
        case tracker
        case weather
        case clock
        case solar
        case eclipse
        case moon
        case iss
        case launchNasa
        case launchRoscosmos
        case launchSpacex
        case trackerFrame
        case trackerDetail
        case weatherForecast
        case weatherRadar
        case clockWorld
        case launchNasaMap
        case launchRoscosmosMap
        case launchSpacexMap

        var id: String { rawValue }

        var title: String {
            switch self {
            case .radar: "Radar"
            case .tracker: "Flight Tracker"
            case .weather: "Weather"
            case .clock: "Clock"
            case .solar: "Solar System"
            case .eclipse: "Solar Eclipse"
            case .moon: "Lunar Phase"
            case .iss: "ISS"
            case .launchNasa: "NASA"
            case .launchRoscosmos: "Roscosmos"
            case .launchSpacex: "SpaceX"
            case .trackerFrame: "Flight Tracker · Straight-line Route"
            case .trackerDetail: "Flight Tracker · Detail"
            case .weatherForecast: "Weather · Forecast"
            case .weatherRadar: "Weather · Radar"
            case .clockWorld: "Clock · World Time"
            case .launchNasaMap: "NASA · Map"
            case .launchRoscosmosMap: "Roscosmos · Map"
            case .launchSpacexMap: "SpaceX · Map"
            }
        }

    }

    enum MainScreen: String, Codable, CaseIterable, Identifiable, Sendable {
        case radar
        case tracker
        case weather
        case clock
        case space
        case launches

        var id: String { rawValue }

        var title: String {
            switch self {
            case .radar: "Radar"
            case .tracker: "Flight Tracker"
            case .weather: "Weather"
            case .clock: "GMT Clock"
            case .space: "Space"
            case .launches: "Space Launches"
            }
        }
    }

    struct DockScheduleEntry: Codable, Equatable, Identifiable, Sendable {
        var screen: DockScreen
        var durationSeconds: Int

        var id: DockScreen { screen }
    }

    struct TrackedFlightPlan: Codable, Equatable, Sendable {
        let id: UUID
        var flightNumber: String
        var departureDateUnix: Int64
        let addedAtUnix: Int64
        var scheduledDepartureUnix: Int64?
        var actualDepartureUnix: Int64?
        var estimatedArrivalUnix: Int64?
        var actualArrivalUnix: Int64?
        var lastRefreshedUnix: Int64?
        var statusSummary: String
        var isComplete: Bool
        var mapsPreparedAtUnix: Int64?
        var deviceSlot: Int?

        init(_ plan: PlannedFlight) {
            id = plan.id
            flightNumber = plan.flightNumber
            departureDateUnix = Self.unix(plan.departureDate)
            addedAtUnix = Self.unix(plan.addedAt)
            scheduledDepartureUnix = plan.scheduledDeparture.map(Self.unix)
            actualDepartureUnix = plan.actualDeparture.map(Self.unix)
            estimatedArrivalUnix = plan.estimatedArrival.map(Self.unix)
            actualArrivalUnix = plan.actualArrival.map(Self.unix)
            lastRefreshedUnix = plan.lastRefreshed.map(Self.unix)
            statusSummary = plan.statusSummary
            isComplete = plan.isComplete
            mapsPreparedAtUnix = plan.mapsPreparedAt.map(Self.unix)
            deviceSlot = plan.deviceSlot
        }

        var plannedFlight: PlannedFlight {
            PlannedFlight(
                id: id,
                flightNumber: flightNumber,
                departureDate: Self.date(departureDateUnix),
                addedAt: Self.date(addedAtUnix),
                scheduledDeparture: scheduledDepartureUnix.map(Self.date),
                actualDeparture: actualDepartureUnix.map(Self.date),
                estimatedArrival: estimatedArrivalUnix.map(Self.date),
                actualArrival: actualArrivalUnix.map(Self.date),
                lastRefreshed: lastRefreshedUnix.map(Self.date),
                statusSummary: statusSummary,
                isComplete: isComplete,
                mapsPreparedAt: mapsPreparedAtUnix.map(Self.date),
                deviceSlot: deviceSlot
            )
        }

        private static func unix(_ date: Date) -> Int64 {
            Int64(date.timeIntervalSince1970.rounded())
        }

        private static func date(_ unix: Int64) -> Date {
            Date(timeIntervalSince1970: TimeInterval(unix))
        }
    }

    let schemaVersion: Int
    let savedAtUnix: Int64
    let reconnecting: Bool
    let conflict: Bool
    let ownership: Ownership
    let device: Device
    var companion: Companion
}

enum SharedConfigurationStore {
    static var fileURL: URL {
        let base = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory
        return base
            .appendingPathComponent(
                "AirspaceCompanion",
                isDirectory: true
            )
            .appendingPathComponent("airspace_config.json")
    }

    static func load() -> SharedConfiguration? {
        guard let data = try? Data(contentsOf: fileURL) else {
            return nil
        }
        return try? JSONDecoder().decode(
            SharedConfiguration.self,
            from: data
        )
    }

    static func save(_ configuration: SharedConfiguration) throws {
        let directory = fileURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(configuration)
        try data.write(to: fileURL, options: .atomic)
    }
}
