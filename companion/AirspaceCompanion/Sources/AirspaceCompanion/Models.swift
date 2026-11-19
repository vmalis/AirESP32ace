// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

struct Airport: Identifiable, Hashable, Codable {
    var id: String { code }
    let code: String
    let name: String
    let city: String
    let coordinate: CLLocationCoordinate2D
    var countryCode: String? = nil
    var timeZoneIdentifier: String? = nil

    /// Some airport-provider tiers omit `city`. Keep older saved plans usable
    /// without another provider request or adding a Radar city to the device.
    var displayCity: String {
        let value = city.trimmingCharacters(in: .whitespacesAndNewlines)
        let airportCode = code.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        if !value.isEmpty && value.uppercased() != airportCode { return value }
        return AirportCityCatalog.city(for: self) ?? ""
    }

    static let icn = Airport(
        code: "ICN",
        name: "Incheon International",
        city: "Seoul",
        coordinate: CLLocationCoordinate2D(latitude: 37.4602, longitude: 126.4407),
        countryCode: "KR",
        timeZoneIdentifier: "Asia/Seoul"
    )

    static let sfo = Airport(
        code: "SFO",
        name: "San Francisco International",
        city: "San Francisco",
        coordinate: CLLocationCoordinate2D(latitude: 37.6213, longitude: -122.3790),
        countryCode: "US",
        timeZoneIdentifier: "America/Los_Angeles"
    )
}

extension CLLocationCoordinate2D: @retroactive Hashable {
    public static func == (lhs: CLLocationCoordinate2D, rhs: CLLocationCoordinate2D) -> Bool {
        lhs.latitude == rhs.latitude && lhs.longitude == rhs.longitude
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(latitude)
        hasher.combine(longitude)
    }
}

extension CLLocationCoordinate2D: @retroactive Codable {
    private enum CodingKeys: String, CodingKey {
        case latitude
        case longitude
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        self.init(
            latitude: try values.decode(Double.self, forKey: .latitude),
            longitude: try values.decode(Double.self, forKey: .longitude)
        )
    }

    public func encode(to encoder: Encoder) throws {
        var values = encoder.container(keyedBy: CodingKeys.self)
        try values.encode(latitude, forKey: .latitude)
        try values.encode(longitude, forKey: .longitude)
    }
}

struct TrackedFlight: Identifiable, Hashable, Codable {
    var id: String { marketingNumber }
    let airlineName: String
    let marketingNumber: String
    let operationalCallsign: String
    let aircraftType: String
    var aircraftTypeCode: String? = nil
    let origin: Airport
    let destination: Airport
    let scheduleSummary: String
    let durationSummary: String
    let distanceSummary: String
    let liveStatusSummary: String
    let dataSourceSummary: String
    var routeProgress: Double?
    var lastPosition: CLLocationCoordinate2D?
    let lastUpdated: Date?
    var altitudeMeters: Int? = nil
    var groundSpeedKPH: Int? = nil
    var headingDegrees: Int? = nil
    var verticalPhase: String? = nil
    var etaSummary: String? = nil
    var elapsedSummary: String? = nil
    var remainingSummary: String? = nil
    var scheduledDeparture: Date? = nil
    var estimatedDeparture: Date? = nil
    var actualDeparture: Date? = nil
    var estimatedArrival: Date? = nil
    var actualArrival: Date? = nil
    var routeDistanceKilometers: Int? = nil
    var originTerminal: String? = nil
    var originGate: String? = nil
    var destinationTerminal: String? = nil
    var destinationGate: String? = nil

    var journeyPhase: String {
        journeyPhase(at: Date())
    }

    func journeyPhase(at now: Date) -> String {
        if liveStatusSummary.localizedCaseInsensitiveContains("cancelled") ||
            liveStatusSummary.localizedCaseInsensitiveContains("canceled") { return "cancelled" }
        if actualArrival != nil ||
            liveStatusSummary.localizedCaseInsensitiveContains("arrived") ||
            liveStatusSummary.localizedCaseInsensitiveContains("landed") {
            return "arrived"
        }
        if liveStatusSummary.localizedCaseInsensitiveContains("diverted") { return "diverted" }
        if hasInferredTerminalState(at: now) {
            return "arrived"
        }
        if actualDeparture != nil ||
            liveStatusSummary.localizedCaseInsensitiveContains("en route") ||
            liveStatusSummary.localizedCaseInsensitiveContains("airborne") {
            return "enroute"
        }
        return "preflight"
    }

