// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

struct TrackedFlightTelemetry: Sendable {
    let coordinate: CLLocationCoordinate2D?
    let altitudeMeters: Int?
    let groundSpeedKPH: Int?
    let headingDegrees: Int?
    let verticalPhase: String?
    let updated: Date?
}

protocol TrackedFlightTelemetryProviding: Sendable {
    func telemetry(for flight: TrackedFlight) async throws
        -> TrackedFlightTelemetry?
}

protocol TrackedFlightFallbackTelemetryProviding: Sendable {
    func fallbackTelemetry(
        for flight: TrackedFlight,
        on date: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlightTelemetry?
}

actor TrackedFlightTelemetryStateStore {
    static let shared = TrackedFlightTelemetryStateStore()

    struct Snapshot: Sendable {
        let telemetry: TrackedFlightTelemetry
        let source: String
    }

    private struct Entry: Sendable {
        var telemetry: TrackedFlightTelemetry
        var missingSince: Date?
        var lastFallbackAttempt: Date?
        var usedFallback = false
        var source = "OpenSky"
        var lastAccess: Date
    }

    private var entries: [String: Entry] = [:]

    func ingestOpenSky(
        _ telemetry: TrackedFlightTelemetry?,
        for key: String,
        at now: Date
    ) -> Snapshot? {
        prune(at: now)
        var entry = entries[key] ?? Entry(
            telemetry: .empty,
            lastAccess: now
        )
        if let telemetry {
            entry.telemetry = entry.telemetry.merging(telemetry)
            if telemetry.hasRequiredMotion {
                entry.source = "OpenSky"
                entry.usedFallback = false
            } else {
                entry.source = entry.usedFallback
                    ? "OpenSky + FlightAware telemetry"
                    : "OpenSky"
            }
        }
        entry.missingSince = telemetry?.hasRequiredMotion == true
            ? nil
            : (entry.missingSince ?? now)
        entry.lastAccess = now
        entries[key] = entry
        return snapshot(entry)
    }

    func beginFallbackIfAllowed(
        for key: String,
        at now: Date,
        interval: TimeInterval
    ) -> Bool {
        var entry = entries[key] ?? Entry(
            telemetry: .empty,
            missingSince: now,
            lastAccess: now
        )
        if let lastAttempt = entry.lastFallbackAttempt,
           now.timeIntervalSince(lastAttempt) < interval {
            entry.lastAccess = now
            entries[key] = entry
            return false
        }

        let needsInitialSeed = !entry.telemetry.hasRequiredMotion
        let outageMatured = entry.missingSince.map {
            now.timeIntervalSince($0) >= interval
        } ?? false
        guard needsInitialSeed || outageMatured else {
            entry.lastAccess = now
            entries[key] = entry
            return false
        }

        // Record the attempt before the network call. Failed requests still
        // consume the five-minute allowance and cannot create a retry storm.
        entry.lastFallbackAttempt = now
        entry.lastAccess = now
        entries[key] = entry
        return true
    }

    func ingestFallback(
        _ telemetry: TrackedFlightTelemetry,
        for key: String,
        at now: Date
    ) -> Snapshot {
        var entry = entries[key] ?? Entry(
            telemetry: .empty,
            missingSince: now,
            lastAccess: now
        )
        entry.telemetry = entry.telemetry.merging(telemetry)
        entry.usedFallback = true
        entry.source = "FlightAware telemetry"
        entry.lastAccess = now
        entries[key] = entry
        return snapshot(entry)!
    }

    func remove(_ key: String) {
        entries[key] = nil
    }

    private func snapshot(_ entry: Entry) -> Snapshot? {
        guard entry.telemetry.hasAnyMotion else { return nil }
        return Snapshot(telemetry: entry.telemetry, source: entry.source)
    }

    private func prune(at now: Date) {
        entries = entries.filter {
            now.timeIntervalSince($0.value.lastAccess) < 24 * 3600
        }
    }
}

private extension TrackedFlightTelemetry {
    static let empty = TrackedFlightTelemetry(
        coordinate: nil,
        altitudeMeters: nil,
        groundSpeedKPH: nil,
        headingDegrees: nil,
        verticalPhase: nil,
        updated: nil
    )

    var hasAnyMotion: Bool {
        coordinate != nil || altitudeMeters != nil ||
            groundSpeedKPH != nil || headingDegrees != nil
    }

    var hasRequiredMotion: Bool {
        coordinate != nil && altitudeMeters != nil &&
            groundSpeedKPH != nil && headingDegrees != nil
    }

    func merging(_ newer: TrackedFlightTelemetry) -> TrackedFlightTelemetry {
        TrackedFlightTelemetry(
            coordinate: newer.coordinate ?? coordinate,
            altitudeMeters: newer.altitudeMeters ?? altitudeMeters,
            groundSpeedKPH: newer.groundSpeedKPH ?? groundSpeedKPH,
            headingDegrees: newer.headingDegrees ?? headingDegrees,
            verticalPhase: newer.verticalPhase ?? verticalPhase,
            updated: Self.newest(updated, newer.updated)
        )
    }

    private static func newest(_ first: Date?, _ second: Date?) -> Date? {
        switch (first, second) {
        case (.some(let lhs), .some(let rhs)):
            max(lhs, rhs)
        case (.some(let value), .none), (.none, .some(let value)):
            value
        case (.none, .none):
            nil
        }
    }
}

struct FallbackFlightStatusProvider: FlightLegProviding {
    let primary: any FlightStatusProviding
    let fallback: (any FlightStatusProviding)?
    let telemetryProvider: (any TrackedFlightTelemetryProviding)?
    let fallbackTelemetryProvider:
        (any TrackedFlightFallbackTelemetryProviding)?
    let telemetryState: TrackedFlightTelemetryStateStore
    let fallbackTelemetryInterval: TimeInterval
    var now: @Sendable () -> Date

    init(
        primary: any FlightStatusProviding,
        fallback: (any FlightStatusProviding)? = nil,
        telemetryProvider: (any TrackedFlightTelemetryProviding)? = nil,
        fallbackTelemetryProvider:
            (any TrackedFlightFallbackTelemetryProviding)? = nil,
        telemetryState: TrackedFlightTelemetryStateStore = .shared,
        fallbackTelemetryInterval: TimeInterval = 5 * 60,
        now: @escaping @Sendable () -> Date = { Date() }
    ) {
        self.primary = primary
        self.fallback = fallback
        self.telemetryProvider = telemetryProvider
        self.fallbackTelemetryProvider = fallbackTelemetryProvider
        self.telemetryState = telemetryState
        self.fallbackTelemetryInterval = fallbackTelemetryInterval
        self.now = now
    }

    func trackedFlight(
        for marketingNumber: String,
        on date: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        do {
            var resolved = try await trackedFlight(
                using: primary,
                marketingNumber: marketingNumber,
                date: date,
                selection: selection
            )
            /* FlightAware owns the selected occurrence, route and schedule.
             * Its free-tier response can omit terminal/gate fields or retain
             * an airborne state after touchdown. For an explicitly selected
             * occurrence only, let AirLabs fill missing terminal fields and,
             * once the ETA has passed, confirm a terminal state. AirLabs may
             * never replace the selected route or occurrence. */
            if let selection,
               Self.hasMissingGateMetadata(resolved) ||
                    Self.shouldValidateTerminalState(resolved, at: now()),
               let fallback,
               let supplemental = try? await trackedFlight(
                    using: fallback,
                    marketingNumber: marketingNumber,
                    date: date,
                    selection: selection
               ),
               Self.isSameSelectedLeg(
                    resolved,
                    supplemental,
                    selection: selection
               ) {
                if Self.hasMissingGateMetadata(resolved) {
                    resolved = Self.mergingMissingGateMetadata(
                        primary: resolved,
                        supplemental: supplemental
                    )
                }
                if Self.shouldValidateTerminalState(resolved, at: now()),
                   Self.isSameSelectedTerminalOccurrence(
                        supplemental,
                        selection: selection
                   ),
                   Self.hasExplicitTerminalState(supplemental) {
                    resolved = Self.mergingTerminalConfirmation(
                        primary: resolved,
                        supplemental: supplemental,
                        at: now()
                    )
                }
            }

            /* Once that exact flight is airborne, OpenSky owns the live
             * motion fields. Retain each last valid field independently so
             * a sparse state vector never turns an already-known value into
             * a dash. FlightAware position is a five-minute fallback only. */
            let telemetryKey = Self.telemetryKey(
                flight: resolved,
                selection: selection
            )
            if resolved.journeyPhase(at: now()) == "arrived" {
                await telemetryState.remove(telemetryKey)
                return resolved.normalizedForTerminalState(at: now())
            }
            if Self.shouldUseLiveTelemetry(resolved, at: now()) {
                let refreshNow = now()
                var openSky: TrackedFlightTelemetry?
                if let telemetryProvider,
                   let candidate = try? await telemetryProvider.telemetry(
                        for: resolved
                   ),
                   Self.isFresh(candidate, at: refreshNow) {
                    openSky = candidate
                }
                var snapshot = await telemetryState.ingestOpenSky(
                    openSky,
                    for: telemetryKey,
                    at: refreshNow
                )

                if let fallbackTelemetryProvider,
                   await telemetryState.beginFallbackIfAllowed(
                        for: telemetryKey,
                        at: refreshNow,
                        interval: fallbackTelemetryInterval
                   ) {
                    if let fallbackTelemetry = try? await
                        fallbackTelemetryProvider.fallbackTelemetry(
                            for: resolved,
                            on: date,
                            matching: selection
                        ) {
                        snapshot = await telemetryState.ingestFallback(
                            fallbackTelemetry,
                            for: telemetryKey,
                            at: refreshNow
                        )
                    }
                }

                if let snapshot {
                    resolved = Self.merging(
                        primary: resolved,
                        telemetry: snapshot.telemetry,
                        source: snapshot.source
                    )
                }
            }
            return resolved.reconciledTerminalState(at: now())
        } catch {
            let primaryError = error
            FileHandle.standardError.write(Data(
                ("[airspace] FlightAware primary failed: " +
                 "\(primaryError.localizedDescription)\n").utf8
            ))
            guard let fallback else {
                throw primaryError
            }
            do {
                let resolved = try await trackedFlight(
                    using: fallback,
                    marketingNumber: marketingNumber,
                    date: date,
                    selection: selection
                )
                return resolved.reconciledTerminalState(at: now())
            } catch FlightProviderError.noLiveFlight {
                throw FlightProviderError.noLiveFlight(marketingNumber)
            } catch {
                throw FlightFallbackError.bothProvidersFailed(
                    primary: primaryError.localizedDescription,
                    fallback: error.localizedDescription
                )
            }
        }
    }

