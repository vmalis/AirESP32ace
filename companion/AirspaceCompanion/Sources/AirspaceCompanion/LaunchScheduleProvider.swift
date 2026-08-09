// AirESP32ace
// Created by VM, August 2026

import Foundation

struct LaunchScheduleProvider: Sendable {
    private static let nasaURL =
        "https://ll.thespacedevs.com/2.3.0/launches/upcoming/" +
        "?format=json&limit=8&mode=detailed&search=Artemis"
    private static let roscosmosURL =
        "https://ll.thespacedevs.com/2.3.0/launches/upcoming/" +
        "?format=json&limit=8&mode=detailed&search=Soyuz%20MS"
    private static let starshipURL =
        "https://ll.thespacedevs.com/2.3.0/launches/upcoming/" +
        "?format=json&limit=8&mode=detailed&" +
        "rocket__configuration__full_name__icontains=Starship"

    func fetchNextLaunch(
        session: URLSession = .shared
    ) async throws -> TrackedLaunch {
        let launches = try await fetchNextLaunches(
            session: session
        )
        guard let launch = launches.min(
            by: { $0.net < $1.net }
        ) else {
            throw LaunchScheduleError.noMatchingLaunch
        }
        return launch
    }

    func fetchNextLaunches(
        session: URLSession = .shared
    ) async throws -> [TrackedLaunch] {
        async let nasa = fetchDetailedListIfAvailable(
            Self.nasaURL,
            session: session
        )
        async let roscosmos = fetchDetailedListIfAvailable(
            Self.roscosmosURL,
            session: session
        )
        async let starship = fetchDetailedListIfAvailable(
            Self.starshipURL,
            session: session
        )

        let groups = await (nasa, roscosmos, starship)
        let selected: [DetailedLaunch] = [
            groups.0.first {
                $0.name.localizedCaseInsensitiveContains(
                    "Artemis"
                )
            },
            groups.1.first {
                $0.name.localizedCaseInsensitiveContains(
                    "Soyuz MS"
                )
            },
            groups.2.first {
                $0.name.localizedCaseInsensitiveContains(
                    "Starship"
                )
            },
        ].compactMap { $0 }
        guard !selected.isEmpty else {
            throw LaunchScheduleError.noMatchingLaunch
        }

        var launches: [TrackedLaunch] = []
        for detail in selected {
            let site = Self.siteKind(
                location:
                    detail.pad?.location?.name ?? "",
                longitude: detail.pad?.longitude,
                vehicle: Self.vehicleKind(
                    configuration:
                        detail.rocket?.configuration?.fullName ??
                        detail.rocket?.configuration?.name ??
                        detail.name,
                    launchName: detail.name
                )
            )
            let weatherProbability =
                try? await fetchWeatherProbability(
                for: site,
                at: detail.net,
                session: session
            )
            launches.append(
                try map(
                    detail,
                    weatherProbabilityPercent:
                        weatherProbability ?? nil
                )
            )
        }
        guard !launches.isEmpty else {
            throw LaunchScheduleError.noMatchingLaunch
        }
        return launches.sorted { $0.net < $1.net }
    }

    private func fetchDetailedListIfAvailable(
        _ urlString: String,
        session: URLSession
    ) async -> [DetailedLaunch] {
        let response: LaunchDetailedListResponse? =
            try? await fetch(
                urlString,
                session: session
            )
        return response?.results ?? []
    }

