// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

struct FlightAwareFlightStatusProvider: FlightLegProviding {
    let apiKey: String
    var session: URLSession = .shared
    var now: @Sendable () -> Date = { Date() }

    func trackedFlight(
        for marketingNumber: String,
        on selectedDate: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        let requestNow = now()
        let cacheKey = selection.map {
            FlightAwareTrackedFlightCache.key(
                apiKey: apiKey,
                marketingNumber: marketingNumber,
                selection: $0
            )
        }
        if let cacheKey,
           let cached = await FlightAwareTrackedFlightCache.shared.flight(
                for: cacheKey,
                now: requestNow
           ) {
            return cached
        }

        let ident = Self.flightAwareIdent(for: marketingNumber)
        let response = try await fetchFlights(
            ident: ident,
            selectedDate: selectedDate
        )

        guard var flight = selectBestFlight(
            from: response.flights,
            now: now(),
            selectedDate: selectedDate,
            requestedMarketingNumber: marketingNumber,
            requestedIdent: ident,
            selection: selection
        ) else {
            throw FlightProviderError.noLiveFlight(marketingNumber)
        }

        flight = await enrichExactOccurrenceIfUseful(flight)

        /* Keep the resolved occurrence immutable while these lookups run.
         * Swift 6 correctly rejects sharing this decoded reference graph
         * across several child tasks. These requests are small relative to
         * the provider call and sequencing them also makes cancellation and
         * free-tier request accounting deterministic. */
        let resolvedOrigin = try await resolveAirport(
            from: flight.origin,
            fallback: .icn
        )
        let resolvedDestination = try await resolveAirport(
            from: flight.destination,
            fallback: .sfo
        )
        let position = try await fetchPositionIfUseful(for: flight)
        try Self.validateDepartureDate(
            flight,
            selectedDate: selectedDate,
            origin: resolvedOrigin,
            marketingNumber: marketingNumber
        )
        let tracked = makeTrackedFlight(
            from: flight,
            requestedMarketingNumber: marketingNumber,
            position: position,
            origin: resolvedOrigin,
            destination: resolvedDestination
        )
        /* The one-minute airborne loop exists for smooth OpenSky telemetry,
         * not for one-minute AeroAPI polling. Reuse FlightAware's selected
         * occurrence, route, schedule and gate snapshot for ten minutes;
         * FallbackFlightStatusProvider still overlays fresh OpenSky motion on
         * every loop. Only an exact, user-selected occurrence is cached so
         * flight-leg discovery and manual selection never receive a stale
         * candidate list. */
        if tracked.journeyPhase == "enroute", let cacheKey {
            let closeToArrival = tracked.estimatedArrival.map {
                $0.timeIntervalSince(requestNow) <= 30 * 60
            } ?? false
            await FlightAwareTrackedFlightCache.shared.store(
                tracked,
                for: cacheKey,
                checkedAt: requestNow,
                validFor: closeToArrival ? 60 : 10 * 60
            )
        }
        return tracked
    }

    func flightLegs(
        for marketingNumber: String,
        on selectedDate: Date
    ) async throws -> [FlightLegCandidate] {
        let ident = Self.flightAwareIdent(for: marketingNumber)
        let response = try await fetchFlights(
            ident: ident,
            selectedDate: selectedDate
        )
        let requestedIdentifiers = Set([
            Self.normalizedIdentifier(ident),
            Self.normalizedMarketingIdentifier(marketingNumber),
            FlightNumberIdentity.canonical(marketingNumber),
        ])
        var legs: [FlightLegCandidate] = []
        for flight in response.flights where flight.cancelled != true {
            guard !Self.flightIdentifiers(flight).isDisjoint(
                with: requestedIdentifiers
            ),
            let departure = flight.bestDepartureDate,
            let originCode = flight.origin?.bestCode,
            let destinationCode = flight.destination?.bestCode
            else { continue }

            let origin = try? await resolveAirport(
                from: flight.origin,
                fallback: .icn
            )
            guard Self.isSameDepartureDay(
                departure,
                selectedDate: selectedDate,
                timeZoneIdentifier: origin?.timeZoneIdentifier
            ) else { continue }

            let normalizedOrigin = originCode.uppercased()
            let normalizedDestination = destinationCode.uppercased()
            let providerID = flight.faFlightID
            let identity = providerID ?? [
                FlightNumberIdentity.canonical(marketingNumber),
                normalizedOrigin,
                normalizedDestination,
                String(Int(departure.timeIntervalSince1970)),
            ].joined(separator: "|")
            let status = Self.effectiveStatus(for: flight, now: now())
            let complete = status == "Arrived"
            legs.append(FlightLegCandidate(
                id: identity,
                marketingNumber: FlightNumberIdentity.canonical(
                    marketingNumber
                ),
                originCode: normalizedOrigin,
                destinationCode: normalizedDestination,
                scheduledDeparture: departure,
                departureTimeZoneIdentifier: origin?.timeZoneIdentifier,
                statusSummary: status,
                isComplete: complete,
                providerIdentifier: providerID
            ))
        }
        return Array(Dictionary(grouping: legs, by: \.id).compactMap {
            $0.value.first
        }).sorted { $0.scheduledDeparture < $1.scheduledDeparture }
    }