    func flightLegs(
        for marketingNumber: String,
        on date: Date
    ) async throws -> [FlightLegCandidate] {
        if let provider = primary as? any FlightLegProviding {
            do {
                let legs = try await provider.flightLegs(
                    for: marketingNumber,
                    on: date
                )
                if !legs.isEmpty {
                    return legs
                }
            } catch {
                FileHandle.standardError.write(Data(
                    ("[airspace] FlightAware leg lookup failed: " +
                     "\(error.localizedDescription)\n").utf8
                ))
            }
        }

        /* AirLabs is a true fallback here too. A successful FlightAware leg
         * list is authoritative and must not be blended with a second
         * provider's stale or differently coded occurrence. */
        if let provider = fallback as? any FlightLegProviding {
            let legs = try await provider.flightLegs(
                for: marketingNumber,
                on: date
            )
            if !legs.isEmpty {
                return legs
            }
        }
        throw FlightProviderError.noLiveFlight(marketingNumber)
    }

    private func trackedFlight(
        using provider: any FlightStatusProviding,
        marketingNumber: String,
        date: Date?,
        selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        if let provider = provider as? any FlightLegProviding {
            return try await provider.trackedFlight(
                for: marketingNumber,
                on: date,
                matching: selection
            )
        }
        return try await provider.trackedFlight(
            for: marketingNumber,
            on: date
        )
    }

    private static func shouldUseLiveTelemetry(
        _ flight: TrackedFlight,
        at now: Date
    ) -> Bool {
        flight.journeyPhase(at: now) == "enroute"
    }

    private static func telemetryKey(
        flight: TrackedFlight,
        selection: FlightLegSelection?
    ) -> String {
        let departure = selection?.scheduledDeparture ??
            flight.scheduledDeparture ?? flight.actualDeparture ?? .distantPast
        return [
            FlightNumberIdentity.canonical(flight.marketingNumber),
            selection?.providerIdentifier ?? "no-provider-id",
            selection?.originCode.uppercased() ?? flight.origin.code.uppercased(),
            selection?.destinationCode.uppercased() ??
                flight.destination.code.uppercased(),
            String(Int(departure.timeIntervalSince1970)),
        ].joined(separator: "|")
    }

    private static func isFresh(
        _ telemetry: TrackedFlightTelemetry,
        at now: Date
    ) -> Bool {
        guard let updated = telemetry.updated else { return false }
        let age = now.timeIntervalSince(updated)
        return age >= -30 && age <= 90
    }

    private static func hasMissingGateMetadata(_ flight: TrackedFlight) -> Bool {
        [
            flight.originTerminal,
            flight.originGate,
            flight.destinationTerminal,
            flight.destinationGate,
        ].contains { value in
            value?.trimmingCharacters(in: .whitespacesAndNewlines)
                .isEmpty != false
        }
    }

    private static func shouldValidateTerminalState(
        _ flight: TrackedFlight,
        at now: Date
    ) -> Bool {
        guard flight.actualArrival == nil,
              let expectedArrival = flight.estimatedArrival,
              now >= expectedArrival.addingTimeInterval(15 * 60)
        else { return false }

        let status = flight.liveStatusSummary.lowercased()
        guard !status.contains("arrived"),
              !status.contains("landed"),
              !status.contains("cancelled"),
              !status.contains("canceled")
        else { return false }
        return flight.actualDeparture != nil ||
            status.contains("en route") ||
            status.contains("enroute") ||
            status.contains("airborne")
    }

    private static func hasExplicitTerminalState(
        _ flight: TrackedFlight
    ) -> Bool {
        let status = flight.liveStatusSummary.lowercased()
        return flight.actualArrival != nil ||
            status.contains("arrived") ||
            status.contains("landed")
    }

    private static func isSameSelectedLeg(
        _ primary: TrackedFlight,
        _ supplemental: TrackedFlight,
        selection: FlightLegSelection
    ) -> Bool {
        let expectedOrigin = selection.originCode.uppercased()
        let expectedDestination = selection.destinationCode.uppercased()
        guard primary.origin.code.uppercased() == expectedOrigin,
              primary.destination.code.uppercased() == expectedDestination,
              supplemental.origin.code.uppercased() == expectedOrigin,
              supplemental.destination.code.uppercased() == expectedDestination
        else { return false }

        guard let departure = supplemental.scheduledDeparture else {
            return true
        }
        return abs(departure.timeIntervalSince(
            selection.scheduledDeparture
        )) < 2 * 3600
    }

    private static func isSameSelectedTerminalOccurrence(
        _ supplemental: TrackedFlight,
        selection: FlightLegSelection
    ) -> Bool {
        guard let departure = supplemental.scheduledDeparture ??
                supplemental.estimatedDeparture ??
                supplemental.actualDeparture
        else { return false }
        return abs(departure.timeIntervalSince(
            selection.scheduledDeparture
        )) < 2 * 3600
    }

    private static func mergingMissingGateMetadata(
        primary: TrackedFlight,
        supplemental: TrackedFlight
    ) -> TrackedFlight {
        TrackedFlight(
            airlineName: primary.airlineName,
            marketingNumber: primary.marketingNumber,
            operationalCallsign: primary.operationalCallsign,
            aircraftType: primary.aircraftType,
            aircraftTypeCode: primary.aircraftTypeCode,
            origin: primary.origin,
            destination: primary.destination,
            scheduleSummary: primary.scheduleSummary,
            durationSummary: primary.durationSummary,
            distanceSummary: primary.distanceSummary,
            liveStatusSummary: primary.liveStatusSummary,
            dataSourceSummary: sourceSummary(
                primary.dataSourceSummary,
                adding: "AirLabs"
            ),
            routeProgress: primary.routeProgress,
            lastPosition: primary.lastPosition,
            lastUpdated: primary.lastUpdated,
            altitudeMeters: primary.altitudeMeters,
            groundSpeedKPH: primary.groundSpeedKPH,
            headingDegrees: primary.headingDegrees,
            verticalPhase: primary.verticalPhase,
            etaSummary: primary.etaSummary,
            elapsedSummary: primary.elapsedSummary,
            remainingSummary: primary.remainingSummary,
            scheduledDeparture: primary.scheduledDeparture,
            estimatedDeparture: primary.estimatedDeparture,
            actualDeparture: primary.actualDeparture,
            estimatedArrival: primary.estimatedArrival,
            actualArrival: primary.actualArrival,
            routeDistanceKilometers: primary.routeDistanceKilometers,
            originTerminal: primary.originTerminal ??
                supplemental.originTerminal,
            originGate: primary.originGate ?? supplemental.originGate,
            destinationTerminal: primary.destinationTerminal ??
                supplemental.destinationTerminal,
            destinationGate: primary.destinationGate ??
                supplemental.destinationGate
        )
    }

    private static func mergingTerminalConfirmation(
        primary: TrackedFlight,
        supplemental: TrackedFlight,
        at now: Date
    ) -> TrackedFlight {
        let arrival = supplemental.actualArrival ??
            supplemental.estimatedArrival ??
            primary.estimatedArrival ?? now
        let status = supplemental.liveStatusSummary
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let confirmed = TrackedFlight(
            airlineName: primary.airlineName,
            marketingNumber: primary.marketingNumber,
            operationalCallsign: primary.operationalCallsign,
            aircraftType: primary.aircraftType,
            aircraftTypeCode: primary.aircraftTypeCode,
            origin: primary.origin,
            destination: primary.destination,
            scheduleSummary: primary.scheduleSummary,
            durationSummary: primary.durationSummary,
            distanceSummary: primary.distanceSummary,
            liveStatusSummary: status.isEmpty ? "Arrived" : status,
            dataSourceSummary: sourceSummary(
                primary.dataSourceSummary,
                adding: "AirLabs"
            ),
            routeProgress: primary.routeProgress,
            lastPosition: primary.lastPosition,
            lastUpdated: newest(primary.lastUpdated, supplemental.lastUpdated),
            altitudeMeters: primary.altitudeMeters,
            groundSpeedKPH: primary.groundSpeedKPH,
            headingDegrees: primary.headingDegrees,
            verticalPhase: primary.verticalPhase,
            etaSummary: primary.etaSummary,
            elapsedSummary: primary.elapsedSummary,
            remainingSummary: primary.remainingSummary,
            scheduledDeparture: primary.scheduledDeparture,
            estimatedDeparture: primary.estimatedDeparture,
            actualDeparture: primary.actualDeparture ??
                supplemental.actualDeparture,
            estimatedArrival: primary.estimatedArrival ??
                supplemental.estimatedArrival,
            actualArrival: supplemental.actualArrival ?? arrival,
            routeDistanceKilometers: primary.routeDistanceKilometers,
            originTerminal: primary.originTerminal,
            originGate: primary.originGate,
            destinationTerminal: primary.destinationTerminal,
            destinationGate: primary.destinationGate
        )
        return confirmed.normalizedForTerminalState(
            status: status.isEmpty ? "Arrived" : status,
            at: arrival
        )
    }