    private func fetch<Value: Decodable>(
        _ urlString: String,
        session: URLSession
    ) async throws -> Value {
        guard let url = URL(string: urlString) else {
            throw LaunchScheduleError.badURL
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 18
        request.setValue(
            "AirspaceCompanion/0.1 personal-display",
            forHTTPHeaderField: "User-Agent"
        )
        let (data, response) =
            try await session.data(for: request)
        guard
            let http = response as? HTTPURLResponse,
            (200..<300).contains(http.statusCode)
        else {
            throw LaunchScheduleError.badResponse
        }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return try decoder.decode(Value.self, from: data)
    }

    private func map(
        _ launch: DetailedLaunch,
        weatherProbabilityPercent: Int?
    ) throws -> TrackedLaunch {
        let configuration =
            launch.rocket?.configuration?.fullName ??
            launch.rocket?.configuration?.name ??
            launch.name
        let agencyName =
            launch.launchServiceProvider?.name ?? ""
        let vehicle = Self.vehicleKind(
            configuration: configuration,
            launchName: launch.name
        )
        let agency = Self.agencyKind(
            provider: agencyName,
            vehicle: vehicle
        )
        let locationName =
            launch.pad?.location?.name ?? ""
        let site = Self.siteKind(
            location: locationName,
            longitude: launch.pad?.longitude,
            vehicle: vehicle
        )
        let missionName = launch.name
            .split(separator: "|", maxSplits: 1)
            .last?
            .trimmingCharacters(
                in: .whitespacesAndNewlines
            )
            .uppercased() ?? launch.name.uppercased()
        let watchURL =
            launch.videoURLs?
                .compactMap(\.bestURL)
                .first ??
            launch.videoURL

        return TrackedLaunch(
            id: launch.id,
            missionName: missionName,
            fullName: launch.name,
            agency: agency,
            vehicle: vehicle,
            site: site,
            padName: launch.pad?.name ?? "PAD TBD",
            locationName:
                locationName.isEmpty
                    ? site.displayName
                    : locationName,
            net: launch.net,
            precisionName:
                launch.netPrecision?.name ?? "Unknown",
            precisionAbbreviation:
                launch.netPrecision?.abbreviation ?? "",
            statusName:
                launch.status?.name ?? "Status pending",
            statusAbbreviation:
                launch.status?.abbreviation ?? "TBD",
            watchURL: watchURL,
            lastUpdated: launch.lastUpdated,
            currentUTCOffsetSeconds:
                TimeZone(
                    identifier:
                        site.timeZoneIdentifier
                )?.secondsFromGMT() ?? 0,
            weatherProbabilityPercent:
                weatherProbabilityPercent
        )
    }

    private func fetchWeatherProbability(
        for site: LaunchSiteKind,
        at launchDate: Date,
        session: URLSession
    ) async throws -> Int? {
        let remaining =
            launchDate.timeIntervalSinceNow
        guard remaining >= 0,
              remaining <= 16 * 86_400
        else {
            return nil
        }
        let coordinate = site.coordinate
        var components = URLComponents(
            string:
                "https://api.open-meteo.com/v1/forecast"
        )
        components?.queryItems = [
            URLQueryItem(
                name: "latitude",
                value: String(
                    format: "%.5f",
                    coordinate.latitude
                )
            ),
            URLQueryItem(
                name: "longitude",
                value: String(
                    format: "%.5f",
                    coordinate.longitude
                )
            ),
            URLQueryItem(
                name: "hourly",
                value: "precipitation_probability"
            ),
            URLQueryItem(
                name: "forecast_days",
                value: "16"
            ),
            URLQueryItem(
                name: "timezone",
                value: "UTC"
            ),
        ]
        guard let url = components?.url else {
            throw LaunchScheduleError.badURL
        }
        let response: LaunchWeatherResponse =
            try await fetch(
                url.absoluteString,
                session: session
            )
        let formatter = DateFormatter()
        formatter.locale = Locale(
            identifier: "en_US_POSIX"
        )
        formatter.timeZone = TimeZone(
            secondsFromGMT: 0
        )
        formatter.dateFormat = "yyyy-MM-dd'T'HH:mm"

        var best: (distance: TimeInterval, value: Int)?
        for (index, timestamp) in
            response.hourly.time.enumerated() {
            guard
                index <
                    response.hourly
                        .precipitationProbability.count,
                let date = formatter.date(
                    from: timestamp
                ),
                let value =
                    response.hourly
                        .precipitationProbability[index]
            else {
                continue
            }
            let distance = abs(
                date.timeIntervalSince(launchDate)
            )
            if best == nil ||
                distance < best!.distance {
                best = (distance, value)
            }
        }
        return best?.value
    }

    private static func vehicleKind(
        configuration: String,
        launchName: String
    ) -> LaunchVehicleKind {
        let value =
            "\(configuration) \(launchName)".lowercased()
        if value.contains("starship") {
            return .starship
        }
        if value.contains("soyuz") {
            return .soyuz
        }
        if value.contains("sls") ||
            value.contains("space launch system") ||
            value.contains("artemis") {
            return .sls
        }
        return .falcon9
    }

    private static func agencyKind(
        provider: String,
        vehicle: LaunchVehicleKind
    ) -> LaunchAgency {
        let normalized = provider.lowercased()
        if normalized.contains("roscosmos") ||
            normalized.contains("russian") ||
            vehicle == .soyuz {
            return .roscosmos
        }
        if normalized.contains("spacex") ||
            vehicle == .falcon9 ||
            vehicle == .starship {
            return .spacex
        }
        return .nasa
    }

    private static func siteKind(
        location: String,
        longitude: Double?,
        vehicle: LaunchVehicleKind
    ) -> LaunchSiteKind {
        let normalized = location.lowercased()
        if normalized.contains("baikonur") ||
            vehicle == .soyuz {
            return .baikonur
        }
        if normalized.contains("starbase") ||
            normalized.contains("boca chica") ||
            (longitude ?? 0) < -90 ||
            vehicle == .starship {
            return .starbase
        }
        return .florida
    }
}

private struct LaunchWeatherResponse: Decodable {
    struct Hourly: Decodable {
        let time: [String]
        let precipitationProbability: [Int?]