    private func fetchFlights(
        ident: String,
        selectedDate: Date?
    ) async throws -> FlightsResponse {
        var queryItems = [URLQueryItem(name: "max_pages", value: "2")]
        if let selectedDate {
            let interval = Self.requestInterval(around: selectedDate)
            queryItems.append(URLQueryItem(
                name: "start",
                value: Self.requestDateString(interval.start)
            ))
            queryItems.append(URLQueryItem(
                name: "end",
                value: Self.requestDateString(interval.end)
            ))
        }
        return try await get("flights/\(ident)", queryItems: queryItems)
    }

    static func flightAwareIdent(for marketingNumber: String) -> String {
        let normalized = FlightNumberIdentity.canonical(marketingNumber)

        if normalized.hasPrefix("YP"),
           let flightNumber = Int(normalized.dropFirst(2)) {
            return "APZ\(flightNumber)"
        }

        return normalized
    }

    private func selectBestFlight(
        from flights: [FlightAwareFlight],
        now: Date,
        selectedDate: Date?,
        requestedMarketingNumber: String,
        requestedIdent: String,
        selection: FlightLegSelection?
    ) -> FlightAwareFlight? {
        let requestedIdentifiers = Set([
            Self.normalizedIdentifier(requestedIdent),
            Self.normalizedMarketingIdentifier(requestedMarketingNumber),
            FlightNumberIdentity.canonical(requestedMarketingNumber),
        ])
        let candidates = flights.filter {
            $0.cancelled != true &&
                Self.flightIdentifiers($0).isDisjoint(
                    with: requestedIdentifiers
                ) == false
        }

        if let selection,
           let exact = candidates.first(where: {
               Self.matches($0, selection: selection)
           }) {
            return exact
        }

        /* Once the user has chosen a concrete leg, never silently replace it
         * with another occurrence of the same number. That is how reused
         * callsigns and multi-leg flights could inherit the wrong route. */
        if selection != nil {
            return nil
        }

        if selectedDate != nil {
            let activeFlights = candidates.filter {
                $0.departedDate != nil && $0.arrivedDate == nil
            }
            if let active = activeFlights.max(by: {
                    ($0.departedDate ?? .distantPast) <
                        ($1.departedDate ?? .distantPast)
                }) {
                return active
            }
            let upcomingFlights = candidates.filter {
                ($0.bestDepartureDate ?? .distantPast) >=
                    now.addingTimeInterval(-30 * 60)
            }
            if let upcoming = upcomingFlights.min(by: {
                    ($0.bestDepartureDate ?? .distantFuture) <
                        ($1.bestDepartureDate ?? .distantFuture)
                }) {
                return upcoming
            }
            return candidates.max {
                ($0.bestDepartureDate ?? .distantPast) <
                    ($1.bestDepartureDate ?? .distantPast)
            }
        }

        if let active = candidates
            .filter({ $0.departedDate != nil && $0.arrivedDate == nil })
            .sorted(by: { ($0.departedDate ?? .distantPast) > ($1.departedDate ?? .distantPast) })
            .first {
            return active
        }

        if let upcoming = candidates
            .filter({ ($0.bestDepartureDate ?? .distantPast) >= now.addingTimeInterval(-2 * 3600) })
            .sorted(by: { ($0.bestDepartureDate ?? .distantFuture) < ($1.bestDepartureDate ?? .distantFuture) })
            .first {
            return upcoming
        }

        return candidates
            .sorted(by: { ($0.bestDepartureDate ?? .distantPast) > ($1.bestDepartureDate ?? .distantPast) })
            .first
    }

    private static func flightIdentifiers(
        _ flight: FlightAwareFlight
    ) -> Set<String> {
        Set([flight.ident, flight.identICAO, flight.identIATA]
            .compactMap { $0 }
            .flatMap {
                [
                    normalizedIdentifier($0),
                    normalizedMarketingIdentifier($0),
                    FlightNumberIdentity.canonical($0),
                ]
            })
    }

    private static func matches(
        _ flight: FlightAwareFlight,
        selection: FlightLegSelection
    ) -> Bool {
        if let providerIdentifier = selection.providerIdentifier,
           providerIdentifier == flight.faFlightID {
            return true
        }
        guard let departure = flight.bestDepartureDate,
              let origin = flight.origin?.bestCode?.uppercased(),
              let destination = flight.destination?.bestCode?.uppercased(),
              origin == selection.originCode.uppercased(),
              destination == selection.destinationCode.uppercased()
        else { return false }
        return abs(departure.timeIntervalSince(selection.scheduledDeparture)) <
            2 * 3600
    }

    private static func isSameDepartureDay(
        _ departure: Date,
        selectedDate: Date,
        timeZoneIdentifier: String?
    ) -> Bool {
        let requested = Calendar.current.dateComponents(
            [.year, .month, .day],
            from: selectedDate
        )
        var calendar = Calendar(identifier: .gregorian)
        if let timeZoneIdentifier,
           let timeZone = TimeZone(identifier: timeZoneIdentifier) {
            calendar.timeZone = timeZone
        }
        let actual = calendar.dateComponents(
            [.year, .month, .day],
            from: departure
        )
        return requested.year == actual.year &&
            requested.month == actual.month &&
            requested.day == actual.day
    }