    private static func merging(
        primary: TrackedFlight,
        telemetry: TrackedFlightTelemetry,
        source: String
    ) -> TrackedFlight {
        let telemetryProgress = telemetry.coordinate.map {
            GreatCircle.nearestProgress(
                of: $0,
                from: primary.origin.coordinate,
                to: primary.destination.coordinate
            )
        }
        // ADS-B can briefly jump backward or deliver an older point during a
        // receiver handoff. Route progress on one occurrence is monotonic.
        let routeProgress = max(
            primary.routeProgress ?? 0,
            telemetryProgress ?? primary.routeProgress ?? 0
        )
        return TrackedFlight(
            airlineName: primary.airlineName,
            marketingNumber: primary.marketingNumber,
            operationalCallsign: primary.operationalCallsign,
            aircraftType: primary.aircraftType,
            aircraftTypeCode: primary.aircraftTypeCode,
            origin: primary.origin,
            destination: primary.destination,
            scheduleSummary: primary.scheduleSummary,
            durationSummary: primary.durationSummary,
            distanceSummary: primary.distanceSummary,
            liveStatusSummary: primary.liveStatusSummary,
            dataSourceSummary: sourceSummary(
                primary.dataSourceSummary,
                adding: source
            ),
            routeProgress: routeProgress,
            lastPosition: telemetry.coordinate ?? primary.lastPosition,
            lastUpdated: newest(primary.lastUpdated, telemetry.updated),
            altitudeMeters:
                telemetry.altitudeMeters ?? primary.altitudeMeters,
            groundSpeedKPH:
                telemetry.groundSpeedKPH ?? primary.groundSpeedKPH,
            headingDegrees:
                telemetry.headingDegrees ?? primary.headingDegrees,
            verticalPhase:
                telemetry.verticalPhase ?? primary.verticalPhase,
            etaSummary: primary.etaSummary,
            elapsedSummary: primary.elapsedSummary,
            remainingSummary: primary.remainingSummary,
            scheduledDeparture: primary.scheduledDeparture,
            estimatedDeparture: primary.estimatedDeparture,
            actualDeparture: primary.actualDeparture,
            estimatedArrival: primary.estimatedArrival,
            actualArrival: primary.actualArrival,
            routeDistanceKilometers: primary.routeDistanceKilometers,
            originTerminal: primary.originTerminal,
            originGate: primary.originGate,
            destinationTerminal: primary.destinationTerminal,
            destinationGate: primary.destinationGate
        )
    }

    private static func sourceSummary(
        _ primary: String,
        adding supplemental: String
    ) -> String {
        let components = primary
            .split(separator: "+")
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        if components.contains(where: {
            $0.localizedCaseInsensitiveContains(supplemental)
        }) {
            return primary
        }
        let base = primary.isEmpty ? "AirLabs" : primary
        return "\(base) + \(supplemental)"
    }

    private static func newest(_ first: Date?, _ second: Date?) -> Date? {
        switch (first, second) {
        case (.some(let lhs), .some(let rhs)):
            return max(lhs, rhs)
        case (.some(let value), .none), (.none, .some(let value)):
            return value
        case (.none, .none):
            return nil
        }
    }
}

enum FlightFallbackError: LocalizedError {
    case bothProvidersFailed(primary: String, fallback: String)

    var errorDescription: String? {
        switch self {
        case .bothProvidersFailed(let primary, let fallback):
            "FlightAware failed (\(primary)); AirLabs/OpenSky also failed (\(fallback))."
        }
    }
}

struct AirLabsOpenSkyFlightStatusProvider: FlightLegProviding {
    let apiKey: String
    let openSkyClientID: String
    let openSkyClientSecret: String
    var session: URLSession = .shared
    var now: @Sendable () -> Date = { Date() }

    func trackedFlight(
        for marketingNumber: String,
        on selectedDate: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        let normalized = Self.normalizedFlightNumber(marketingNumber)
        guard !normalized.isEmpty else {
            throw FlightProviderError.unsupportedFlight(marketingNumber)
        }

        let cacheKey = Self.cacheKey(
            flightNumber: normalized,
            selectedDate: selectedDate,
            selection: selection
        )
        let refreshNow = now()
        let record: AirLabsFlightRecord
        if let cached = await AirLabsFlightCache.shared.record(
            for: cacheKey,
            at: refreshNow,
            validFor: { cached in
                Self.metadataRefreshInterval(
                    for: cached,
                    at: refreshNow
                )
            }
        ) {
            record = cached
        } else {
            record = try await fetchFlightWithRouteFallback(
                normalized,
                selectedDate: selectedDate,
                selection: selection
            )
            await AirLabsFlightCache.shared.store(
                record,
                for: cacheKey,
                at: refreshNow
            )
        }

        async let originResult = resolveAirport(
            iata: record.departureIATA,
            icao: record.departureICAO
        )
        async let destinationResult = resolveAirport(
            iata: record.arrivalIATA,
            icao: record.arrivalICAO
        )

        let origin = try await originResult
        let destination = try await destinationResult
        try validate(
            record: record,
            selectedDate: selectedDate,
            origin: origin,
            marketingNumber: marketingNumber
        )

        let preliminaryProgress = routeProgress(
            record: record,
            position: record.coordinate,
            origin: origin,
            destination: destination
        )
        let expectedCoordinate = record.coordinate ?? Self.routeCoordinate(
            origin: origin,
            destination: destination,
            progress: preliminaryProgress
        )
        var openSky = await openSkyState(for: record.hex)
        if openSky == nil,
           Self.normalizedStatus(record.status) == "enroute" {
            let callsigns = [
                record.flightICAO,
                record.codeshareFlightICAO,
                FlightAwareFlightStatusProvider.flightAwareIdent(
                    for: normalized
                ),
            ].compactMap { $0 }
            openSky = await openSkyState(
                forCallsigns: callsigns,
                near: expectedCoordinate
            )
        }

        /* The singular AirLabs flight endpoint may retain route/position
         * while omitting motion fields over sparse ADS-B coverage.  Only
         * after OpenSky misses do we spend the final fallback request on the
         * live flights endpoint. */
        let liveAirLabs: AirLabsFlightRecord? = if openSky == nil &&
            Self.missingMotionTelemetry(record) {
            await liveAirLabsState(normalized, expected: record)
        } else {
            nil
        }

        let position = openSky?.coordinate ??
            liveAirLabs?.coordinate ??
            record.coordinate
        let progress = routeProgress(
            record: record,
            position: position,
            origin: origin,
            destination: destination
        )
        let status = Self.effectiveStatus(record, now: now())
        let isAirborne = status == "enroute"
        let isArrived = status == "arrived"
        let scheduledDeparture = record.departureScheduled
        let estimatedDeparture = record.departureEstimated ?? scheduledDeparture
        let actualDeparture = record.departureActual ?? (
            isAirborne || isArrived ? estimatedDeparture : nil
        )
        let scheduledArrival = record.arrivalScheduled
        let estimatedArrival = record.arrivalEstimated ?? scheduledArrival
        let actualArrival = record.arrivalActual
        let routeDistanceKilometers = Self.routeDistanceKilometers(
            origin: origin,
            destination: destination
        )
        let aircraft = AircraftTypeCatalog.resolve(
            record.aircraftICAO ?? record.model
        )
        let marketing = normalized
        let operational = record.flightICAO ?? marketing
        let source = if openSky != nil {
            "AirLabs + OpenSky"
        } else if liveAirLabs != nil {
            "AirLabs live fallback"
        } else {
            "AirLabs fallback"
        }
        let updateTime = openSky?.lastContact ??
            liveAirLabs?.updated ??
            record.updated ??
            now()
        let altitude = openSky?.altitudeMeters ??
            liveAirLabs?.altitudeMeters ??
            record.altitudeMeters
        let speed = openSky?.groundSpeedKPH ??
            liveAirLabs?.groundSpeedKPH ??
            record.groundSpeedKPH
        let heading = openSky?.headingDegrees ??
            liveAirLabs?.headingDegrees ??
            record.headingDegrees
        let verticalRate = openSky?.verticalRateMPS ??
            liveAirLabs?.verticalRateMPS ??
            record.verticalRateMPS
        let durationSeconds = Self.durationSeconds(
            record: record,
            departure: actualDeparture ?? estimatedDeparture,
            arrival: actualArrival ?? estimatedArrival
        )
        let remainingKilometers = Int(round(
            Double(routeDistanceKilometers) * (1 - (progress ?? 0))
        ))

        return TrackedFlight(
            airlineName: Self.airlineName(
                iata: record.airlineIATA ?? String(marketing.prefix(2))
            ),
            marketingNumber: marketing,
            operationalCallsign: operational,
            aircraftType: aircraft?.label ?? record.model ?? "Aircraft type unavailable",
            aircraftTypeCode: aircraft?.code ?? record.aircraftICAO,
            origin: origin,
            destination: destination,
            scheduleSummary: "\(origin.code) → \(destination.code) · \(Self.displayStatus(status))",
            durationSummary: Self.durationString(durationSeconds) ?? "-",
            distanceSummary: "\(max(0, remainingKilometers)) km",
            liveStatusSummary: Self.liveStatusSummary(
                status: status,
                progress: progress,
                arrival: actualArrival ?? estimatedArrival,
                destinationTimeZone: destination.timeZoneIdentifier
            ),
            dataSourceSummary: source,
            routeProgress: progress,
            lastPosition: position,
            lastUpdated: updateTime,
            altitudeMeters: altitude,
            groundSpeedKPH: speed,
            headingDegrees: heading,
            verticalPhase: Self.verticalPhase(verticalRate),
            etaSummary: (actualArrival ?? estimatedArrival).map {
                Self.shortTimeString(
                    from: $0,
                    timeZoneIdentifier: destination.timeZoneIdentifier
                )
            },
            elapsedSummary: Self.elapsedSummary(
                departure: actualDeparture,
                arrival: actualArrival,
                now: now()
            ),
            remainingSummary: Self.remainingSummary(
                arrival: actualArrival ?? estimatedArrival,
                arrived: actualArrival != nil,
                now: now()
            ),
            scheduledDeparture: scheduledDeparture,
            estimatedDeparture: estimatedDeparture,
            actualDeparture: actualDeparture,
            estimatedArrival: estimatedArrival,
            actualArrival: actualArrival,
            routeDistanceKilometers: routeDistanceKilometers,
            originTerminal: record.departureTerminal,
            originGate: record.departureGate,
            destinationTerminal: record.arrivalTerminal,
            destinationGate: record.arrivalGate
        )
    }