        enum CodingKeys: String, CodingKey {
            case time
            case precipitationProbability =
                "precipitation_probability"
        }
    }

    let hourly: Hourly
}

private struct LaunchDetailedListResponse: Decodable {
    let results: [DetailedLaunch]
}

private struct DetailedLaunch: Decodable {
    struct Status: Decodable {
        let name: String
        let abbreviation: String

        enum CodingKeys: String, CodingKey {
            case name
            case abbreviation = "abbrev"
        }
    }

    struct Precision: Decodable {
        let name: String
        let abbreviation: String

        enum CodingKeys: String, CodingKey {
            case name
            case abbreviation = "abbrev"
        }
    }

    struct Location: Decodable {
        let name: String
    }

    struct Pad: Decodable {
        let name: String
        let latitude: Double?
        let longitude: Double?
        let location: Location?
    }

    struct Configuration: Decodable {
        let name: String?
        let fullName: String?

        enum CodingKeys: String, CodingKey {
            case name
            case fullName = "full_name"
        }
    }

    struct Rocket: Decodable {
        let configuration: Configuration?
    }

    struct Agency: Decodable {
        let name: String
    }

    struct Video: Decodable {
        let url: URL?
        let directURL: URL?

        enum CodingKeys: String, CodingKey {
            case url
            case directURL = "direct_url"
        }

        var bestURL: URL? {
            directURL ?? url
        }
    }

    let id: String
    let name: String
    let net: Date
    let lastUpdated: Date?
    let status: Status?
    let netPrecision: Precision?
    let pad: Pad?
    let rocket: Rocket?
    let launchServiceProvider: Agency?
    let videoURLs: [Video]?
    let videoURL: URL?

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case net
        case lastUpdated = "last_updated"
        case status
        case netPrecision = "net_precision"
        case pad
        case rocket
        case launchServiceProvider =
            "launch_service_provider"
        case videoURLs = "vid_urls"
        case videoURL = "video_url"
    }
}

enum LaunchScheduleError: LocalizedError {
    case badURL
    case badResponse
    case noMatchingLaunch

    var errorDescription: String? {
        switch self {
        case .badURL:
            "The launch-feed URL is invalid."
        case .badResponse:
            "The launch feed did not return a usable response."
        case .noMatchingLaunch:
            "No upcoming crewed or Starship launch was found."
        }
    }
}