    var isActivelyTrackable: Bool {
        guard journeyPhase != "arrived", journeyPhase != "cancelled" else { return false }
        let normalizedStatus = liveStatusSummary.lowercased()
        return !normalizedStatus.contains("arrived")
            && !normalizedStatus.contains("landed")
            && !normalizedStatus.contains("cancelled")
    }

    static let airPremiaYP111 = TrackedFlight(
        airlineName: "Air Premia",
        marketingNumber: "YP111",
        operationalCallsign: "APZ111",
        aircraftType: "Boeing 787-9",
        aircraftTypeCode: "B789",
        origin: .icn,
        destination: .sfo,
        scheduleSummary: "ICN → SFO · Mon/Wed/Fri/Sun",
        durationSummary: "~ 11h",
        distanceSummary: "~ 5,677 mi",
        liveStatusSummary: "Static route preset",
        dataSourceSummary: "Local preset",
        routeProgress: nil,
        lastPosition: nil,
        lastUpdated: nil
    )
}

extension TrackedFlight {
    // Match tracked_should_show_countdown in the shared device renderer.
    static let livePresentationLeadInterval: TimeInterval = 3600

    func trackerNotice(at now: Date = Date()) -> String? {
        let phase = journeyPhase(at: now)
        if phase == "cancelled" { return "Flight cancelled" }
        if liveStatusSummary.localizedCaseInsensitiveContains("diversion") ||
            liveStatusSummary.localizedCaseInsensitiveContains("diverted") {
            return phase == "arrived" ? "Landed after diversion" : "Flight diverted"
        }
        if phase == "preflight", let departure = estimatedDeparture ?? scheduledDeparture,
           departure <= now { return "Awaiting departure" }
        return nil
    }

    func showsScheduledCountdown(at now: Date = Date()) -> Bool {
        if trackerNotice(at: now) != nil { return true }
        guard journeyPhase(at: now) == "preflight",
              let departure = estimatedDeparture ?? scheduledDeparture else { return false }
        return departure.timeIntervalSince(now) > Self.livePresentationLeadInterval
    }

    var isDepartureDelayed: Bool {
        if liveStatusSummary.localizedCaseInsensitiveContains("delay") { return true }
        guard let scheduledDeparture, let estimatedDeparture else { return false }
        return estimatedDeparture.timeIntervalSince(scheduledDeparture) > 60
    }

    static let postArrivalRetentionInterval: TimeInterval = 2 * 3600
    static let endpointArrivalGraceInterval: TimeInterval = 30 * 60
    static let unconfirmedArrivalTimeoutInterval: TimeInterval = 24 * 3600

    /// The tracker remains available briefly after touchdown so the landed
    /// state can still be reviewed. Prefer the provider's actual arrival and
    /// fall back to its ETA when the free-tier response omits that timestamp.
    func postArrivalExpiryDate(at now: Date = Date()) -> Date? {
        guard journeyPhase(at: now) == "arrived" else { return nil }
        guard let arrival = actualArrival ?? estimatedArrival ?? lastUpdated else {
            return nil
        }
        return arrival.addingTimeInterval(Self.postArrivalRetentionInterval)
    }

    func isExpiredAfterArrival(at now: Date = Date()) -> Bool {
        guard let expiry = postArrivalExpiryDate(at: now) else { return false }
        return now >= expiry
    }

