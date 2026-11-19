// AirESP32ace
// Created by VM, August 2026

import Foundation

struct DeviceClient: Sendable {
    private let controlTimeout: TimeInterval = 8
    private let bitmapTimeout: TimeInterval = 25

    struct StatePayload: Encodable {
        struct RoutePoint: Encodable {
            let x: Int
            let y: Int
        }

        struct Flight: Encodable {
            let number: String
            let callsign: String
            let airlineIata: String
            let airlineName: String
            let aircraftType: String
            let aircraftTypeCode: String?
            let origin: String
            let originCity: String
            let originCountryCode: String?
            let destination: String
            let destinationCity: String
            let destinationCountryCode: String?
            let originTerminal: String?
            let originGate: String?
            let destinationTerminal: String?
            let destinationGate: String?
            let status: String
            let duration: String
            let distance: String
            let eta: String
            let elapsed: String
            let remaining: String
            let altitudeMeters: Int?
            let speedKPH: Int?
            let headingDegrees: Int?
            let phase: String
            let journeyPhase: String
            let delayed: Bool
            let routeDistanceKilometers: Int?
            let originCurrentUTCOffsetSeconds: Int?
            let destinationCurrentUTCOffsetSeconds: Int?
            let departureUTCOffsetSeconds: Int?
            let arrivalUTCOffsetSeconds: Int?
            let estimatedDepartureUnix: Int64?
            let actualDepartureUnix: Int64?
            let estimatedArrivalUnix: Int64?
            let actualArrivalUnix: Int64?
        }

        let style: String
        let deviceSlot: Int
        let trackingActive: Bool
        let routeProgress: Double
        let routePoints: [RoutePoint]
        let flight: Flight
    }

    private struct ClearTrackedFlightPayload: Encodable {
        let style: String
        let trackingActive: Bool
        let routeProgress: Double
        let routePoints: [StatePayload.RoutePoint]
    }

    private struct RouteGeometryPayload: Encodable {
        let routePoints: [StatePayload.RoutePoint]
    }

    private struct LaunchStatePayload: Encodable {
        struct Launch: Encodable {
            let available: Bool
            let id: String
            let missionName: String
            let fullName: String
            let agency: String
            let vehicle: String
            let site: String
            let padName: String
            let locationName: String
            let precisionName: String
            let precisionAbbreviation: String
            let statusName: String
            let statusAbbreviation: String
            let watchURL: String
            let netUnix: Int64
            let updatedUnix: Int64
            let currentUTCOffsetSeconds: Int
            let weatherProbabilityPercent: Int
        }

        let style: String
        let launch: Launch
        let launches: [Launch]
    }

    struct DeviceConfigPayload: Encodable {
        struct WiFiProfiles: Encodable {
            let home: WiFiProfile
            let hotspot: WiFiProfile
        }

        struct WiFiProfile: Encodable {
            let ssid: String?
            let password: String?
        }

        struct Location: Encodable {
            let key: String?
            let label: String
            let lat: String
            let lon: String
            let timezone: String
            let metar: String
            let radarHalfDegrees: String
        }

        struct OpenSky: Encodable {
            let enabled: Bool
            let clientId: String?
            let clientSecret: String?
        }

        struct AirLabs: Encodable {
            let apiKey: String?
        }

        struct FlightAware: Encodable {
            let available: Bool?
            var apiKey: String? = nil
        }

        struct Display: Encodable {
            let aircraftLabels: Bool
            let radarWedge: Bool
            let issFlags: Bool
            let weatherUnits: String
            let accentColor: String
            let accentColors: [String: String]
        }

        struct Device: Encodable {
            let activeWifiProfile: String
            let wifiProfiles: WiFiProfiles
            let location: Location
            let display: Display
            let radarRadiusMiles: Int
        }

        struct Credentials: Encodable {
            let openSky: OpenSky
            let airLabs: AirLabs
            let flightAware: FlightAware
        }

        let schemaVersion: Int
        let baseDeviceRevision: UInt32
        let device: Device
        let credentials: Credentials
        let companion: SharedConfiguration.Companion
    }