    func flightLegs(
        for marketingNumber: String,
        on selectedDate: Date
    ) async throws -> [FlightLegCandidate] {
        let normalized = Self.normalizedFlightNumber(marketingNumber)
        let records = try await datedScheduleRecords(normalized)
        var legs = await legCandidates(
            from: records,
            marketingNumber: normalized,
            selectedDate: selectedDate,
            providerIdentifierPrefix: nil
        )
        if legs.isEmpty {
            let routes = try await recurringRouteRecords(
                normalized,
                selectedDate: selectedDate
            )
            legs = await legCandidates(
                from: routes,
                marketingNumber: normalized,
                selectedDate: selectedDate,
                providerIdentifierPrefix: "airlabs-route"
            )
        }
        return Array(Dictionary(grouping: legs, by: \.id).compactMap {
            $0.value.first
        }).sorted { $0.scheduledDeparture < $1.scheduledDeparture }
    }

    private func legCandidates(
        from records: [AirLabsFlightRecord],
        marketingNumber: String,
        selectedDate: Date,
        providerIdentifierPrefix: String?
    ) async -> [FlightLegCandidate] {
        var legs: [FlightLegCandidate] = []
        for record in records {
            guard let departure = Self.preferredDeparture(record),
                  let origin = try? await resolveAirport(
                      iata: record.departureIATA,
                      icao: record.departureICAO
                  ),
                  Self.sameDepartureDay(
                      selectedDate,
                      departure,
                      timeZoneIdentifier: origin.timeZoneIdentifier
                  ),
                  let originCode = record.departureIATA ??
                    record.departureICAO,
                  let destinationCode = record.arrivalIATA ??
                    record.arrivalICAO
            else { continue }
            let status = Self.effectiveStatus(record, now: now())
            let complete = status == "arrived"
            let identity = [
                marketingNumber,
                originCode.uppercased(),
                destinationCode.uppercased(),
                String(Int(departure.timeIntervalSince1970)),
            ].joined(separator: "|")
            legs.append(FlightLegCandidate(
                id: identity,
                marketingNumber: marketingNumber,
                originCode: originCode.uppercased(),
                destinationCode: destinationCode.uppercased(),
                scheduledDeparture: departure,
                departureTimeZoneIdentifier: origin.timeZoneIdentifier,
                statusSummary: Self.displayStatus(status),
                isComplete: complete,
                providerIdentifier: providerIdentifierPrefix.map {
                    "\($0):\(identity)"
                }
            ))
        }
        return legs
    }

    private func fetchFlightWithRouteFallback(
        _ marketingNumber: String,
        selectedDate: Date?,
        selection: FlightLegSelection?
    ) async throws -> AirLabsFlightRecord {
        do {
            return try await fetchFlight(
                marketingNumber,
                selectedDate: selectedDate,
                selection: selection
            )
        } catch FlightProviderError.noLiveFlight {
            guard let selectedDate else { throw FlightProviderError.noLiveFlight(marketingNumber) }
            let routes = try await recurringRouteRecords(
                marketingNumber,
                selectedDate: selectedDate
            )
            if let selection,
               let exact = routes.first(where: {
                   Self.matches($0, selection: selection)
               }) {
                return exact
            }
            guard selection == nil, routes.count == 1,
                  let onlyRoute = routes.first
            else {
                throw FlightProviderError.noLiveFlight(marketingNumber)
            }
            return onlyRoute
        }
    }

    private func fetchFlight(
        _ marketingNumber: String,
        selectedDate: Date?,
        selection: FlightLegSelection?
    ) async throws -> AirLabsFlightRecord {
        guard let selectedDate else {
            let object = try await airLabsObject(
                path: "flight",
                queryItems: [
                    URLQueryItem(name: "flight_iata", value: marketingNumber)
                ]
            )
            let record = try AirLabsFlightRecord(object: object)
            guard Self.matchesRequestedFlight(record, marketingNumber) else {
                throw FlightProviderError.noLiveFlight(marketingNumber)
            }
            return record
        }

        /* The singular /flight endpoint intentionally returns only the one
         * closest occurrence.  That is unsafe for a dated tracked flight,
         * especially around midnight or when a number operates repeatedly.
         * Schedules gives us candidates which are then matched to the
         * departure date in the origin airport's time zone. */
        let records = try await datedScheduleRecords(marketingNumber)
        var dated: [AirLabsFlightRecord] = []
        for record in records {
            guard let departure = Self.preferredDeparture(record),
                  let origin = try? await resolveAirport(
                    iata: record.departureIATA,
                    icao: record.departureICAO
                  ),
                  Self.sameDepartureDay(
                    selectedDate,
                    departure,
                    timeZoneIdentifier: origin.timeZoneIdentifier
                  )
            else { continue }
            dated.append(record)
        }
        if let selection,
           let exact = dated.first(where: {
               Self.matches($0, selection: selection)
           }) {
            return exact
        }
        if selection != nil {
            throw FlightProviderError.noLiveFlight(marketingNumber)
        }
        let current = now()
        if let active = dated.first(where: {
            Self.effectiveStatus($0, now: current) == "enroute"
        }) {
            return active
        }
        let upcomingFlights = dated.filter {
            (Self.preferredDeparture($0) ?? .distantPast) >=
                current.addingTimeInterval(-30 * 60)
        }
        if let upcoming = upcomingFlights.min(by: {
                (Self.preferredDeparture($0) ?? .distantFuture) <
                    (Self.preferredDeparture($1) ?? .distantFuture)
            }) {
            return upcoming
        }
        if let latest = dated.max(by: {
            (Self.preferredDeparture($0) ?? .distantPast) <
                (Self.preferredDeparture($1) ?? .distantPast)
        }) {
            return latest
        }
        throw FlightProviderError.noLiveFlight(marketingNumber)
    }

    private func datedScheduleRecords(
        _ marketingNumber: String
    ) async throws -> [AirLabsFlightRecord] {
        let rows = try await airLabsArray(
            path: "schedules",
            queryItems: [
                URLQueryItem(name: "flight_iata", value: marketingNumber),
                URLQueryItem(
                    name: "_fields",
                    value: "hex,reg_number,flight_iata,flight_icao," +
                        "cs_flight_iata,cs_flight_icao,airline_iata," +
                        "dep_iata,dep_icao,arr_iata,arr_icao," +
                        "aircraft_icao,model,status,lat,lng,alt,dir,speed," +
                        "v_speed,updated,dep_time_ts,dep_estimated_ts," +
                        "dep_actual_ts,arr_time_ts,arr_estimated_ts," +
                        "arr_actual_ts,duration,dep_terminal,dep_gate," +
                        "arr_terminal,arr_gate"
                ),
            ]
        )
        return rows.compactMap { try? AirLabsFlightRecord(object: $0) }
            .filter { Self.matchesRequestedFlight($0, marketingNumber) }
            .sorted {
                (Self.preferredDeparture($0) ?? .distantFuture) <
                    (Self.preferredDeparture($1) ?? .distantFuture)
            }
    }

    /// AirLabs' free dated schedule feed has a short forward-looking window.
    /// Its route catalog still describes recurring operations beyond that
    /// window, so use it only to prepare a provisional future flight. The
    /// ordinary provider cascade replaces this record with precise schedule,
    /// gate and aircraft data as the departure gets closer.
    private func recurringRouteRecords(
        _ marketingNumber: String,
        selectedDate: Date
    ) async throws -> [AirLabsFlightRecord] {
        let rows = try await airLabsArray(
            path: "routes",
            queryItems: [
                URLQueryItem(name: "flight_iata", value: marketingNumber),
                URLQueryItem(
                    name: "_fields",
                    value: "flight_iata,flight_icao,cs_flight_iata," +
                        "cs_flight_icao,airline_iata,dep_iata,dep_icao," +
                        "arr_iata,arr_icao,dep_time,arr_time,days," +
                        "aircraft_icao,model,duration"
                ),
            ]
        )
        let weekday = Self.weekdayCode(for: selectedDate)
        var records: [AirLabsFlightRecord] = []
        for row in rows {
            guard Self.rawRouteMatches(row, requested: marketingNumber),
                  Self.routeDays(row["days"]).contains(weekday),
                  let departureIATA = Self.string(row["dep_iata"]),
                  let arrivalIATA = Self.string(row["arr_iata"]),
                  let departureTime = Self.string(row["dep_time"]),
                  let arrivalTime = Self.string(row["arr_time"]),
                  let origin = try? await resolveAirport(
                      iata: departureIATA,
                      icao: Self.string(row["dep_icao"])
                  ),
                  let destination = try? await resolveAirport(
                      iata: arrivalIATA,
                      icao: Self.string(row["arr_icao"])
                  ),
                  let departure = Self.localDate(
                      selectedDate,
                      clock: departureTime,
                      timeZoneIdentifier: origin.timeZoneIdentifier
                  ),
                  var arrival = Self.localDate(
                      selectedDate,
                      clock: arrivalTime,
                      timeZoneIdentifier: destination.timeZoneIdentifier
                  )
            else { continue }

            while arrival <= departure {
                arrival = arrival.addingTimeInterval(24 * 3600)
            }
            records.append(AirLabsFlightRecord(
                routeObject: row,
                departureScheduled: departure,
                arrivalScheduled: arrival
            ))
        }
        return records.sorted {
            (Self.preferredDeparture($0) ?? .distantFuture) <
                (Self.preferredDeparture($1) ?? .distantFuture)
        }
    }