    private static func normalizedIdentifier(_ value: String) -> String {
        value.replacingOccurrences(of: " ", with: "").uppercased()
    }

    private static func normalizedMarketingIdentifier(
        _ value: String
    ) -> String {
        let normalized = normalizedIdentifier(value)
        let prefix = normalized.prefix { $0.isLetter }
        let suffix = normalized.dropFirst(prefix.count)
        guard !prefix.isEmpty, let number = Int(suffix) else {
            return normalized
        }
        return "\(prefix)\(number)"
    }

    private static func effectiveStatus(
        for flight: FlightAwareFlight,
        now: Date
    ) -> String {
        if flight.cancelled == true {
            return "Cancelled"
        }
        if flight.arrivedDate != nil {
            return "Arrived"
        }
        if flight.departedDate != nil {
            return "En Route"
        }

        let raw = flight.status?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        let normalized = raw.lowercased()
        let tolerance = now.addingTimeInterval(5 * 60)
        let departureHasStarted = flight.bestDepartureDate.map {
            $0 <= tolerance
        } ?? false
        if normalized.contains("arriv") || normalized.contains("landed") {
            // A terminal provider state is authoritative once this occurrence
            // could actually have departed. Reused flight numbers can expose
            // yesterday's "Arrived" status on a genuinely future leg, so do
            // not let the status string alone complete that future flight.
            return departureHasStarted ? "Arrived" : "Scheduled"
        }
        if normalized.contains("en route") ||
            normalized.contains("enroute") ||
            normalized.contains("airborne") ||
            normalized == "active" {
            return departureHasStarted ? "En Route" : "Scheduled"
        }
        if normalized.contains("delay") {
            return "Delayed"
        }
        return raw.isEmpty ? "Scheduled" : raw
    }

    private static func validateDepartureDate(
        _ flight: FlightAwareFlight,
        selectedDate: Date?,
        origin: Airport,
        marketingNumber: String
    ) throws {
        guard let selectedDate,
              let departure = flight.bestDepartureDate
        else { return }
        let selected = Calendar.current.dateComponents(
            [.year, .month, .day],
            from: selectedDate
        )
        var originCalendar = Calendar(identifier: .gregorian)
        if let identifier = origin.timeZoneIdentifier,
           let timeZone = TimeZone(identifier: identifier) {
            originCalendar.timeZone = timeZone
        }
        let actual = originCalendar.dateComponents(
            [.year, .month, .day],
            from: departure
        )
        guard selected.year == actual.year,
              selected.month == actual.month,
              selected.day == actual.day
        else {
            throw FlightProviderError.noLiveFlight(marketingNumber)
        }
    }

    private func fetchPositionIfUseful(for flight: FlightAwareFlight) async throws -> FlightAwarePosition? {
        guard Self.effectiveStatus(for: flight, now: now()) == "En Route",
              flight.arrivedDate == nil,
              flight.cancelled != true,
              let id = flight.faFlightID,
              !id.isEmpty
        else {
            return nil
        }

        do {
            let envelope: PositionEnvelope = try await get("flights/\(id.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? id)/position")
            return envelope.bestPosition
        } catch FlightAwareProviderError.httpStatus(404, _) {
            return nil
        } catch FlightAwareProviderError.httpStatus(409, _) {
            return nil
        }
    }

    private func enrichExactOccurrenceIfUseful(
        _ flight: FlightAwareFlight
    ) async -> FlightAwareFlight {
        guard Self.gateMetadataScore(flight) < 4,
              let id = flight.faFlightID,
              !id.isEmpty
        else { return flight }

        let refreshNow = now()
        if let cached = await FlightAwareGateRefreshCache.shared.entry(
            for: id,
            now: refreshNow
        ) {
            return cached.metadata.map {
                Self.applying($0, to: flight)
            } ?? flight
        }

        let encoded = id.addingPercentEncoding(
            withAllowedCharacters: .urlPathAllowed
        ) ?? id
        do {
            let response: ExactFlightResponse = try await get(
                "flights/\(encoded)",
                queryItems: [
                    URLQueryItem(name: "max_pages", value: "1"),
                    /* Gate and terminal fields are extended AeroAPI data.
                     * Request them only for this already-selected exact
                     * occurrence; nearby Radar aircraft never reach here. */
                    URLQueryItem(name: "include_ex_data", value: "true"),
                ]
            )
            guard let exact = response.flights.first(where: {
                $0.faFlightID == id
            }) ?? response.flights.first else {
                await FlightAwareGateRefreshCache.shared.store(
                    nil,
                    for: id,
                    checkedAt: refreshNow,
                    validFor: Self.gateMetadataRefreshInterval
                )
                return flight
            }
            let metadata = FlightAwareGateMetadata(exact)
            await FlightAwareGateRefreshCache.shared.store(
                metadata.score > 0 ? metadata : nil,
                for: id,
                checkedAt: refreshNow,
                validFor: Self.gateMetadataRefreshInterval
            )
            guard Self.gateMetadataScore(exact) >
                    Self.gateMetadataScore(flight) else {
                return flight
            }
            return exact
        } catch let error as FlightAwareProviderError {
            /* Gate enrichment is optional. A free-tier restriction or a
             * temporary exact-flight failure must not discard the otherwise
             * valid scheduled occurrence.  Back off for several hours when
             * extended data is not part of the account instead of repeatedly
             * spending free-tier requests on a known-denied endpoint. */
            let validFor: TimeInterval
            switch error {
            case .httpStatus(402, _), .httpStatus(403, _):
                validFor = Self.extendedDataDeniedBackoff
            default:
                validFor = Self.gateMetadataRefreshInterval
            }
            await FlightAwareGateRefreshCache.shared.store(
                nil,
                for: id,
                checkedAt: refreshNow,
                validFor: validFor
            )
            return flight
        } catch {
            await FlightAwareGateRefreshCache.shared.store(
                nil,
                for: id,
                checkedAt: refreshNow,
                validFor: Self.gateMetadataRefreshInterval
            )
            return flight
        }
    }