    /// Some free-tier provider records can keep reporting `en route` after
    /// their last live occurrence has ended.  Treat a flight as terminal only
    /// when the selected occurrence has started, its route has reached the
    /// destination, and its best ETA has been behind us for a conservative
    /// grace period.  This deliberately does not trust the status string or
    /// schedule clock alone, which protects delayed flights. Provider-backed
    /// terminal validation handles flights whose final position was missed.
    /// As a final offline/restart safeguard, an occurrence more than 24 hours
    /// beyond its ETA is terminal even if its last persisted route sample was
    /// fractional. This prevents a days-old snapshot from owning the Tracker
    /// forever while leaving a full day for exceptional operational delays.
    func hasInferredTerminalState(
        at now: Date,
        gracePeriod: TimeInterval = Self.endpointArrivalGraceInterval
    ) -> Bool {
        guard actualArrival == nil,
              let expectedArrival = estimatedArrival
        else { return false }

        let normalizedStatus = liveStatusSummary.lowercased()
        guard !normalizedStatus.contains("cancelled"),
              !normalizedStatus.contains("canceled")
        else { return false }

        let occurrenceStarted = actualDeparture != nil ||
            normalizedStatus.contains("en route") ||
            normalizedStatus.contains("enroute") ||
            normalizedStatus.contains("airborne")
        guard occurrenceStarted else { return false }

        let timeSinceArrival = now.timeIntervalSince(expectedArrival)
        return ((routeProgress ?? 0) >= 0.995 &&
            timeSinceArrival >= gracePeriod) ||
            timeSinceArrival >= Self.unconfirmedArrivalTimeoutInterval
    }

    /// Reconciles the inferred phase into concrete values before the flight is
    /// cached, rendered, or sent to the instrument.  Every tracked-flight view
    /// therefore receives the same landed state.
    func reconciledTerminalState(at now: Date = Date()) -> TrackedFlight {
        let phase = journeyPhase(at: now)
        if phase == "preflight" || phase == "cancelled" || phase == "diverted" ||
            liveStatusSummary.localizedCaseInsensitiveContains("after diversion") {
            // A future occurrence must not inherit today's aircraft motion
            // from a callsign/airframe lookup or an older persisted snapshot.
            var scheduled = self
            scheduled.routeProgress = 0
            scheduled.lastPosition = nil
            scheduled.altitudeMeters = nil
            scheduled.groundSpeedKPH = nil
            scheduled.headingDegrees = nil
            scheduled.verticalPhase = phase == "arrived" ? "landed" : nil
            return scheduled
        }
        guard phase == "arrived" else { return self }
        let normalizedStatus = liveStatusSummary.lowercased()
        let hasExplicitTerminalStatus =
            normalizedStatus.contains("arrived") ||
            normalizedStatus.contains("landed")
        return normalizedForTerminalState(
            status: hasExplicitTerminalStatus ? liveStatusSummary : "Arrived",
            at: actualArrival ?? estimatedArrival ?? now
        )
    }

    /// Makes the terminal state internally consistent at every provider and
    /// transport boundary. A landed flight must never retain an older
    /// airborne coordinate or a fractional route progress value.
    func normalizedForTerminalState(
        status: String = "Arrived",
        at resolvedArrival: Date = Date()
    ) -> TrackedFlight {
        TrackedFlight(
            airlineName: airlineName,
            marketingNumber: marketingNumber,
            operationalCallsign: operationalCallsign,
            aircraftType: aircraftType,
            aircraftTypeCode: aircraftTypeCode,
            origin: origin,
            destination: destination,
            scheduleSummary: scheduleSummary,
            durationSummary: durationSummary,
            distanceSummary: "0 km",
            liveStatusSummary: status,
            dataSourceSummary: dataSourceSummary,
            routeProgress: 1,
            lastPosition: destination.coordinate,
            lastUpdated: max(lastUpdated ?? .distantPast, resolvedArrival),
            altitudeMeters: 0,
            groundSpeedKPH: 0,
            headingDegrees: headingDegrees,
            verticalPhase: "landed",
            etaSummary: etaSummary,
            elapsedSummary: elapsedSummary,
            remainingSummary: "0m",
            scheduledDeparture: scheduledDeparture,
            estimatedDeparture: estimatedDeparture,
            actualDeparture: actualDeparture,
            estimatedArrival: estimatedArrival,
            actualArrival: actualArrival ?? resolvedArrival,
            routeDistanceKilometers: routeDistanceKilometers,
            originTerminal: originTerminal,
            originGate: originGate,
            destinationTerminal: destinationTerminal,
            destinationGate: destinationGate
        )
    }
}

struct PlannedFlight: Codable, Identifiable, Hashable {
    static let postArrivalRetentionInterval: TimeInterval = 2 * 3600
    static let unconfirmedArrivalTimeoutInterval: TimeInterval = 24 * 3600