    private static func rawRouteMatches(
        _ row: [String: Any],
        requested: String
    ) -> Bool {
        let normalized = normalizedFlightNumber(requested)
        return [
            string(row["flight_iata"]),
            string(row["flight_icao"]),
            string(row["cs_flight_iata"]),
            string(row["cs_flight_icao"]),
        ].compactMap { $0 }
            .map(FlightNumberIdentity.canonical)
            .contains(normalized)
    }

    private static func routeDays(_ value: Any?) -> Set<String> {
        if let values = value as? [String] {
            return Set(values.map { $0.lowercased() })
        }
        if let value = string(value) {
            return Set(value.lowercased().split { $0 == "," || $0 == " " }
                .map(String.init))
        }
        return []
    }

    private static func weekdayCode(for date: Date) -> String {
        let codes = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
        let weekday = Calendar.current.component(.weekday, from: date)
        return codes[max(0, min(codes.count - 1, weekday - 1))]
    }

    private static func localDate(
        _ selectedDate: Date,
        clock: String,
        timeZoneIdentifier: String?
    ) -> Date? {
        let pieces = clock.split(separator: ":")
        guard pieces.count >= 2,
              let hour = Int(pieces[0]),
              let minute = Int(pieces[1])
        else { return nil }
        let requested = Calendar.current.dateComponents(
            [.year, .month, .day],
            from: selectedDate
        )
        var calendar = Calendar(identifier: .gregorian)
        if let timeZoneIdentifier,
           let timeZone = TimeZone(identifier: timeZoneIdentifier) {
            calendar.timeZone = timeZone
        }
        var components = DateComponents()
        components.calendar = calendar
        components.timeZone = calendar.timeZone
        components.year = requested.year
        components.month = requested.month
        components.day = requested.day
        components.hour = hour
        components.minute = minute
        return calendar.date(from: components)
    }

    private static func matchesRequestedFlight(
        _ record: AirLabsFlightRecord,
        _ requested: String
    ) -> Bool {
        let normalized = normalizedFlightNumber(requested)
        return [
            record.flightIATA,
            record.codeshareFlightIATA,
            record.flightICAO,
            record.codeshareFlightICAO,
        ].compactMap { $0 }
            .map(FlightNumberIdentity.canonical)
            .contains(normalized)
    }

    private static func preferredDeparture(
        _ record: AirLabsFlightRecord
    ) -> Date? {
        record.departureScheduled ?? record.departureEstimated ??
            record.departureActual
    }

    private static func matches(
        _ record: AirLabsFlightRecord,
        selection: FlightLegSelection
    ) -> Bool {
        guard let departure = preferredDeparture(record),
              let origin = (record.departureIATA ??
                record.departureICAO)?.uppercased(),
              let destination = (record.arrivalIATA ??
                record.arrivalICAO)?.uppercased(),
              origin == selection.originCode.uppercased(),
              destination == selection.destinationCode.uppercased()
        else { return false }
        return abs(departure.timeIntervalSince(selection.scheduledDeparture)) <
            2 * 3600
    }

    private static func sameDepartureDay(
        _ requested: Date,
        _ departure: Date,
        timeZoneIdentifier: String?
    ) -> Bool {
        let requestedComponents = Calendar.current.dateComponents(
            [.year, .month, .day],
            from: requested
        )
        var departureCalendar = Calendar(identifier: .gregorian)
        if let timeZoneIdentifier,
           let timeZone = TimeZone(identifier: timeZoneIdentifier) {
            departureCalendar.timeZone = timeZone
        }
        let departureComponents = departureCalendar.dateComponents(
            [.year, .month, .day],
            from: departure
        )
        return requestedComponents.year == departureComponents.year &&
            requestedComponents.month == departureComponents.month &&
            requestedComponents.day == departureComponents.day
    }

    private func fetchLiveFlight(
        _ marketingNumber: String,
        expected: AirLabsFlightRecord
    ) async throws -> AirLabsFlightRecord? {
        let rows = try await airLabsArray(
            path: "flights",
            queryItems: [
                URLQueryItem(
                    name: "flight_iata",
                    value: marketingNumber
                ),
                URLQueryItem(
                    name: "_fields",
                    value: "hex,reg_number,flight_iata,flight_icao," +
                        "cs_flight_iata,cs_flight_icao,airline_iata," +
                        "dep_iata,dep_icao,arr_iata,arr_icao," +
                        "aircraft_icao,status,lat,lng,alt,dir,speed," +
                        "v_speed,updated"
                ),
            ]
        )
        let candidates = rows.compactMap {
            try? AirLabsFlightRecord(object: $0)
        }
        return candidates
            .compactMap { candidate -> (AirLabsFlightRecord, Int)? in
                guard let score = Self.liveMatchScore(
                    candidate,
                    expected: expected,
                    requested: marketingNumber
                ) else { return nil }
                return (candidate, score)
            }
            .max {
                if $0.1 != $1.1 { return $0.1 < $1.1 }
                return ($0.0.updated ?? .distantPast) <
                    ($1.0.updated ?? .distantPast)
            }?.0
    }

    private func liveAirLabsState(
        _ marketingNumber: String,
        expected: AirLabsFlightRecord
    ) async -> AirLabsFlightRecord? {
        do {
            let record = try await fetchLiveFlight(
                marketingNumber,
                expected: expected
            )
            if record == nil {
                FileHandle.standardError.write(Data(
                    ("[airspace] AirLabs live endpoint returned no " +
                     "state for \(marketingNumber)\n").utf8
                ))
            }
            return record
        } catch {
            FileHandle.standardError.write(Data(
                ("[airspace] AirLabs live lookup failed: " +
                 "\(error.localizedDescription)\n").utf8
            ))
            return nil
        }
    }

    private static func liveMatchScore(
        _ candidate: AirLabsFlightRecord,
        expected: AirLabsFlightRecord,
        requested: String
    ) -> Int? {
        let exactHex = candidate.hex != nil &&
            candidate.hex == expected.hex
        if let expectedHex = expected.hex,
           let candidateHex = candidate.hex,
           expectedHex != candidateHex {
            return nil
        }
        let exactRegistration = candidate.registration != nil &&
            candidate.registration?.caseInsensitiveCompare(
                expected.registration ?? ""
            ) == .orderedSame
        if let expectedRegistration = expected.registration,
           let candidateRegistration = candidate.registration,
           expectedRegistration.caseInsensitiveCompare(candidateRegistration) !=
                .orderedSame {
            return nil
        }
        let exactFlight = matchesRequestedFlight(candidate, requested)
        guard exactHex || exactRegistration || exactFlight else { return nil }

        var score = exactHex ? 100 : 0
        score += exactRegistration ? 60 : 0
        score += exactFlight ? 30 : 0
        if candidate.departureIATA == expected.departureIATA { score += 10 }
        if candidate.arrivalIATA == expected.arrivalIATA { score += 10 }
        return score
    }

    private static func missingMotionTelemetry(
        _ record: AirLabsFlightRecord
    ) -> Bool {
        record.altitudeMeters == nil ||
            record.groundSpeedKPH == nil ||
            record.headingDegrees == nil
    }

    private func resolveAirport(iata: String?, icao: String?) async throws -> Airport {
        let normalizedIATA = iata?.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        let normalizedICAO = icao?.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        let lookupCode = normalizedIATA?.isEmpty == false ? normalizedIATA! : (normalizedICAO ?? "")
        guard !lookupCode.isEmpty else {
            throw AirLabsProviderError.missingAirport
        }
        if lookupCode == "ICN" || lookupCode == "RKSI" { return .icn }
        if lookupCode == "SFO" || lookupCode == "KSFO" { return .sfo }
        if let cached = await AirLabsAirportCache.shared.airport(for: lookupCode) {
            return cached
        }

        var items: [URLQueryItem]
        if let normalizedIATA, !normalizedIATA.isEmpty {
            items = [URLQueryItem(name: "iata_code", value: normalizedIATA)]
        } else {
            items = [URLQueryItem(name: "icao_code", value: normalizedICAO)]
        }
        items.append(URLQueryItem(
            name: "_fields",
            value: "name,iata_code,icao_code,lat,lng,city,timezone,country_code"
        ))
        let rows = try await airLabsArray(path: "airports", queryItems: items)
        guard let row = rows.first,
              let latitude = Self.double(row["lat"]),
              let longitude = Self.double(row["lng"])
        else {
            throw AirLabsProviderError.airportNotFound(lookupCode)
        }
        let airport = Airport(
            code: Self.string(row["iata_code"]) ?? normalizedIATA ?? normalizedICAO ?? lookupCode,
            name: Self.string(row["name"]) ?? lookupCode,
            city: Self.string(row["city"]) ?? "",
            coordinate: CLLocationCoordinate2D(
                latitude: latitude,
                longitude: longitude
            ),
            countryCode: Self.string(row["country_code"])?.uppercased(),
            timeZoneIdentifier: Self.string(row["timezone"])
        )
        await AirLabsAirportCache.shared.store(airport, for: lookupCode)
        if airport.code != lookupCode {
            await AirLabsAirportCache.shared.store(airport, for: airport.code)
        }
        return airport
    }