    private static let gateMetadataRefreshInterval: TimeInterval = 30 * 60
    private static let extendedDataDeniedBackoff: TimeInterval = 6 * 3600

    private static func applying(
        _ metadata: FlightAwareGateMetadata,
        to flight: FlightAwareFlight
    ) -> FlightAwareFlight {
        var result = flight
        result.terminalOrigin = metadata.terminalOrigin ??
            result.terminalOrigin
        result.gateOrigin = metadata.gateOrigin ?? result.gateOrigin
        result.terminalDestination = metadata.terminalDestination ??
            result.terminalDestination
        result.gateDestination = metadata.gateDestination ??
            result.gateDestination
        return result
    }

    private static func gateMetadataScore(_ flight: FlightAwareFlight) -> Int {
        [
            flight.terminalOrigin,
            flight.gateOrigin,
            flight.terminalDestination,
            flight.gateDestination,
        ].reduce(into: 0) { score, value in
            if value?.trimmingCharacters(in: .whitespacesAndNewlines)
                .isEmpty == false {
                score += 1
            }
        }
    }

    private func makeTrackedFlight(
        from flight: FlightAwareFlight,
        requestedMarketingNumber: String,
        position: FlightAwarePosition?,
        origin: Airport,
        destination: Airport
    ) -> TrackedFlight {
        let fallback = TrackedFlight.airPremiaYP111
        let marketingNumber = bestMarketingNumber(from: flight, requested: requestedMarketingNumber)
        let operational = flight.identICAO ?? flight.ident ?? fallback.operationalCallsign
        let status = Self.effectiveStatus(for: flight, now: now())

        let isArrived = status == "Arrived"
        let progress = isArrived ? 1 : routeProgress(
            from: flight,
            position: position,
            origin: origin,
            destination: destination
        )

        let liveSummary = liveStatusSummary(for: flight, status: status, progress: progress)
        let aircraft = AircraftTypeCatalog.resolve(flight.aircraftType)
        let routeDistanceKilometers = flight.routeDistance.map {
            Int(round(Double($0) * 1.852))
        }
        return TrackedFlight(
            airlineName: Self.airlineName(
                for: marketingNumber,
                fallback: fallback.airlineName
            ),
            marketingNumber: marketingNumber,
            operationalCallsign: operational,
            aircraftType: aircraft?.label ?? "Aircraft type unavailable",
            aircraftTypeCode: aircraft?.code,
            origin: origin,
            destination: destination,
            scheduleSummary: "\(origin.code) → \(destination.code) · \(status)",
            durationSummary: durationSummary(for: flight) ?? fallback.durationSummary,
            distanceSummary: distanceSummary(
                for: flight,
                progress: progress
            ) ?? fallback.distanceSummary,
            liveStatusSummary: liveSummary,
            dataSourceSummary: "FlightAware AeroAPI",
            routeProgress: progress,
            lastPosition: isArrived ? destination.coordinate : position?.coordinate,
            lastUpdated: now(),
            altitudeMeters: isArrived ? 0 : position?.altitude.map {
                Int(round(Double($0) * 100.0 * 0.3048))
            },
            groundSpeedKPH: isArrived ? 0 : position?.groundspeed.map {
                Int(round(Double($0) * 1.852))
            },
            headingDegrees: position?.heading,
            verticalPhase: isArrived
                ? "landed"
                : Self.verticalPhase(position?.altitudeChange),
            etaSummary: (flight.arrivedDate ?? flight.bestArrivalDate).map {
                Self.shortTimeString(from: $0)
            },
            elapsedSummary: elapsedSummary(for: flight),
            remainingSummary: isArrived ? "0m" : remainingSummary(for: flight),
            scheduledDeparture: flight.bestDepartureDate,
            estimatedDeparture:
                flight.estimatedOut ??
                flight.estimatedOff ??
                flight.scheduledOut ??
                flight.scheduledOff,
            actualDeparture: flight.departedDate,
            estimatedArrival: flight.bestArrivalDate,
            actualArrival: isArrived
                ? (flight.arrivedDate ?? now())
                : flight.arrivedDate,
            routeDistanceKilometers: routeDistanceKilometers,
            originTerminal: flight.terminalOrigin,
            originGate: flight.gateOrigin,
            destinationTerminal: flight.terminalDestination,
            destinationGate: flight.gateDestination
        )
    }