    func fetchStatus(host: String) async throws -> String {
        let url = try url(host: host, path: "/api/status")
        var request = URLRequest(url: url)
        request.timeoutInterval = controlTimeout
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return String(data: data, encoding: .utf8) ?? "{}"
    }

    func fetchTracker(host: String) async throws -> DeviceTrackerSnapshot {
        try JSONDecoder().decode(DeviceTrackerSnapshot.self, from: await fetchTrackerData(host: host))
    }

    func fetchTrackerData(host: String) async throws -> Data {
        var request = URLRequest(url: try url(host: host, path: "/api/tracker"))
        request.timeoutInterval = controlTimeout
        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await PairedDeviceSession.current().data(for: request)
        } catch {
            if CommandLine.arguments.contains("--trace-sync") {
                let failure = error as NSError
                FileHandle.standardError.write(Data(
                    "Tracker read failed at \(request.url?.host ?? "device")/api/tracker: \(failure.domain) \(failure.code)\n".utf8
                ))
                if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? NSError {
                    FileHandle.standardError.write(Data("Network detail: \(underlying.domain) \(underlying.code)\n".utf8))
                }
            }
            throw error
        }
        try validate(response: response, data: data)
        return data
    }

    func fetchLaunches(host: String) async throws -> DeviceLaunchSnapshot {
        var request = URLRequest(url: try url(host: host, path: "/api/launches"))
        request.timeoutInterval = controlTimeout
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(DeviceLaunchSnapshot.self, from: data)
    }

    func encodedLaunchPreferences(agencies: Set<LaunchAgency>) throws -> Data {
        guard !agencies.isEmpty else { throw CocoaError(.validationMissingMandatoryProperty) }
        return try JSONSerialization.data(withJSONObject: [
            "style": "launch", "launchAgencies": agencies.map(\.rawValue).sorted()
        ], options: [.sortedKeys])
    }

    func pushLaunchPreferences(host: String, data: Data) async throws {
        // Same acknowledged state transport, without any launch runtime snapshot.
        try await pushTrackerQueue(host: host, data: data)
    }

    /// The ESP32 has no worldwide timezone database. Prescribe actual offset
    /// changes from Foundation once; delayed ETDs/ETAs and the local clocks
    /// can then be recalculated by the device without another Mac sync.
    static func offsetSteps(zoneID: String?, from start: Date, through end: Date) throws -> [[Int64]] {
        guard let zoneID, let zone = TimeZone(identifier: zoneID) else { return [] }
        var cursor = start
        var steps = [[Int64(cursor.timeIntervalSince1970), Int64(zone.secondsFromGMT(for: cursor))]]
        while let next = zone.nextDaylightSavingTimeTransition(after: cursor),
              next > cursor, next <= end {
            let offset = Int64(zone.secondsFromGMT(for: next.addingTimeInterval(1)))
            if offset != steps.last?[1] {
                guard steps.count < 12 else { throw CocoaError(.validationMissingMandatoryProperty) }
                steps.append([Int64(next.timeIntervalSince1970), offset])
            }
            cursor = next.addingTimeInterval(1)
        }
        return steps
    }

    func encodedTrackerQueue(
        flights: [(plan: PlannedFlight, flight: TrackedFlight)],
        credentials: DeviceConfigPayload.Credentials
    ) throws -> Data {
        guard flights.count <= 4 else { throw CocoaError(.validationMissingMandatoryProperty) }
        var queue: [[String: Any]] = []
        for (plan, flight) in flights {
            guard let departure = plan.legSelection?.scheduledDeparture ?? flight.scheduledDeparture,
                  let slot = plan.deviceSlot, (0...3).contains(slot) else {
                throw CocoaError(.validationMissingMandatoryProperty)
            }
            let seed = try encodedState(style: .cartographicGlass,
                routeProgress: flight.routeProgress ?? 0, flight: flight, deviceSlot: slot)
            var item = try JSONSerialization.jsonObject(with: seed) as! [String: Any]
            let start = min(Date(), departure).addingTimeInterval(-2 * 86400)
            let end = max(Date(), flight.estimatedArrival ?? departure).addingTimeInterval(14 * 86400)
            item["tracking"] = [
                "planId": plan.id.uuidString,
                "providerId": plan.selectedProviderIdentifier?.hasPrefix("airlabs-") == true
                    ? "" : (plan.selectedProviderIdentifier ?? ""),
                "scheduledDepartureUnix": Int64(departure.timeIntervalSince1970),
                "originLatitude": flight.origin.coordinate.latitude,
                "originLongitude": flight.origin.coordinate.longitude,
                "destinationLatitude": flight.destination.coordinate.latitude,
                "destinationLongitude": flight.destination.coordinate.longitude,
                "originOffsetSteps": try Self.offsetSteps(zoneID: flight.origin.timeZoneIdentifier, from: start, through: end),
                "destinationOffsetSteps": try Self.offsetSteps(zoneID: flight.destination.timeZoneIdentifier, from: start, through: end),
            ]
            queue.append(item)
        }
        return try JSONSerialization.data(withJSONObject: [
            "style": InstrumentStyle.cartographicGlass.rawValue,
            "trackerQueue": queue,
            "trackerCredentials": try JSONSerialization.jsonObject(with: JSONEncoder().encode(credentials)),
        ], options: [.sortedKeys])
    }

    func pushTrackerQueue(host: String, data: Data) async throws {
        var request = URLRequest(url: try url(host: host, path: "/api/state"))
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = data
        let (body, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: body)
    }

    enum RouteMapView: String, Sendable {
        case globe
        case frame
    }

    enum RouteEndpoint: String, Sendable {
        case origin
        case destination
    }

    struct StatusSnapshot: Decodable, Sendable {
        let ok: Bool?
        let ip: String?
        let trackedFlightValid: Bool
        let trackedFlightNumber: String
        let routeMapMask: Int
        let routeFlagMask: Int?
        let launchAssetMask: Int?
        let spaceAssetFileMask: Int?
        var radarMapInventory: String? = nil
        var locationKey: String? = nil
        var lat: String? = nil
        var lon: String? = nil
        var radarRadiusMiles: Int? = nil
        var storageFreeBytes: Int? = nil
        var storageWriteErrno: Int? = nil
        var weatherCityIconMask: Int? = nil
        let batteryAvailable: Bool?
        let batteryPresent: Bool?
        let batteryCharging: Bool?
        let batteryPercent: Int?
        let lastPushUnix: Int?
    }

    struct AircraftSnapshot: Decodable, Equatable, Sendable {
        struct Aircraft: Decodable, Equatable, Sendable, Identifiable {
            let icao24: String
            let callsign: String
            let typecode: String
            let model: String
            let registration: String
            let manufacturer: String
            let engineType: String
            let engineCount: Int
            let typeSource: String
            let airlineName: String
            let airlineIata: String
            let airlineIcao: String
            let origin: String
            let destination: String
            let originCity: String
            let destinationCity: String
            let metadataValid: Bool
            let routeValid: Bool
            let routeProgressValid: Bool
            let routeProgress: Double
            let routeDistanceNm: Double
            let routeDistanceCoveredNm: Double
            let routeDistanceRemainingNm: Double
            let iconLabel: String
            let latitude: Double
            let longitude: Double
            let altitudeFt: Int
            let speedKt: Int
            let trackDeg: Int
            let trackValid: Bool
            let verticalRateMps: Double
            let verticalRateValid: Bool
            let positionUnix: Int
            let contactUnix: Int
            let category: Int
            let distanceNm: Double
            let bearingDeg: Int

            var id: String { icao24 }
        }

        let ok: Bool
        let aircraftValid: Bool
        let aircraftCount: Int
        let aircraftRawCount: Int
        let aircraftUsableCount: Int
        let aircraftHttpStatus: Int
        let aircraftRadiusMiles: Double
        let aircraftAuthUsed: Bool
        let fetchedUnix: Int
        let nowUnix: Int
        let aircraft: [Aircraft]
    }

    struct WeatherSnapshot: Decodable, Equatable, Sendable {
        struct ForecastDay: Decodable, Equatable, Sendable, Identifiable {
            let day: String
            let code: Int
            let high: Int
            let low: Int
            let precipitationPercent: Int

            var id: String { day }
        }

        let ok: Bool
        let weatherValid: Bool
        let airQualityValid: Bool
        let locationKey: String
        let fetchedUnix: Int
        let temperature: Int
        let apparentTemperature: Int
        let code: Int
        let humidityPercent: Int
        let pressureHPA: Int
        let precipitationTenthsMM: Int
        let uvTenths: Int
        let aqi: Int
        let windKT: Int
        let windDegrees: Int
        let unit: String
        let summary: String
        let wind: String
        let sunrise: String
        let sunset: String
        let forecast: [ForecastDay]
    }

    func pushMap(
        host: String,
        rgb565: Data,
        view: RouteMapView = .globe,
        slot: Int? = nil,
        progress: (@Sendable (_ sent: Int64, _ total: Int64) -> Void)? = nil
    ) async throws {
        var path = "/api/map?view=\(view.rawValue)"
        if let slot {
            path += "&slot=\(max(0, min(3, slot)))"
        }
        let url = try url(host: host, path: path)
        try await pushBitmap(
            url: url,
            rgb565: rgb565,
            progress: progress
        )
    }

    func prepareTrackedFlightPackage(host: String) async throws {
        let url = try url(host: host, path: "/api/map?prepare=1")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type")
        request.httpBody = Data()
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
    }

    func pushRouteFlag(
        host: String,
        rgb565: Data,
        endpoint: RouteEndpoint,
        slot: Int? = nil,
        progress: (@Sendable (_ sent: Int64, _ total: Int64) -> Void)? = nil
    ) async throws {
        var path = "/api/flag?endpoint=\(endpoint.rawValue)"
        if let slot {
            path += "&slot=\(max(0, min(3, slot)))"
        }
        let url = try url(
            host: host,
            path: path
        )
        try await pushBitmap(
            url: url,
            rgb565: rgb565,
            progress: progress
        )
    }

    func pushSpaceAsset(
        host: String,
        name: String,
        data: Data
    ) async throws {
        let url = try url(
            host: host,
            path:
                "/api/space-asset?name=" +
                Self.urlEncode(name)
        )
        try await pushBitmap(url: url, rgb565: data)
    }

    func pushRadarMap(host: String, rgb565: Data, rangeMiles: Double = 30, locationKey: String? = nil) async throws {
        var path = "/api/radar-map?range=\(Int(rangeMiles))"
        if let locationKey, !locationKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            path += "&location=\(Self.urlEncode(locationKey))"
        }
        let url = try url(host: host, path: path)
        try await pushBitmap(url: url, rgb565: rgb565)
    }

    func pushWeatherCityIcon(
        host: String,
        data: Data,
        locationKey: String
    ) async throws {
        let url = try url(
            host: host,
            path: "/api/weather-city-icon?location=" +
                Self.urlEncode(locationKey)
        )
        try await pushBitmap(url: url, rgb565: data)
    }

    private func pushBitmap(
        url: URL,
        rgb565: Data,
        progress: (@Sendable (_ sent: Int64, _ total: Int64) -> Void)? = nil
    ) async throws {
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        let standaloneArtwork = ["/api/radar-map", "/api/weather-city-icon", "/api/space-asset"].contains(url.path)
        request.timeoutInterval = standaloneArtwork ? 120 : bitmapTimeout
        let hash = rgb565.reduce(UInt32(2166136261)) { ($0 ^ UInt32($1)) &* 16777619 }
        request.setValue(String(format: "%08x",hash), forHTTPHeaderField: "X-Asset-FNV32")
        request.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type")
        request.setValue("\(rgb565.count)", forHTTPHeaderField: "Content-Length")

        let data: Data
        let response: URLResponse
        if let progress {
            let delegate = BitmapUploadProgressDelegate(progress: progress)
            (data, response) = try await PairedDeviceSession.current().upload(
                for: request,
                from: rgb565,
                delegate: delegate
            )
        } else {
            (data, response) = try await PairedDeviceSession.current().upload(
                for: request,
                from: rgb565
            )
        }
        try validate(response: response, data: data)
        progress?(Int64(rgb565.count), Int64(rgb565.count))
    }

    func encodedState(
        style: InstrumentStyle,
        routeProgress: Double,
        flight: TrackedFlight,
        trackingActive: Bool = true,
        deviceSlot: Int = 0
    ) throws -> Data {
        // Switching to Radar, including after Rebuild Maps, is navigation.
        // Never publish the Mac's cached flight or touch the device's queue.
        if style == .xdrRadar {
            return try JSONEncoder().encode(["style": style.rawValue])
        }
        let flight = flight.reconciledTerminalState()
        let airlineIata = String(
            flight.marketingNumber
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .uppercased()
                .prefix(2)
        )
        let delayed = flight.isDepartureDelayed
        let now = Date()
        let originTimeZone = flight.origin.timeZoneIdentifier.flatMap(
            TimeZone.init(identifier:)
        )
        let destinationTimeZone = flight.destination.timeZoneIdentifier.flatMap(
            TimeZone.init(identifier:)
        )
        let estimatedDeparture =
            flight.estimatedDeparture ??
            flight.scheduledDeparture
        let estimatedArrival =
            flight.actualArrival ??
            flight.estimatedArrival
        let routePoints =
            MapKitFlightMapRenderer.routeScreenPoints(
                for: flight
            ).map {
                StatePayload.RoutePoint(
                    x: max(0, min(465, Int(round($0.x)))),
                    y: max(0, min(465, Int(round($0.y))))
                )
            }
        let payload = StatePayload(
            style: style.rawValue,
            deviceSlot: max(0, min(3, deviceSlot)),
            trackingActive: trackingActive,
            routeProgress: flight.journeyPhase == "preflight" ? 0 : routeProgress,
            routePoints: routePoints,
            flight: .init(
                number: flight.marketingNumber,
                callsign: flight.operationalCallsign,
                airlineIata: airlineIata,
                airlineName: flight.airlineName,
                aircraftType: flight.aircraftType,
                aircraftTypeCode: flight.aircraftTypeCode,
                origin: flight.origin.code,
                originCity: flight.origin.displayCity,
                originCountryCode: AirportCityCatalog.flagCountry(for: flight.origin),
                destination: flight.destination.code,
                destinationCity: flight.destination.displayCity,
                destinationCountryCode: AirportCityCatalog.flagCountry(for: flight.destination),
                originTerminal: flight.originTerminal,
                originGate: flight.originGate,
                destinationTerminal: flight.destinationTerminal,
                destinationGate: flight.destinationGate,
                status: flight.liveStatusSummary,
                duration: flight.durationSummary,
                distance: flight.distanceSummary,
                eta: flight.etaSummary ?? "-",
                elapsed: flight.elapsedSummary ?? "-",
                remaining: flight.remainingSummary ?? "-",
                altitudeMeters: flight.altitudeMeters,
                speedKPH: flight.groundSpeedKPH,
                headingDegrees: flight.headingDegrees,
                phase: flight.verticalPhase ?? "unknown",
                journeyPhase: flight.journeyPhase,
                delayed: delayed,
                routeDistanceKilometers: flight.routeDistanceKilometers,
                originCurrentUTCOffsetSeconds:
                    originTimeZone?.secondsFromGMT(for: now),
                destinationCurrentUTCOffsetSeconds:
                    destinationTimeZone?.secondsFromGMT(for: now),
                departureUTCOffsetSeconds:
                    estimatedDeparture.flatMap {
                        originTimeZone?.secondsFromGMT(for: $0)
                    },
                arrivalUTCOffsetSeconds:
                    estimatedArrival.flatMap {
                        destinationTimeZone?.secondsFromGMT(for: $0)
                    },
                estimatedDepartureUnix:
                    estimatedDeparture.map {
                        Int64($0.timeIntervalSince1970.rounded())
                    },
                actualDepartureUnix:
                    flight.actualDeparture.map {
                        Int64($0.timeIntervalSince1970.rounded())
                    },
                estimatedArrivalUnix:
                    flight.estimatedArrival.map {
                        Int64($0.timeIntervalSince1970.rounded())
                    },
                actualArrivalUnix:
                    flight.actualArrival.map {
                        Int64($0.timeIntervalSince1970.rounded())
                    }
            )
        )
        return try JSONEncoder().encode(payload)
    }

    func pushState(
        host: String,
        style: InstrumentStyle,
        routeProgress: Double,
        flight: TrackedFlight,
        trackingActive: Bool = true,
        deviceSlot: Int = 0
    ) async throws {
        let url = try url(host: host, path: "/api/state")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let body = try encodedState(
            style: style,
            routeProgress: routeProgress,
            flight: flight,
            trackingActive: trackingActive,
            deviceSlot: deviceSlot
        )
        let (data, response) = try await PairedDeviceSession.current().upload(for: request, from: body)
        try validate(response: response, data: data)
    }

    func clearTrackedFlight(
        host: String,
        style: InstrumentStyle = .cartographicGlass
    ) async throws {
        let url = try url(host: host, path: "/api/state")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let payload = ClearTrackedFlightPayload(
            style: style.rawValue,
            trackingActive: false,
            routeProgress: 0,
            routePoints: []
        )
        let body = try JSONEncoder().encode(payload)
        let (data, response) = try await PairedDeviceSession.current().upload(
            for: request,
            from: body
        )
        try validate(response: response, data: data)
    }

    func pushRouteGeometry(
        host: String,
        flight: TrackedFlight
    ) async throws {
        let url = try url(host: host, path: "/api/state")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue(
            "application/json",
            forHTTPHeaderField: "Content-Type"
        )
        let routePoints =
            MapKitFlightMapRenderer.routeScreenPoints(
                for: flight
            ).map {
                StatePayload.RoutePoint(
                    x: max(0, min(465, Int(round($0.x)))),
                    y: max(0, min(465, Int(round($0.y))))
                )
            }
        let body = try JSONEncoder().encode(
            RouteGeometryPayload(routePoints: routePoints)
        )
        let (data, response) = try await PairedDeviceSession.current().upload(
            for: request,
            from: body
        )
        try validate(response: response, data: data)
    }

    func pushLaunchState(
        host: String,
        launch: TrackedLaunch,
        launches: [TrackedLaunch],
        style: String = "launch"
    ) async throws {
        let url = try url(
            host: host,
            path: "/api/state"
        )
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue(
            "application/json",
            forHTTPHeaderField: "Content-Type"
        )
        func payloadLaunch(
            _ launch: TrackedLaunch
        ) -> LaunchStatePayload.Launch {
            .init(
                available: true,
                id: launch.id,
                missionName: launch.missionName,
                fullName: launch.fullName,
                agency: launch.agency.rawValue,
                vehicle: launch.vehicle.rawValue,
                site: launch.site.rawValue,
                padName: launch.padName,
                locationName: launch.locationName,
                precisionName: launch.precisionName,
                precisionAbbreviation:
                    launch.precisionAbbreviation,
                statusName: launch.statusName,
                statusAbbreviation:
                    launch.statusAbbreviation,
                watchURL:
                    launch.watchURL?.absoluteString ?? "",
                netUnix: Int64(
                    launch.net.timeIntervalSince1970
                        .rounded()
                ),
                updatedUnix: Int64(
                    (launch.lastUpdated ?? Date())
                        .timeIntervalSince1970
                        .rounded()
                ),
                currentUTCOffsetSeconds:
                    launch.currentUTCOffsetSeconds,
                weatherProbabilityPercent:
                    launch.weatherProbabilityPercent ?? -1
            )
        }
        let payload = LaunchStatePayload(
            style: style,
            launch: payloadLaunch(launch),
            launches: launches.map(payloadLaunch)
        )
        let body = try JSONEncoder().encode(payload)
        let (data, response) =
            try await PairedDeviceSession.current().upload(
                for: request,
                from: body
            )
        try validate(
            response: response,
            data: data
        )
    }

    func fetchStatusSnapshot(host: String, mapInventory: Bool = false) async throws -> StatusSnapshot {
        let url = try url(host: host, path: mapInventory ? "/api/status?mapInventory=1" : "/api/status")
        var request = URLRequest(url: url)
        request.timeoutInterval = controlTimeout
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(StatusSnapshot.self, from: data)
    }

    func fetchAircraftSnapshot(host: String) async throws -> AircraftSnapshot {
        let url = try url(host: host, path: "/api/aircraft")
        var request = URLRequest(url: url)
        request.timeoutInterval = 3
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(AircraftSnapshot.self, from: data)
    }

    func fetchWeatherSnapshot(host: String) async throws -> WeatherSnapshot {
        let url = try url(host: host, path: "/api/weather")
        var request = URLRequest(url: url)
        request.timeoutInterval = 3
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(WeatherSnapshot.self, from: data)
    }

    func fetchConfig(host: String) async throws -> SharedConfiguration {
        let url = try url(host: host, path: "/api/config")
        var request = URLRequest(url: url)
        request.timeoutInterval = controlTimeout
        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(SharedConfiguration.self, from: data)
    }

    func pushConfig(
        host: String,
        payload: DeviceConfigPayload
    ) async throws -> SharedConfiguration {
        let url = try url(host: host, path: "/api/config")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body = try JSONEncoder().encode(payload)
        let (data, response) = try await PairedDeviceSession.current().upload(for: request, from: body)
        try validate(response: response, data: data)
        return try JSONDecoder().decode(SharedConfiguration.self, from: data)
    }

    func refreshDeviceData(host: String) async throws -> String {
        let url = try url(host: host, path: "/api/data/refresh")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = controlTimeout

        let (data, response) = try await PairedDeviceSession.current().data(for: request)
        try validate(response: response, data: data)
        return String(data: data, encoding: .utf8) ?? "{}"
    }

    func url(host: String, path: String) throws -> URL {
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        let supplied = trimmed.contains("://") ? trimmed : "https://\(trimmed)"
        guard var component = URLComponents(string: supplied),
              ["http","https"].contains(component.scheme?.lowercased() ?? ""),
              component.host?.isEmpty == false, component.user == nil, component.password == nil,
              component.path.isEmpty || component.path == "/", component.query == nil, component.fragment == nil
        else { throw DeviceClientError.badHost(host) }
        component.scheme = "https"
        component.path = ""
        guard path.hasPrefix("/api/"), !path.contains("\\"), let base = component.url,
              let url = URL(string: path, relativeTo: base)?.absoluteURL, url.host == base.host else {
            throw DeviceClientError.badHost(host)
        }
        return url
    }

    private static func urlEncode(_ value: String) -> String {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }

    private func validate(response: URLResponse, data: Data) throws {
        guard let http = response as? HTTPURLResponse else {
            throw DeviceClientError.badResponse
        }
        guard (200..<300).contains(http.statusCode) else {
            let body = String(data: data, encoding: .utf8) ?? ""
            throw DeviceClientError.httpStatus(http.statusCode, body)
        }
    }
}

