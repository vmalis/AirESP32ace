// AirESP32ace
// Created by VM, August 2026

import Foundation

/// Device-owned runtime state. Reading it never performs provider requests or
/// changes the companion's prescribed queue.
struct DeviceTrackerSnapshot: Decodable, Sendable {
    let deviceTrackingSupported: Bool
    let preparedFlightCount: Int
    let planId: UUID?
    let flightNumber: String?
    let status: String?
    let journeyPhase: String?
    let source: String?
    let estimatedPosition: Bool?
    let routeProgress: Double?
    let positionUnix: Int64?
    let checkedUnix: Int64?
    let altitudeMeters: Int?
    let speedKPH: Int?
    let headingDegrees: Int?
    let actualDepartureUnix: Int64?
    let estimatedDepartureUnix: Int64?
    let estimatedArrivalUnix: Int64?
    let actualArrivalUnix: Int64?
    let originTerminal: String?
    let originGate: String?
    let destinationTerminal: String?
    let destinationGate: String?

    func updating(_ flight: TrackedFlight) -> TrackedFlight {
        func date(_ timestamp: Int64?) -> Date? {
            guard let timestamp, timestamp > 0 else { return nil }
            return Date(timeIntervalSince1970: Double(timestamp))
        }
        let arrival = date(estimatedArrivalUnix) ?? flight.estimatedArrival
        let eta: String? = arrival.map {
            let formatter = DateFormatter()
            formatter.dateFormat = "HH:mm"
            formatter.timeZone = flight.destination.timeZoneIdentifier.flatMap(TimeZone.init(identifier:))
            return formatter.string(from: $0)
        }
        return TrackedFlight(
            airlineName: flight.airlineName,
            marketingNumber: flight.marketingNumber,
            operationalCallsign: flight.operationalCallsign,
            aircraftType: flight.aircraftType,
            aircraftTypeCode: flight.aircraftTypeCode,
            origin: flight.origin, destination: flight.destination,
            scheduleSummary: flight.scheduleSummary, durationSummary: flight.durationSummary,
            distanceSummary: flight.distanceSummary,
            liveStatusSummary: status ?? flight.liveStatusSummary,
            dataSourceSummary: "Device · \(source ?? "Tracking")" + (estimatedPosition == true ? " · estimated position" : ""),
            routeProgress: routeProgress ?? flight.routeProgress,
            lastPosition: nil, lastUpdated: date(checkedUnix),
            altitudeMeters: altitudeMeters ?? flight.altitudeMeters,
            groundSpeedKPH: speedKPH ?? flight.groundSpeedKPH,
            headingDegrees: headingDegrees ?? flight.headingDegrees,
            verticalPhase: journeyPhase == "arrived" ? "landed" : flight.verticalPhase,
            etaSummary: eta ?? flight.etaSummary,
            elapsedSummary: flight.elapsedSummary, remainingSummary: flight.remainingSummary,
            scheduledDeparture: flight.scheduledDeparture,
            estimatedDeparture: date(estimatedDepartureUnix) ?? flight.estimatedDeparture,
            actualDeparture: date(actualDepartureUnix) ?? flight.actualDeparture,
            estimatedArrival: arrival,
            actualArrival: date(actualArrivalUnix) ?? flight.actualArrival,
            routeDistanceKilometers: flight.routeDistanceKilometers,
            originTerminal: originTerminal ?? flight.originTerminal,
            originGate: originGate ?? flight.originGate,
            destinationTerminal: destinationTerminal ?? flight.destinationTerminal,
            destinationGate: destinationGate ?? flight.destinationGate
        )
    }
}