    private static func airlineName(for marketingNumber: String, fallback: String) -> String {
        let code = String(
            marketingNumber
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .uppercased()
                .prefix(2)
        )
        let names: [String: String] = [
            "AA": "American Airlines",
            "AS": "Alaska Airlines",
            "DL": "Delta Air Lines",
            "FX": "FedEx Express",
            "KE": "Korean Air",
            "LH": "Lufthansa",
            "OO": "SkyWest Airlines",
            "UA": "United Airlines",
            "WN": "Southwest Airlines",
            "YP": "Air Premia",
            "5X": "UPS Airlines",
        ]
        return names[code] ?? (code == "YP" ? fallback : code)
    }

    private static func verticalPhase(_ altitudeChange: String?) -> String? {
        switch altitudeChange?.uppercased() {
        case "C":
            return "climbing"
        case "D":
            return "descending"
        case "":
            return "cruising"
        default:
            return nil
        }
    }

    private func bestMarketingNumber(from flight: FlightAwareFlight, requested: String) -> String {
        if let identIATA = flight.identIATA, !identIATA.isEmpty {
            return identIATA.replacingOccurrences(of: " ", with: "")
        }

        let normalized = FlightNumberIdentity.canonical(requested)
        if normalized == "APZ111" {
            return "YP111"
        }
        return normalized.isEmpty ? "YP111" : normalized
    }

    private func resolveAirport(from aeroAirport: FlightAwareAirport?, fallback: Airport) async throws -> Airport {
        guard let aeroAirport else {
            return fallback
        }

        let code = aeroAirport.codeIATA ?? aeroAirport.codeICAO ?? aeroAirport.code ?? fallback.code
        if code == "ICN" || code == "RKSI" {
            return .icn
        }
        if code == "SFO" || code == "KSFO" {
            return .sfo
        }

        let normalized = code.uppercased()
        if let cached = await FlightAwareAirportCache.shared.airport(for: normalized) {
            return cached
        }

        let response: FlightAwareAirportInfo = try await get(
            "airports/\(normalized.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? normalized)"
        )
        let displayCode =
            aeroAirport.codeIATA ??
            response.codeIATA ??
            response.codeICAO ??
            normalized
        let city =
            aeroAirport.city ??
            response.city ??
            aeroAirport.name ??
            response.name ??
            ""
        let airport = Airport(
            code: displayCode,
            name: aeroAirport.name ?? response.name ?? normalized,
            city: Self.displayCity(city, airportCode: displayCode),
            coordinate: CLLocationCoordinate2D(
                latitude: response.latitude,
                longitude: response.longitude
            ),
            countryCode: Self.normalizedCountryCode(
                aeroAirport.countryCode ??
                response.countryCode ??
                Self.countryCodeFromICAO(aeroAirport.codeICAO ?? response.codeICAO)
            ),
            timeZoneIdentifier: response.timezone
        )
        await FlightAwareAirportCache.shared.store(airport, for: normalized)
        if let iata = response.codeIATA {
            await FlightAwareAirportCache.shared.store(airport, for: iata.uppercased())
        }
        if let icao = response.codeICAO {
            await FlightAwareAirportCache.shared.store(airport, for: icao.uppercased())
        }
        return airport
    }

    private static func displayCity(
        _ value: String,
        airportCode: String
    ) -> String {
        let trimmed = value.trimmingCharacters(
            in: .whitespacesAndNewlines
        )
        let normalizedValue = trimmed.uppercased()
        let normalizedCode = airportCode
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        if normalizedValue == normalizedCode {
            return ""
        }
        return trimmed
    }

    private static func normalizedCountryCode(_ value: String?) -> String? {
        guard let value else { return nil }
        let normalized = value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        return normalized.count == 2 ? normalized : nil
    }

    private static func countryCodeFromICAO(_ value: String?) -> String? {
        guard let icao = value?
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased(),
            !icao.isEmpty
        else {
            return nil
        }
        if icao.hasPrefix("K") { return "US" }
        if icao.hasPrefix("C") { return "CA" }
        if icao.hasPrefix("RK") { return "KR" }
        if icao.hasPrefix("RJ") { return "JP" }
        if icao.hasPrefix("ED") { return "DE" }
        if icao.hasPrefix("EG") { return "GB" }
        if icao.hasPrefix("LF") { return "FR" }
        if icao.hasPrefix("LE") { return "ES" }
        if icao.hasPrefix("LI") { return "IT" }
        if icao.hasPrefix("EH") { return "NL" }
        if icao.hasPrefix("EB") { return "BE" }
        if icao.hasPrefix("LS") { return "CH" }
        if icao.hasPrefix("LO") { return "AT" }
        if icao.hasPrefix("UU") || icao.hasPrefix("UL") { return "RU" }
        if icao.hasPrefix("Z") { return "CN" }
        if icao.hasPrefix("Y") { return "AU" }
        if icao.hasPrefix("NZ") { return "NZ" }
        if icao.hasPrefix("OM") { return "AE" }
        if icao.hasPrefix("OE") { return "SA" }
        if icao.hasPrefix("VT") { return "TH" }
        if icao.hasPrefix("VV") { return "VN" }
        if icao.hasPrefix("WS") { return "SG" }
        if icao.hasPrefix("WI") || icao.hasPrefix("WA") { return "ID" }
        if icao.hasPrefix("VH") { return "HK" }
        if icao.hasPrefix("RP") { return "PH" }
        if icao.hasPrefix("VI") || icao.hasPrefix("VA") { return "IN" }
        if icao.hasPrefix("SB") { return "BR" }
        if icao.hasPrefix("SA") { return "AR" }
        if icao.hasPrefix("SC") { return "CL" }
        if icao.hasPrefix("MM") { return "MX" }
        return nil
    }