    let id: UUID
    var flightNumber: String
    var departureDate: Date
    let addedAt: Date
    var scheduledDeparture: Date?
    var estimatedDeparture: Date? = nil
    var actualDeparture: Date?
    var estimatedArrival: Date?
    var actualArrival: Date?
    var lastRefreshed: Date?
    var statusSummary: String
    var isComplete: Bool
    /// Set only after both route maps and endpoint flags have been rendered.
    /// Optional keeps schedules saved by older app versions decodable.
    var mapsPreparedAt: Date?
    var deviceSlot: Int?
    var selectedOriginCode: String?
    var selectedDestinationCode: String?
    var selectedDeparture: Date?
    var selectedProviderIdentifier: String?

    init(
        id: UUID = UUID(),
        flightNumber: String,
        departureDate: Date,
        addedAt: Date = Date(),
        scheduledDeparture: Date? = nil,
        actualDeparture: Date? = nil,
        estimatedArrival: Date? = nil,
        actualArrival: Date? = nil,
        lastRefreshed: Date? = nil,
        statusSummary: String = "Waiting for first refresh",
        isComplete: Bool = false,
        mapsPreparedAt: Date? = nil,
        deviceSlot: Int? = nil,
        selectedOriginCode: String? = nil,
        selectedDestinationCode: String? = nil,
        selectedDeparture: Date? = nil,
        selectedProviderIdentifier: String? = nil
    ) {
        self.id = id
        self.flightNumber = flightNumber
        self.departureDate = departureDate
        self.addedAt = addedAt
        self.scheduledDeparture = scheduledDeparture
        self.actualDeparture = actualDeparture
        self.estimatedArrival = estimatedArrival
        self.actualArrival = actualArrival
        self.lastRefreshed = lastRefreshed
        self.statusSummary = statusSummary
        self.isComplete = isComplete
        self.mapsPreparedAt = mapsPreparedAt
        self.deviceSlot = deviceSlot
        self.selectedOriginCode = selectedOriginCode
        self.selectedDestinationCode = selectedDestinationCode
        self.selectedDeparture = selectedDeparture
        self.selectedProviderIdentifier = selectedProviderIdentifier
    }

    var mapsPrepared: Bool { mapsPreparedAt != nil }

    var legSelection: FlightLegSelection? {
        guard let selectedOriginCode,
              let selectedDestinationCode,
              let selectedDeparture
        else { return nil }
        return FlightLegSelection(
            providerIdentifier: selectedProviderIdentifier,
            originCode: selectedOriginCode,
            destinationCode: selectedDestinationCode,
            scheduledDeparture: selectedDeparture
        )
    }

    var effectiveDeparture: Date {
        actualDeparture
            ?? estimatedDeparture
            ?? scheduledDeparture
            ?? Calendar.current.date(
                byAdding: .hour,
                value: 12,
                to: Calendar.current.startOfDay(for: departureDate)
            )
            ?? departureDate
    }

    var effectiveArrival: Date {
        actualArrival
            ?? estimatedArrival
            ?? effectiveDeparture.addingTimeInterval(18 * 3600)
    }

    var isTerminal: Bool {
        if hasArrived || isCancelled {
            return true
        }
        return false
    }

    var hasArrived: Bool {
        if actualArrival != nil {
            return true
        }
        let normalized = statusSummary.lowercased()
        return normalized.contains("arrived") ||
            normalized.contains("landed") ||
            (isComplete && !isCancelled)
    }

    var isCancelled: Bool {
        let normalized = statusSummary.lowercased()
        return normalized.contains("cancelled") ||
            normalized.contains("canceled")
    }

    var arrivalReferenceDate: Date? {
        guard hasArrived else { return nil }
        return actualArrival ?? estimatedArrival ?? lastRefreshed
    }

    func isWithinPostArrivalRetention(at now: Date = Date()) -> Bool {
        guard let arrivalReferenceDate else { return hasArrived }
        return now < arrivalReferenceDate.addingTimeInterval(
            Self.postArrivalRetentionInterval
        )
    }