    private func openSkyState(for hex: String?) async -> OpenSkyFlightState? {
        guard let hex, !hex.isEmpty else { return nil }
        do {
            return try await OpenSkyFlightStateClient(
                clientID: openSkyClientID,
                clientSecret: openSkyClientSecret,
                session: session
            ).state(for: hex)
        } catch {
            return nil
        }
    }

    private func openSkyState(
        forCallsigns callsigns: [String],
        near coordinate: CLLocationCoordinate2D
    ) async -> OpenSkyFlightState? {
        guard !callsigns.isEmpty else { return nil }
        do {
            let state = try await OpenSkyFlightStateClient(
                clientID: openSkyClientID,
                clientSecret: openSkyClientSecret,
                session: session
            ).state(
                forCallsigns: callsigns,
                near: coordinate
            )
            if state == nil {
                FileHandle.standardError.write(Data(
                    ("[airspace] OpenSky returned no live state for " +
                     "\(callsigns.joined(separator: ",")) near " +
                     "\(String(format: "%.2f", coordinate.latitude))," +
                     "\(String(format: "%.2f", coordinate.longitude))\n").utf8
                ))
            }
            return state
        } catch {
            FileHandle.standardError.write(Data(
                ("[airspace] OpenSky callsign lookup failed: " +
                 "\(error.localizedDescription)\n").utf8
            ))
            return nil
        }
    }

    private static func routeCoordinate(
        origin: Airport,
        destination: Airport,
        progress: Double?
    ) -> CLLocationCoordinate2D {
        let points = GreatCircle.points(
            from: origin.coordinate,
            to: destination.coordinate,
            count: 101
        )
        let clamped = min(1, max(0, progress ?? 0.5))
        let index = min(
            points.count - 1,
            max(0, Int(round(clamped * Double(points.count - 1))))
        )
        return points[index]
    }

    private func airLabsObject(
        path: String,
        queryItems: [URLQueryItem]
    ) async throws -> [String: Any] {
        let root = try await airLabsJSON(path: path, queryItems: queryItems)
        if let response = root as? [String: Any] {
            return response
        }
        throw AirLabsProviderError.badPayload
    }

    private func airLabsArray(
        path: String,
        queryItems: [URLQueryItem]
    ) async throws -> [[String: Any]] {
        let root = try await airLabsJSON(path: path, queryItems: queryItems)
        if let rows = root as? [[String: Any]] {
            return rows
        }
        throw AirLabsProviderError.badPayload
    }

    private func airLabsJSON(
        path: String,
        queryItems: [URLQueryItem]
    ) async throws -> Any {
        var components = URLComponents(string: "https://airlabs.co/api/v9/\(path)")!
        components.queryItems = queryItems + [
            URLQueryItem(name: "api_key", value: apiKey)
        ]
        guard let url = components.url else {
            throw AirLabsProviderError.badURL
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 20
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw AirLabsProviderError.badResponse
        }
        guard (200..<300).contains(http.statusCode) else {
            throw AirLabsProviderError.httpStatus(
                http.statusCode,
                String(data: data, encoding: .utf8) ?? ""
            )
        }
        let json = try JSONSerialization.jsonObject(with: data)
        if let envelope = json as? [String: Any] {
            if let error = envelope["error"] as? [String: Any] {
                throw AirLabsProviderError.api(
                    Self.string(error["message"]) ?? "Unknown AirLabs error"
                )
            }
            if let response = envelope["response"] {
                return response
            }
        }
        return json
    }