    private static func requestInterval(around date: Date) -> DateInterval {
        let dayStart = Calendar.current.startOfDay(for: date)
        return DateInterval(
            start: dayStart.addingTimeInterval(-12 * 3600),
            end: dayStart.addingTimeInterval(36 * 3600)
        )
    }

    private static func requestDateString(_ date: Date) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter.string(from: date)
    }

    private func routeProgress(
        from flight: FlightAwareFlight,
        position: FlightAwarePosition?,
        origin: Airport,
        destination: Airport
    ) -> Double? {
        if let position {
            return GreatCircle.nearestProgress(
                of: position.coordinate,
                from: origin.coordinate,
                to: destination.coordinate
            )
        }

        if let progressPercent = flight.progressPercent,
           (0...100).contains(progressPercent) {
            return Double(progressPercent) / 100.0
        }

        guard let departure = flight.departedDate ?? flight.bestDepartureDate,
              let arrival = flight.arrivedDate ?? flight.bestArrivalDate
        else {
            return nil
        }

        let total = arrival.timeIntervalSince(departure)
        guard total > 0 else {
            return nil
        }

        let elapsed = now().timeIntervalSince(departure)
        return min(1, max(0, elapsed / total))
    }

    private func liveStatusSummary(for flight: FlightAwareFlight, status: String?, progress: Double?) -> String {
        var parts: [String] = []

        if let status, !status.isEmpty {
            parts.append(status)
        } else if flight.cancelled == true {
            parts.append("Cancelled")
        } else if flight.arrivedDate != nil {
            parts.append("Arrived")
        } else if flight.departedDate != nil {
            parts.append("En route")
        } else {
            parts.append("Scheduled")
        }

        if let progress {
            parts.append("\(Int(round(progress * 100)))%")
        }

        if let arrival = flight.arrivedDate ?? flight.bestArrivalDate {
            parts.append("ETA \(Self.shortTimeString(from: arrival))")
        }

        return parts.joined(separator: " · ")
    }

    private func durationSummary(for flight: FlightAwareFlight) -> String? {
        if let filedETE = flight.filedETE, filedETE > 0 {
            return Self.durationString(from: TimeInterval(filedETE))
        }

        guard let departure = flight.departedDate ?? flight.bestDepartureDate,
              let arrival = flight.arrivedDate ?? flight.bestArrivalDate
        else {
            return nil
        }

        return Self.durationString(from: arrival.timeIntervalSince(departure))
    }

    private func elapsedSummary(for flight: FlightAwareFlight) -> String? {
        guard let departure = flight.departedDate else {
            return nil
        }
        let end = flight.arrivedDate ?? min(now(), flight.bestArrivalDate ?? now())
        return Self.durationString(from: max(0, end.timeIntervalSince(departure)))
    }

    private func remainingSummary(for flight: FlightAwareFlight) -> String? {
        guard flight.arrivedDate == nil,
              let arrival = flight.bestArrivalDate
        else {
            return flight.arrivedDate == nil ? nil : "Arrived"
        }
        return Self.durationString(from: max(0, arrival.timeIntervalSince(now())))
    }

    private func distanceSummary(
        for flight: FlightAwareFlight,
        progress: Double?
    ) -> String? {
        guard let distance = flight.routeDistance, distance > 0 else {
            return nil
        }
        let totalKilometers = Double(distance) * 1.852
        let remainingKilometers =
            totalKilometers * (1.0 - min(1.0, max(0.0, progress ?? 0.0)))
        return "\(Int(round(remainingKilometers))) km"
    }

    private func get<T: Decodable>(_ path: String, queryItems: [URLQueryItem] = []) async throws -> T {
        var components = URLComponents(string: "https://aeroapi.flightaware.com/aeroapi/\(path)")!
        if !queryItems.isEmpty {
            components.queryItems = queryItems
        }

        guard let url = components.url else {
            throw FlightAwareProviderError.badURL
        }

        var request = URLRequest(url: url)
        request.timeoutInterval = 20
        request.setValue(apiKey, forHTTPHeaderField: "x-apikey")
        request.setValue("application/json", forHTTPHeaderField: "Accept")

        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw FlightAwareProviderError.badResponse
        }
        guard (200..<300).contains(http.statusCode) else {
            let body = String(data: data, encoding: .utf8) ?? ""
            throw FlightAwareProviderError.httpStatus(http.statusCode, body)
        }

        do {
            return try Self.decoder.decode(T.self, from: data)
        } catch {
            throw FlightAwareProviderError.decode(error.localizedDescription)
        }
    }

    private static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .custom { decoder in
            let container = try decoder.singleValueContainer()
            let value = try container.decode(String.self)
            if let date = decodeISO8601(value) {
                return date
            }
            throw DecodingError.dataCorruptedError(in: container, debugDescription: "Invalid ISO8601 date: \(value)")
        }
        return decoder
    }()

    private static func decodeISO8601(_ value: String) -> Date? {
        let fractional = ISO8601DateFormatter()
        fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        if let date = fractional.date(from: value) {
            return date
        }

        let wholeSeconds = ISO8601DateFormatter()
        wholeSeconds.formatOptions = [.withInternetDateTime]
        return wholeSeconds.date(from: value)
    }

    private static func durationString(from interval: TimeInterval) -> String? {
        let formatter = DateComponentsFormatter()
        formatter.allowedUnits = [.hour, .minute]
        formatter.unitsStyle = .abbreviated
        formatter.maximumUnitCount = 2
        return formatter.string(from: interval)
    }

    private static func shortTimeString(from date: Date) -> String {
        let formatter = DateFormatter()
        formatter.timeStyle = .short
        formatter.dateStyle = .none
        return formatter.string(from: date)
    }
}