/// Bridges URLSession's upload callbacks into the companion's aggregate
/// tracked-flight progress. Reporting only whole-percent changes keeps SwiftUI
/// responsive while large RGB565 maps are in flight.
private final class BitmapUploadProgressDelegate: NSObject,
    URLSessionTaskDelegate,
    @unchecked Sendable
{
    private let progress: @Sendable (_ sent: Int64, _ total: Int64) -> Void
    private let lock = NSLock()
    private var lastPercent = -1

    init(
        progress: @escaping @Sendable (_ sent: Int64, _ total: Int64) -> Void
    ) {
        self.progress = progress
    }

    func urlSession(
        _ session: URLSession,
        task: URLSessionTask,
        willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest,
        completionHandler: @escaping (URLRequest?) -> Void
    ) { completionHandler(nil) }

    func urlSession(
        _ session: URLSession,
        task: URLSessionTask,
        didSendBodyData bytesSent: Int64,
        totalBytesSent: Int64,
        totalBytesExpectedToSend: Int64
    ) {
        let total = max(totalBytesExpectedToSend, totalBytesSent, 1)
        let percent = Int((Double(totalBytesSent) / Double(total) * 100).rounded(.down))
        lock.lock()
        let shouldReport = percent != lastPercent
        if shouldReport {
            lastPercent = percent
        }
        lock.unlock()
        if shouldReport {
            progress(totalBytesSent, total)
        }
    }
}

enum DeviceClientError: LocalizedError {
    case badHost(String)
    case badResponse
    case httpStatus(Int, String)

    var errorDescription: String? {
        switch self {
        case .badHost(let host):
            "Bad device host: \(host)"
        case .badResponse:
            "Device returned a non-HTTP response."
        case .httpStatus(let status, let body):
            "Device returned HTTP \(status): \(body)"
        }
    }
}