    func isExpiredAfterArrival(at now: Date = Date()) -> Bool {
        guard let arrivalReferenceDate else { return false }
        return hasArrived &&
            now >= arrivalReferenceDate.addingTimeInterval(
                Self.postArrivalRetentionInterval
            )
    }

    /// A provider or device can retain an unfinished in-flight snapshot when
    /// it remains offline through landing. Do not let that snapshot block the
    /// rest of the queue indefinitely after a later app restart.
    func hasExceededUnconfirmedArrivalWindow(
        at now: Date = Date()
    ) -> Bool {
        guard !isTerminal,
              actualDeparture != nil || effectiveDeparture <= now
        else { return false }
        return now >= effectiveArrival.addingTimeInterval(
            Self.unconfirmedArrivalTimeoutInterval
        )
    }
}

enum PlannedFlightPriority {
    static func select(
        from plans: [PlannedFlight],
        currentPlanID: UUID? = nil,
        at now: Date = Date()
    ) -> PlannedFlight? {
        let available = plans.filter {
            !$0.isTerminal &&
                !$0.hasExceededUnconfirmedArrivalWindow(at: now)
        }
        let retained = plans.filter {
            ($0.hasArrived && $0.isWithinPostArrivalRetention(at: now)) ||
            ($0.isCancelled && ($0.lastRefreshed ?? .distantPast)
                .addingTimeInterval(PlannedFlight.postArrivalRetentionInterval) > now)
        }

        // Within the conservative stale-occurrence window, a flight keeps the
        // tracker until a provider explicitly reports a terminal state.
        // Schedule estimates must never make a newer departure steal an
        // aircraft that may still be airborne.
        if let currentPlanID,
           let current = available.first(where: { $0.id == currentPlanID }),
           current.actualDeparture != nil || current.effectiveDeparture <= now {
            return current
        }

        // A terminal notice remains available for two hours, but cannot
        // displace an airborne flight or an imminent connection.
        if !available.contains(where: {
            $0.actualDeparture != nil ||
                $0.effectiveDeparture <= now.addingTimeInterval(TrackedFlight.livePresentationLeadInterval)
        }), let notice = retained.max(by: {
            ($0.arrivalReferenceDate ?? $0.lastRefreshed ?? .distantPast) <
            ($1.arrivalReferenceDate ?? $1.lastRefreshed ?? .distantPast)
        }) { return notice }

        guard !available.isEmpty else { return nil }

        let underway = available.filter {
            $0.effectiveDeparture <= now && now < $0.effectiveArrival
        }
        if !underway.isEmpty {
            return underway.min {
                if $0.effectiveDeparture == $1.effectiveDeparture {
                    return $0.addedAt < $1.addedAt
                }
                return $0.effectiveDeparture < $1.effectiveDeparture
            }
        }

        let upcoming = available.filter { $0.effectiveDeparture > now }
        if !upcoming.isEmpty {
            return upcoming.min {
                if $0.effectiveDeparture == $1.effectiveDeparture {
                    return $0.addedAt < $1.addedAt
                }
                return $0.effectiveDeparture < $1.effectiveDeparture
            }
        }

        // A stale estimated arrival is not permission to advance. Prefer the
        // oldest unfinished flight until it is reported terminal or removed.
        return available.min {
            if $0.effectiveDeparture == $1.effectiveDeparture {
                return $0.addedAt < $1.addedAt
            }
            return $0.effectiveDeparture < $1.effectiveDeparture
        }
    }
}

enum PlannedFlightSchedule {
    static func conflict(
        for candidate: PlannedFlight,
        among plans: [PlannedFlight],
        excluding excludedID: UUID? = nil
    ) -> PlannedFlight? {
        let candidateStart = candidate.effectiveDeparture
        let candidateEnd = candidate.effectiveArrival
        return plans
            .filter { $0.id != excludedID && !$0.isTerminal }
            .sorted { $0.effectiveDeparture < $1.effectiveDeparture }
            .first {
                candidateStart < $0.effectiveArrival &&
                    $0.effectiveDeparture < candidateEnd
            }
    }
}

enum InstrumentStyle: String, CaseIterable, Identifiable {
    case xdrRadar
    case cartographicGlass
    case gmtChronograph
    case weather
    case space
    case launch

    var id: String { rawValue }