private struct FlightsResponse: Decodable {
    let flights: [FlightAwareFlight]
}

private struct ExactFlightResponse: Decodable {
    let flights: [FlightAwareFlight]

    private enum CodingKeys: String, CodingKey {
        case flights
    }

    init(from decoder: Decoder) throws {
        if let container = try? decoder.container(keyedBy: CodingKeys.self),
           container.contains(.flights) {
            flights = try container.decode(
                [FlightAwareFlight].self,
                forKey: .flights
            )
        } else {
            flights = [try FlightAwareFlight(from: decoder)]
        }
    }
}

private struct FlightAwareFlight: Decodable {
    let ident: String?
    let identICAO: String?
    let identIATA: String?
    let faFlightID: String?
    let cancelled: Bool?
    let diverted: Bool?
    let origin: FlightAwareAirport?
    let destination: FlightAwareAirport?
    let scheduledOut: Date?
    let estimatedOut: Date?
    let actualOut: Date?
    let scheduledOff: Date?
    let estimatedOff: Date?
    let actualOff: Date?
    let scheduledOn: Date?
    let estimatedOn: Date?
    let actualOn: Date?
    let scheduledIn: Date?
    let estimatedIn: Date?
    let actualIn: Date?
    let progressPercent: Int?
    let status: String?
    let aircraftType: String?
    let routeDistance: Int?
    let filedETE: Int?
    var terminalOrigin: String?
    var gateOrigin: String?
    var terminalDestination: String?
    var gateDestination: String?

    var bestDepartureDate: Date? {
        actualOut ?? actualOff ?? estimatedOut ?? estimatedOff ?? scheduledOut ?? scheduledOff
    }

    var departedDate: Date? {
        actualOut ?? actualOff
    }

    var bestArrivalDate: Date? {
        actualIn ?? actualOn ?? estimatedIn ?? estimatedOn ?? scheduledIn ?? scheduledOn
    }

    var arrivedDate: Date? {
        actualIn ?? actualOn
    }

    enum CodingKeys: String, CodingKey {
        case ident
        case identICAO = "ident_icao"
        case identIATA = "ident_iata"
        case faFlightID = "fa_flight_id"
        case cancelled
        case diverted
        case origin
        case destination
        case scheduledOut = "scheduled_out"
        case estimatedOut = "estimated_out"
        case actualOut = "actual_out"
        case scheduledOff = "scheduled_off"
        case estimatedOff = "estimated_off"
        case actualOff = "actual_off"
        case scheduledOn = "scheduled_on"
        case estimatedOn = "estimated_on"
        case actualOn = "actual_on"
        case scheduledIn = "scheduled_in"
        case estimatedIn = "estimated_in"
        case actualIn = "actual_in"
        case progressPercent = "progress_percent"
        case status
        case aircraftType = "aircraft_type"
        case routeDistance = "route_distance"
        case filedETE = "filed_ete"
        case terminalOrigin = "terminal_origin"
        case gateOrigin = "gate_origin"
        case terminalDestination = "terminal_destination"
        case gateDestination = "gate_destination"
    }
}

private struct FlightAwareAirport: Decodable {
    let code: String?
    let codeICAO: String?
    let codeIATA: String?
    let name: String?
    let city: String?
    let countryCode: String?

    var bestCode: String? {
        codeIATA ?? codeICAO ?? code
    }

    enum CodingKeys: String, CodingKey {
        case code
        case codeICAO = "code_icao"
        case codeIATA = "code_iata"
        case name
        case city
        case countryCode = "country_code"
    }
}

private struct FlightAwareAirportInfo: Decodable {
    let codeICAO: String?
    let codeIATA: String?
    let name: String?
    let city: String?
    let countryCode: String?
    let latitude: Double
    let longitude: Double
    let timezone: String?

    enum CodingKeys: String, CodingKey {
        case codeICAO = "code_icao"
        case codeIATA = "code_iata"
        case name
        case city
        case countryCode = "country_code"
        case latitude
        case longitude
        case timezone
    }
}