    private func validate(
        record: AirLabsFlightRecord,
        selectedDate: Date?,
        origin: Airport,
        marketingNumber: String
    ) throws {
        guard let selectedDate,
              let departure = record.departureScheduled ?? record.departureEstimated ?? record.departureActual
        else { return }
        let selected = Calendar.current.dateComponents(
            [.year, .month, .day],
            from: selectedDate
        )
        var departureCalendar = Calendar(identifier: .gregorian)
        if let identifier = origin.timeZoneIdentifier,
           let timeZone = TimeZone(identifier: identifier) {
            departureCalendar.timeZone = timeZone
        }
        let actual = departureCalendar.dateComponents(
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

    private func routeProgress(
        record: AirLabsFlightRecord,
        position: CLLocationCoordinate2D?,
        origin: Airport,
        destination: Airport
    ) -> Double? {
        if let position {
            return GreatCircle.nearestProgress(
                of: position,
                from: origin.coordinate,
                to: destination.coordinate
            )
        }
        guard let departure = record.departureActual ?? record.departureEstimated ?? record.departureScheduled,
              let arrival = record.arrivalActual ?? record.arrivalEstimated ?? record.arrivalScheduled,
              arrival > departure
        else { return nil }
        return min(1, max(0, now().timeIntervalSince(departure) / arrival.timeIntervalSince(departure)))
    }

    private static func normalizedFlightNumber(_ value: String) -> String {
        FlightNumberIdentity.canonical(value)
    }

    private static func cacheKey(
        flightNumber: String,
        selectedDate: Date?,
        selection: FlightLegSelection?
    ) -> String {
        guard let selectedDate else { return flightNumber }
        let formatter = DateFormatter()
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        let base = "\(flightNumber)|\(formatter.string(from: selectedDate))"
        guard let selection else { return base }
        return [
            base,
            selection.originCode.uppercased(),
            selection.destinationCode.uppercased(),
            String(Int(selection.scheduledDeparture.timeIntervalSince1970)),
        ].joined(separator: "|")
    }

    private static func normalizedStatus(_ value: String?) -> String {
        switch value?.lowercased() {
        case "en-route", "enroute", "active", "airborne": "enroute"
        case "landed", "arrived": "arrived"
        case "cancelled", "canceled": "cancelled"
        default: "scheduled"
        }
    }

    private static func effectiveStatus(
        _ record: AirLabsFlightRecord,
        now: Date
    ) -> String {
        let normalized = normalizedStatus(record.status)
        if record.arrivalActual != nil {
            return "arrived"
        }
        if normalized == "cancelled" {
            return normalized
        }
        if record.departureActual != nil {
            return normalized == "arrived" ? "arrived" : "enroute"
        }

        let tolerance = now.addingTimeInterval(5 * 60)
        let departure = record.departureEstimated ??
            record.departureScheduled
        let arrival = record.arrivalEstimated ?? record.arrivalScheduled
        let departureHasStarted = departure.map { $0 <= tolerance } ?? false
        let arrivalHasPassed = arrival.map { $0 <= tolerance } ?? false

        if normalized == "arrived" {
            return departureHasStarted && arrivalHasPassed
                ? "arrived" : "scheduled"
        }
        if normalized == "enroute" {
            return departureHasStarted ? "enroute" : "scheduled"
        }
        return normalized
    }

    private static func metadataRefreshInterval(
        for record: AirLabsFlightRecord,
        at now: Date
    ) -> TimeInterval {
        guard effectiveStatus(record, now: now) == "enroute" else {
            return 10 * 60
        }

        let departure = record.departureActual ??
            record.departureEstimated ?? record.departureScheduled
        let arrival = record.arrivalEstimated ??
            record.arrivalScheduled ?? record.arrivalActual

        // Once the expected arrival is stale, return to the shorter terminal
        // state check so a completed flight is not left airborne for an hour.
        if let arrival, now >= arrival.addingTimeInterval(15 * 60) {
            return 10 * 60
        }

        let duration = record.durationMinutes.map {
            TimeInterval($0 * 60)
        } ?? departure.flatMap { departure in
            arrival.map { $0.timeIntervalSince(departure) }
        }
        return (duration ?? 0) > 3 * 3600 ? 60 * 60 : 30 * 60
    }

    private static func displayStatus(_ status: String) -> String {
        switch status {
        case "enroute": "En route"
        case "arrived": "Arrived"
        case "cancelled": "Cancelled"
        default: "Scheduled"
        }
    }

    private static func liveStatusSummary(
        status: String,
        progress: Double?,
        arrival: Date?,
        destinationTimeZone: String?
    ) -> String {
        var parts = [displayStatus(status)]
        if let progress {
            parts.append("\(Int(round(progress * 100)))%")
        }
        if let arrival {
            parts.append("ETA \(shortTimeString(from: arrival, timeZoneIdentifier: destinationTimeZone))")
        }
        return parts.joined(separator: " · ")
    }

    private static func verticalPhase(_ rate: Double?) -> String? {
        guard let rate else { return nil }
        if rate > 0.75 { return "climbing" }
        if rate < -0.75 { return "descending" }
        return "cruising"
    }

    private static func routeDistanceKilometers(origin: Airport, destination: Airport) -> Int {
        let start = CLLocation(
            latitude: origin.coordinate.latitude,
            longitude: origin.coordinate.longitude
        )
        let end = CLLocation(
            latitude: destination.coordinate.latitude,
            longitude: destination.coordinate.longitude
        )
        return Int(round(start.distance(from: end) / 1000))
    }

    private static func durationSeconds(
        record: AirLabsFlightRecord,
        departure: Date?,
        arrival: Date?
    ) -> TimeInterval? {
        if let minutes = record.durationMinutes, minutes > 0 {
            return TimeInterval(minutes * 60)
        }
        guard let departure, let arrival, arrival > departure else { return nil }
        return arrival.timeIntervalSince(departure)
    }

    private static func durationString(_ interval: TimeInterval?) -> String? {
        guard let interval else { return nil }
        let formatter = DateComponentsFormatter()
        formatter.allowedUnits = [.hour, .minute]
        formatter.unitsStyle = .abbreviated
        formatter.maximumUnitCount = 2
        return formatter.string(from: interval)
    }

    private static func elapsedSummary(
        departure: Date?,
        arrival: Date?,
        now: Date
    ) -> String? {
        guard let departure else { return nil }
        return durationString(max(0, (arrival ?? now).timeIntervalSince(departure)))
    }

    private static func remainingSummary(
        arrival: Date?,
        arrived: Bool,
        now: Date
    ) -> String? {
        if arrived { return "Arrived" }
        guard let arrival else { return nil }
        return durationString(max(0, arrival.timeIntervalSince(now)))
    }

    private static func shortTimeString(
        from date: Date,
        timeZoneIdentifier: String?
    ) -> String {
        let formatter = DateFormatter()
        formatter.timeStyle = .short
        formatter.dateStyle = .none
        if let timeZoneIdentifier,
           let timeZone = TimeZone(identifier: timeZoneIdentifier) {
            formatter.timeZone = timeZone
        }
        return formatter.string(from: date)
    }

    private static func airlineName(iata: String) -> String {
        let names: [String: String] = [
            "AA": "American Airlines", "AS": "Alaska Airlines",
            "DL": "Delta Air Lines", "KE": "Korean Air",
            "LH": "Lufthansa", "OO": "SkyWest Airlines",
            "UA": "United Airlines", "WN": "Southwest Airlines",
            "YP": "Air Premia", "FX": "FedEx Express",
            "5X": "UPS Airlines",
        ]
        let normalized = iata.uppercased()
        return names[normalized] ?? normalized
    }

    fileprivate static func string(_ value: Any?) -> String? {
        if let value = value as? String, !value.isEmpty { return value }
        if let value = value as? NSNumber { return value.stringValue }
        return nil
    }

    fileprivate static func double(_ value: Any?) -> Double? {
        if let value = value as? NSNumber { return value.doubleValue }
        if let value = value as? String { return Double(value) }
        return nil
    }
}

private struct AirLabsFlightRecord: @unchecked Sendable {
    let hex: String?
    let registration: String?
    let flightIATA: String?
    let flightICAO: String?
    let codeshareFlightIATA: String?
    let codeshareFlightICAO: String?
    let airlineIATA: String?
    let departureIATA: String?
    let departureICAO: String?
    let arrivalIATA: String?
    let arrivalICAO: String?
    let aircraftICAO: String?
    let model: String?
    let status: String?
    let latitude: Double?
    let longitude: Double?
    let altitudeMeters: Int?
    let headingDegrees: Int?
    let groundSpeedKPH: Int?
    let verticalRateMPS: Double?
    let updated: Date?
    let departureScheduled: Date?
    let departureEstimated: Date?
    let departureActual: Date?
    let arrivalScheduled: Date?
    let arrivalEstimated: Date?
    let arrivalActual: Date?
    let durationMinutes: Int?
    let departureTerminal: String?
    let departureGate: String?
    let arrivalTerminal: String?
    let arrivalGate: String?

    var coordinate: CLLocationCoordinate2D? {
        guard let latitude, let longitude else { return nil }
        return CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }

    init(object: [String: Any]) throws {
        flightIATA = AirLabsOpenSkyFlightStatusProvider.string(object["flight_iata"])
        flightICAO = AirLabsOpenSkyFlightStatusProvider.string(object["flight_icao"])
        guard flightIATA != nil || flightICAO != nil else {
            throw AirLabsProviderError.badPayload
        }
        hex = AirLabsOpenSkyFlightStatusProvider.string(object["hex"])?.lowercased()
        registration = AirLabsOpenSkyFlightStatusProvider.string(
            object["reg_number"]
        )?.uppercased()
        codeshareFlightIATA = AirLabsOpenSkyFlightStatusProvider.string(
            object["cs_flight_iata"]
        )
        codeshareFlightICAO = AirLabsOpenSkyFlightStatusProvider.string(
            object["cs_flight_icao"]
        )
        airlineIATA = AirLabsOpenSkyFlightStatusProvider.string(object["airline_iata"])
        departureIATA = AirLabsOpenSkyFlightStatusProvider.string(object["dep_iata"])
        departureICAO = AirLabsOpenSkyFlightStatusProvider.string(object["dep_icao"])
        arrivalIATA = AirLabsOpenSkyFlightStatusProvider.string(object["arr_iata"])
        arrivalICAO = AirLabsOpenSkyFlightStatusProvider.string(object["arr_icao"])
        aircraftICAO = AirLabsOpenSkyFlightStatusProvider.string(object["aircraft_icao"])
        model = AirLabsOpenSkyFlightStatusProvider.string(object["model"])
        status = AirLabsOpenSkyFlightStatusProvider.string(object["status"])
        latitude = AirLabsOpenSkyFlightStatusProvider.double(object["lat"])
        longitude = AirLabsOpenSkyFlightStatusProvider.double(object["lng"])
        altitudeMeters = Self.integer(object["alt"])
        headingDegrees = Self.integer(object["dir"])
        groundSpeedKPH = Self.integer(object["speed"])
        verticalRateMPS = AirLabsOpenSkyFlightStatusProvider.double(object["v_speed"])
        updated = Self.date(object["updated"])
        departureScheduled = Self.date(object["dep_time_ts"])
        departureEstimated = Self.date(object["dep_estimated_ts"])
        departureActual = Self.date(object["dep_actual_ts"])
        arrivalScheduled = Self.date(object["arr_time_ts"])
        arrivalEstimated = Self.date(object["arr_estimated_ts"])
        arrivalActual = Self.date(object["arr_actual_ts"])
        durationMinutes = Self.integer(object["duration"])
        departureTerminal = AirLabsOpenSkyFlightStatusProvider.string(
            object["dep_terminal"]
        )
        departureGate = AirLabsOpenSkyFlightStatusProvider.string(
            object["dep_gate"]
        )
        arrivalTerminal = AirLabsOpenSkyFlightStatusProvider.string(
            object["arr_terminal"]
        )
        arrivalGate = AirLabsOpenSkyFlightStatusProvider.string(
            object["arr_gate"]
        )
    }

    init(
        routeObject object: [String: Any],
        departureScheduled: Date,
        arrivalScheduled: Date
    ) {
        hex = nil
        registration = nil
        flightIATA = AirLabsOpenSkyFlightStatusProvider.string(object["flight_iata"])
        flightICAO = AirLabsOpenSkyFlightStatusProvider.string(object["flight_icao"])
        codeshareFlightIATA = AirLabsOpenSkyFlightStatusProvider.string(
            object["cs_flight_iata"]
        )
        codeshareFlightICAO = AirLabsOpenSkyFlightStatusProvider.string(
            object["cs_flight_icao"]
        )
        airlineIATA = AirLabsOpenSkyFlightStatusProvider.string(object["airline_iata"])
        departureIATA = AirLabsOpenSkyFlightStatusProvider.string(object["dep_iata"])
        departureICAO = AirLabsOpenSkyFlightStatusProvider.string(object["dep_icao"])
        arrivalIATA = AirLabsOpenSkyFlightStatusProvider.string(object["arr_iata"])
        arrivalICAO = AirLabsOpenSkyFlightStatusProvider.string(object["arr_icao"])
        aircraftICAO = AirLabsOpenSkyFlightStatusProvider.string(object["aircraft_icao"])
        model = AirLabsOpenSkyFlightStatusProvider.string(object["model"])
        status = "scheduled"
        latitude = nil
        longitude = nil
        altitudeMeters = nil
        headingDegrees = nil
        groundSpeedKPH = nil
        verticalRateMPS = nil
        updated = nil
        self.departureScheduled = departureScheduled
        departureEstimated = nil
        departureActual = nil
        self.arrivalScheduled = arrivalScheduled
        arrivalEstimated = nil
        arrivalActual = nil
        durationMinutes = Self.integer(object["duration"])
        departureTerminal = nil
        departureGate = nil
        arrivalTerminal = nil
        arrivalGate = nil
    }

    private static func integer(_ value: Any?) -> Int? {
        AirLabsOpenSkyFlightStatusProvider.double(value).map { Int(round($0)) }
    }

    private static func date(_ value: Any?) -> Date? {
        AirLabsOpenSkyFlightStatusProvider.double(value).map {
            Date(timeIntervalSince1970: $0)
        }
    }
}

private actor AirLabsFlightCache {
    static let shared = AirLabsFlightCache()
    private struct Entry {
        let record: AirLabsFlightRecord
        let fetchedAt: Date
    }
    private var entries: [String: Entry] = [:]

    func record(
        for key: String,
        at now: Date,
        validFor: @Sendable (AirLabsFlightRecord) -> TimeInterval
    ) -> AirLabsFlightRecord? {
        guard let entry = entries[key],
              now.timeIntervalSince(entry.fetchedAt) < validFor(entry.record)
        else { return nil }
        return entry.record
    }

    func store(_ record: AirLabsFlightRecord, for key: String, at date: Date) {
        entries[key] = Entry(record: record, fetchedAt: date)
    }
}

private actor AirLabsAirportCache {
    static let shared = AirLabsAirportCache()
    private var airports: [String: Airport] = [:]

    func airport(for code: String) -> Airport? { airports[code.uppercased()] }
    func store(_ airport: Airport, for code: String) {
        airports[code.uppercased()] = airport
    }
}

private struct OpenSkyFlightState: Sendable {
    let coordinate: CLLocationCoordinate2D
    let altitudeMeters: Int?
    let groundSpeedKPH: Int?
    let headingDegrees: Int?
    let verticalRateMPS: Double?
    let lastContact: Date?
}

struct OpenSkyTrackedFlightTelemetryProvider:
    TrackedFlightTelemetryProviding {
    let clientID: String
    let clientSecret: String
    var session: URLSession = .shared

    func telemetry(for flight: TrackedFlight) async throws
        -> TrackedFlightTelemetry? {
        let points = GreatCircle.points(
            from: flight.origin.coordinate,
            to: flight.destination.coordinate,
            count: 101
        )
        let progress = min(1, max(0, flight.routeProgress ?? 0.5))
        let index = min(
            points.count - 1,
            max(0, Int(round(progress * Double(points.count - 1))))
        )
        let estimatedCoordinate = points[index]
        let operational = flight.operationalCallsign
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        let mapped = FlightAwareFlightStatusProvider.flightAwareIdent(
            for: flight.marketingNumber
        )
        let callsigns = Array(
            Set([operational, mapped].filter { !$0.isEmpty })
        )
        guard !callsigns.isEmpty else { return nil }

        let state = try await OpenSkyFlightStateClient(
            clientID: clientID,
            clientSecret: clientSecret,
            session: session
        ).state(
            forCallsigns: callsigns,
            near: estimatedCoordinate
        )
        guard let state else { return nil }
        return TrackedFlightTelemetry(
            coordinate: state.coordinate,
            altitudeMeters: state.altitudeMeters,
            groundSpeedKPH: state.groundSpeedKPH,
            headingDegrees: state.headingDegrees,
            verticalPhase: Self.verticalPhase(state.verticalRateMPS),
            updated: state.lastContact
        )
    }

    private static func verticalPhase(_ rate: Double?) -> String? {
        guard let rate else { return nil }
        if rate > 1.0 { return "climbing" }
        if rate < -1.0 { return "descending" }
        return "cruising"
    }
}

private struct OpenSkyFlightStateClient: Sendable {
    let clientID: String
    let clientSecret: String
    let session: URLSession

    func state(for hex: String) async throws -> OpenSkyFlightState? {
        let token = try await accessTokenIfConfigured()
        var components = URLComponents(
            string: "https://opensky-network.org/api/states/all"
        )!
        components.queryItems = [
            URLQueryItem(name: "icao24", value: hex.lowercased()),
            URLQueryItem(name: "extended", value: "1"),
        ]
        guard let url = components.url else { throw OpenSkyFlightError.badURL }
        var request = URLRequest(url: url)
        request.timeoutInterval = 15
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if let token {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        let (data, response) = try await session.data(for: request)
        try Self.validate(response: response, data: data)
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let rows = root["states"] as? [[Any?]],
              let row = rows.first
        else { return nil }
        return Self.state(from: row)
    }

    func state(
        forCallsigns callsigns: [String],
        near coordinate: CLLocationCoordinate2D
    ) async throws -> OpenSkyFlightState? {
        let wanted = Set(callsigns.map {
            $0.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        })
        guard !wanted.isEmpty else { return nil }
        let token = try await accessTokenIfConfigured()
        let minLatitude = max(-90, coordinate.latitude - 8)
        let maxLatitude = min(90, coordinate.latitude + 8)
        let longitudeRanges = Self.longitudeRanges(
            center: coordinate.longitude,
            halfWidth: 14
        )

        for range in longitudeRanges {
            var components = URLComponents(
                string: "https://opensky-network.org/api/states/all"
            )!
            components.queryItems = [
                URLQueryItem(name: "lamin", value: String(format: "%.4f", minLatitude)),
                URLQueryItem(name: "lomin", value: String(format: "%.4f", range.lowerBound)),
                URLQueryItem(name: "lamax", value: String(format: "%.4f", maxLatitude)),
                URLQueryItem(name: "lomax", value: String(format: "%.4f", range.upperBound)),
                URLQueryItem(name: "extended", value: "1"),
            ]
            guard let url = components.url else {
                throw OpenSkyFlightError.badURL
            }
            var request = URLRequest(url: url)
            request.timeoutInterval = 15
            request.setValue("application/json", forHTTPHeaderField: "Accept")
            if let token {
                request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
            }
            let (data, response) = try await session.data(for: request)
            try Self.validate(response: response, data: data)
            guard let root = try JSONSerialization.jsonObject(with: data)
                as? [String: Any],
                  let rows = root["states"] as? [[Any?]]
            else {
                throw OpenSkyFlightError.badPayload
            }
            for row in rows {
                guard row.indices.contains(1),
                      let rawCallsign = row[1] as? String,
                      wanted.contains(
                        rawCallsign
                            .trimmingCharacters(in: .whitespacesAndNewlines)
                            .uppercased()
                      )
                else { continue }
                if let state = Self.state(from: row) {
                    return state
                }
            }
        }
        return nil
    }

    private static func state(from row: [Any?]) -> OpenSkyFlightState? {
        guard row.indices.contains(11),
              let longitude = double(row[5]),
              let latitude = double(row[6])
        else { return nil }
        let lastContactSeconds = Self.double(row[4])
        if let lastContactSeconds,
           Date().timeIntervalSince1970 - lastContactSeconds > 180 {
            return nil
        }
        return OpenSkyFlightState(
            coordinate: CLLocationCoordinate2D(latitude: latitude, longitude: longitude),
            altitudeMeters: Self.double(row[7]).map { Int(round($0)) },
            groundSpeedKPH: Self.double(row[9]).map { Int(round($0 * 3.6)) },
            headingDegrees: Self.double(row[10]).map { Int(round($0)) % 360 },
            verticalRateMPS: Self.double(row[11]),
            lastContact: lastContactSeconds.map(Date.init(timeIntervalSince1970:))
        )
    }

    private static func longitudeRanges(
        center: Double,
        halfWidth: Double
    ) -> [ClosedRange<Double>] {
        let normalized = ((center + 180).truncatingRemainder(dividingBy: 360) + 360)
            .truncatingRemainder(dividingBy: 360) - 180
        let lower = normalized - halfWidth
        let upper = normalized + halfWidth
        if lower < -180 {
            return [(lower + 360)...180, -180...upper]
        }
        if upper > 180 {
            return [lower...180, -180...(upper - 360)]
        }
        return [lower...upper]
    }

    private func accessTokenIfConfigured() async throws -> String? {
        let id = clientID.trimmingCharacters(in: .whitespacesAndNewlines)
        let secret = clientSecret.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !id.isEmpty, !secret.isEmpty else { return nil }
        if let cached = await OpenSkyFlightTokenCache.shared.token(
            clientID: id,
            at: Date()
        ) {
            return cached
        }
        guard let url = URL(string: "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token") else {
            throw OpenSkyFlightError.badURL
        }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 15
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = [
            "grant_type=client_credentials",
            "client_id=\(Self.formEncode(id))",
            "client_secret=\(Self.formEncode(secret))",
        ].joined(separator: "&").data(using: .utf8)
        let (data, response) = try await session.data(for: request)
        try Self.validate(response: response, data: data)
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let token = root["access_token"] as? String,
              !token.isEmpty
        else { throw OpenSkyFlightError.badPayload }
        let expires = (root["expires_in"] as? NSNumber)?.doubleValue ?? 1800
        await OpenSkyFlightTokenCache.shared.store(
            token,
            clientID: id,
            expiresAt: Date().addingTimeInterval(max(60, expires - 30))
        )
        return token
    }

    private static func validate(response: URLResponse, data: Data) throws {
        guard let http = response as? HTTPURLResponse else {
            throw OpenSkyFlightError.badResponse
        }
        guard (200..<300).contains(http.statusCode) else {
            throw OpenSkyFlightError.httpStatus(
                http.statusCode,
                String(data: data, encoding: .utf8) ?? ""
            )
        }
    }

    private static func double(_ value: Any?) -> Double? {
        if let value = value as? NSNumber { return value.doubleValue }
        if let value = value as? Double { return value }
        return nil
    }

    private static func formEncode(_ value: String) -> String {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }
}

private actor OpenSkyFlightTokenCache {
    static let shared = OpenSkyFlightTokenCache()
    private struct Entry {
        let token: String
        let expiresAt: Date
    }
    private var entries: [String: Entry] = [:]

    func token(clientID: String, at date: Date) -> String? {
        guard let entry = entries[clientID], entry.expiresAt > date else { return nil }
        return entry.token
    }

    func store(_ token: String, clientID: String, expiresAt: Date) {
        entries[clientID] = Entry(token: token, expiresAt: expiresAt)
    }
}

enum AirLabsProviderError: LocalizedError {
    case badURL
    case badResponse
    case badPayload
    case httpStatus(Int, String)
    case api(String)
    case missingAirport
    case airportNotFound(String)

    var errorDescription: String? {
        switch self {
        case .badURL: "Could not build AirLabs request URL."
        case .badResponse: "AirLabs returned a non-HTTP response."
        case .badPayload: "AirLabs returned an unexpected payload."
        case .httpStatus(let status, let body): "AirLabs HTTP \(status): \(body)"
        case .api(let message): "AirLabs: \(message)"
        case .missingAirport: "AirLabs did not return both route airports."
        case .airportNotFound(let code): "AirLabs airport \(code) was not found."
        }
    }
}

private enum OpenSkyFlightError: LocalizedError {
    case badURL
    case badResponse
    case badPayload
    case httpStatus(Int, String)
}