    var title: String {
        switch self {
        case .xdrRadar:
            "XDR Radar"
        case .cartographicGlass:
            "Cartographic Glass"
        case .gmtChronograph:
            "GMT Chronograph"
        case .weather:
            "Weather"
        case .space:
            "Space"
        case .launch:
            "Next Launch"
        }
    }

    var subtitle: String {
        switch self {
        case .xdrRadar:
            "dark local radar + weather glow"
        case .cartographicGlass:
            "real map under glass"
        case .gmtChronograph:
            "watch-dial time zones"
        case .weather:
            "conditions, forecast, and radar"
        case .space:
            "solar system, lunar phase, and ISS"
        case .launch:
            "crewed missions + Starship"
        }
    }
}

enum NearbyAircraftProvider: String, CaseIterable, Identifiable {
    case openSky

    var id: String { rawValue }

    var title: String {
        switch self {
        case .openSky:
            "OpenSky Network"
        }
    }

    var subtitle: String {
        switch self {
        case .openSky:
            "free nearby ADS-B/state vectors; OAuth client credentials optional but recommended"
        }
    }
}

enum DeviceWiFiProfile: String, CaseIterable, Identifiable {
    case home
    case hotspot

    var id: String { rawValue }

    var title: String {
        switch self {
        case .home:
            "Home"
        case .hotspot:
            "Hotspot"
        }
    }
}

enum DeviceAccentColor: String, CaseIterable, Identifiable {
    case green
    case orange
    case cyan
    case red

    var id: String { rawValue }

    var title: String {
        switch self {
        case .green:
            "Green"
        case .cyan:
            "Cyan"
        case .orange:
            "Orange"
        case .red:
            "Red"
        }
    }
}

enum WeatherDisplayUnits: String, CaseIterable, Identifiable {
    case celsius = "C"
    case fahrenheit = "F"

    var id: String { rawValue }

    var pressureUnit: String {
        switch self {
        case .celsius: "mmHg"
        case .fahrenheit: "kPa"
        }
    }

    func altitudeLabel(meters: Int?) -> String {
        guard let meters, meters > 0 else { return "—" }
        switch self {
        case .celsius:
            return "\(meters) m"
        case .fahrenheit:
            return "\(Int((Double(meters) / 0.3048).rounded())) ft"
        }
    }

    func speedLabel(kilometersPerHour: Int?) -> String {
        guard let kilometersPerHour, kilometersPerHour > 0 else { return "—" }
        switch self {
        case .celsius:
            return "\(kilometersPerHour) km/h"
        case .fahrenheit:
            return "\(Int((Double(kilometersPerHour) * 0.621371).rounded())) mph"
        }
    }
}

struct RadarLocationPreset: Identifiable, Hashable {
    var id: String { key }
    let key: String
    let title: String
    let coordinate: CLLocationCoordinate2D
    let timezone: String
    let metar: String
    let radarHalfDegrees: String

    init(
        key: String,
        title: String,
        coordinate: CLLocationCoordinate2D,
        timezone: String,
        metar: String,
        radarHalfDegrees: String
    ) {
        self.key = key
        self.title = title
        self.coordinate = coordinate
        self.timezone = timezone
        self.metar = metar
        self.radarHalfDegrees = radarHalfDegrees
    }

    static let defaultClockCityKeys = [
        "ICN", "MOW", "DUS", "JFK", "SJC",
    ]