private actor FlightAwareAirportCache {
    static let shared = FlightAwareAirportCache()

    private var airports: [String: Airport] = [:]

    func airport(for code: String) -> Airport? {
        airports[code]
    }

    func store(_ airport: Airport, for code: String) {
        airports[code] = airport
    }
}

private struct FlightAwareGateMetadata: Sendable {
    let terminalOrigin: String?
    let gateOrigin: String?
    let terminalDestination: String?
    let gateDestination: String?

    init(_ flight: FlightAwareFlight) {
        terminalOrigin = flight.terminalOrigin
        gateOrigin = flight.gateOrigin
        terminalDestination = flight.terminalDestination
        gateDestination = flight.gateDestination
    }

    var score: Int {
        [terminalOrigin, gateOrigin, terminalDestination, gateDestination]
            .reduce(into: 0) { result, value in
                if value?.trimmingCharacters(in: .whitespacesAndNewlines)
                    .isEmpty == false {
                    result += 1
                }
            }
    }
}

private struct FlightAwareGateRefreshEntry: Sendable {
    let checkedAt: Date
    let validFor: TimeInterval
    let metadata: FlightAwareGateMetadata?
}

private actor FlightAwareGateRefreshCache {
    static let shared = FlightAwareGateRefreshCache()

    private var entries: [String: FlightAwareGateRefreshEntry] = [:]

    func entry(
        for identifier: String,
        now: Date
    ) -> FlightAwareGateRefreshEntry? {
        guard let entry = entries[identifier],
              now.timeIntervalSince(entry.checkedAt) >= 0,
              now.timeIntervalSince(entry.checkedAt) < entry.validFor
        else { return nil }
        return entry
    }

    func store(
        _ metadata: FlightAwareGateMetadata?,
        for identifier: String,
        checkedAt: Date,
        validFor: TimeInterval
    ) {
        entries[identifier] = FlightAwareGateRefreshEntry(
            checkedAt: checkedAt,
            validFor: validFor,
            metadata: metadata
        )
    }
}

private struct FlightAwareTrackedFlightCacheEntry: Sendable {
    let checkedAt: Date
    let validFor: TimeInterval
    let flight: TrackedFlight
}

private actor FlightAwareTrackedFlightCache {
    static let shared = FlightAwareTrackedFlightCache()

    private var entries: [String: FlightAwareTrackedFlightCacheEntry] = [:]

    static func key(
        apiKey: String,
        marketingNumber: String,
        selection: FlightLegSelection
    ) -> String {
        [
            String(apiKey.hashValue),
            FlightNumberIdentity.canonical(marketingNumber),
            selection.providerIdentifier ?? "-",
            selection.originCode.uppercased(),
            selection.destinationCode.uppercased(),
            String(Int(selection.scheduledDeparture.timeIntervalSince1970)),
        ].joined(separator: "|")
    }

    func flight(for key: String, now: Date) -> TrackedFlight? {
        guard let entry = entries[key],
              now.timeIntervalSince(entry.checkedAt) >= 0,
              now.timeIntervalSince(entry.checkedAt) < entry.validFor
        else {
            entries[key] = nil
            return nil
        }
        return entry.flight
    }

    func store(
        _ flight: TrackedFlight,
        for key: String,
        checkedAt: Date,
        validFor: TimeInterval
    ) {
        entries[key] = FlightAwareTrackedFlightCacheEntry(
            checkedAt: checkedAt,
            validFor: validFor,
            flight: flight
        )
    }
}

private struct PositionEnvelope: Decodable {
    let position: FlightAwarePosition?
    let lastPosition: FlightAwarePosition?
    let positions: [FlightAwarePosition]?
    let latitude: Double?
    let longitude: Double?
    let timestamp: Date?

    var bestPosition: FlightAwarePosition? {
        position ?? lastPosition ?? positions?.last ?? rootPosition
    }

    private var rootPosition: FlightAwarePosition? {
        guard let latitude, let longitude else {
            return nil
        }
        return FlightAwarePosition(latitude: latitude, longitude: longitude, timestamp: timestamp)
    }

    enum CodingKeys: String, CodingKey {
        case position
        case lastPosition = "last_position"
        case positions
        case latitude
        case longitude
        case timestamp
    }
}

private struct FlightAwarePosition: Decodable {
    let latitude: Double
    let longitude: Double
    let timestamp: Date?
    var altitude: Int? = nil
    var groundspeed: Int? = nil
    var heading: Int? = nil
    var altitudeChange: String? = nil

    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }

    enum CodingKeys: String, CodingKey {
        case latitude
        case longitude
        case timestamp
        case altitude
        case groundspeed
        case heading
        case altitudeChange = "altitude_change"
    }
}

enum FlightAwareProviderError: LocalizedError {
    case badURL
    case badResponse
    case httpStatus(Int, String)
    case decode(String)

    var errorDescription: String? {
        switch self {
        case .badURL:
            "Could not build FlightAware request URL."
        case .badResponse:
            "FlightAware returned a non-HTTP response."
        case .httpStatus(let status, let body):
            "FlightAware HTTP \(status): \(body)"
        case .decode(let detail):
            "Could not decode FlightAware response: \(detail)"
        }
    }
}