    static let all: [RadarLocationPreset] = [
        .init(
            key: "SJC",
            title: "SJC",
            coordinate: CLLocationCoordinate2D(latitude: 37.3639, longitude: -121.9289),
            timezone: "PST8PDT,M3.2.0,M11.1.0",
            metar: "KSJC",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "SFO",
            title: "SFO",
            coordinate: CLLocationCoordinate2D(latitude: 37.6213, longitude: -122.3790),
            timezone: "PST8PDT,M3.2.0,M11.1.0",
            metar: "KSFO",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "SAN",
            title: "SAN",
            coordinate: CLLocationCoordinate2D(latitude: 32.7338, longitude: -117.1933),
            timezone: "PST8PDT,M3.2.0,M11.1.0",
            metar: "KSAN",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "JFK",
            title: "New York",
            coordinate: CLLocationCoordinate2D(latitude: 40.6413, longitude: -73.7781),
            timezone: "EST5EDT,M3.2.0,M11.1.0",
            metar: "KJFK",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "MOW",
            title: "Moscow",
            coordinate: CLLocationCoordinate2D(latitude: 55.7558, longitude: 37.6173),
            timezone: "MSK-3",
            metar: "UUEE",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "LED",
            title: "Saint Petersburg",
            coordinate: CLLocationCoordinate2D(latitude: 59.8003, longitude: 30.2625),
            timezone: "MSK-3",
            metar: "ULLI",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "DUS",
            title: "Dusseldorf",
            coordinate: CLLocationCoordinate2D(latitude: 51.2895, longitude: 6.7668),
            timezone: "CET-1CEST,M3.5.0,M10.5.0/3",
            metar: "EDDL",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "ICN",
            title: "Incheon",
            coordinate: CLLocationCoordinate2D(latitude: 37.4602, longitude: 126.4407),
            timezone: "KST-9",
            metar: "RKSI",
            radarHalfDegrees: "1.55"
        )
    ]

    // Mac-side choices only. The original eight remain the default device set.
    static let additionalChoices: [RadarLocationPreset] = [
        .init(
            key: "CLT", title: "Charlotte",
            coordinate: CLLocationCoordinate2D(latitude: 35.2138, longitude: -80.9491),
            timezone: "EST5EDT,M3.2.0,M11.1.0", metar: "KCLT",
            radarHalfDegrees: "1.55"
        ),
        .init(
            key: "CAE", title: "Columbia",
            coordinate: CLLocationCoordinate2D(latitude: 33.9388, longitude: -81.1195),
            timezone: "EST5EDT,M3.2.0,M11.1.0", metar: "KCAE",
            radarHalfDegrees: "1.55"
        ),
    ]

    static var catalog: [RadarLocationPreset] { all + additionalChoices }

    static func preset(for key: String) -> RadarLocationPreset? {
        catalog.first { $0.key.caseInsensitiveCompare(key) == .orderedSame }
    }

    static func preset(label: String, metar: String) -> RadarLocationPreset? {
        let normalizedLabel = label.trimmingCharacters(in: .whitespacesAndNewlines)
        let normalizedMetar = metar.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        return catalog.first {
            $0.key.caseInsensitiveCompare(normalizedLabel) == .orderedSame ||
            $0.title.caseInsensitiveCompare(normalizedLabel) == .orderedSame ||
            $0.metar.uppercased() == normalizedMetar
        }
    }

    init(definition: SharedConfiguration.LocationDefinition) {
        key = definition.key.uppercased()
        title = definition.label
        coordinate = CLLocationCoordinate2D(
            latitude: Double(definition.lat) ?? 0,
            longitude: Double(definition.lon) ?? 0
        )
        timezone = definition.timezone
        metar = definition.metar.uppercased()
        radarHalfDegrees = definition.radarHalfDegrees
    }

    var definition: SharedConfiguration.LocationDefinition {
        .init(
            key: key,
            label: title,
            lat: String(
                format: "%.6f",
                locale: Locale(identifier: "en_US_POSIX"),
                coordinate.latitude
            ),
            lon: String(
                format: "%.6f",
                locale: Locale(identifier: "en_US_POSIX"),
                coordinate.longitude
            ),
            timezone: timezone,
            metar: metar,
            radarHalfDegrees: radarHalfDegrees
        )
    }
}

enum RenderState: Equatable {
    case idle
    case rendering
    case rendered(URL)
    case failed(String)

    var message: String {
        switch self {
        case .idle:
            "Ready"
        case .rendering:
            "Rendering MapKit snapshot…"
        case .rendered(let url):
            "Saved \(url.lastPathComponent)"
        case .failed(let reason):
            reason
        }
    }
}

enum PushState: Equatable {
    case idle
    case pushing(String = "Pushing to device…")
    case pushed(String)
    case failed(String)

    var isPushing: Bool {
        if case .pushing = self {
            return true
        }
        return false
    }

    var message: String {
        switch self {
        case .idle:
            "Not pushed"
        case .pushing(let detail):
            detail
        case .pushed(let detail):
            detail
        case .failed(let reason):
            reason
        }
    }
}
