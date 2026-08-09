// AirESP32ace
// Created by VM, August 2026

import Foundation

protocol FlightStatusProviding: Sendable {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight
}

/// The stable identity of one operating leg. A flight number and calendar day
/// are not sufficient: an airline may reuse the number for several segments.
struct FlightLegSelection: Codable, Hashable, Sendable {
    let providerIdentifier: String?
    let originCode: String
    let destinationCode: String
    let scheduledDeparture: Date
}

struct FlightLegCandidate: Identifiable, Hashable, Sendable {
    let id: String
    let marketingNumber: String
    let originCode: String
    let destinationCode: String
    let scheduledDeparture: Date
    let departureTimeZoneIdentifier: String?
    let statusSummary: String
    let isComplete: Bool
    let providerIdentifier: String?

    var selection: FlightLegSelection {
        FlightLegSelection(
            providerIdentifier: providerIdentifier,
            originCode: originCode,
            destinationCode: destinationCode,
            scheduledDeparture: scheduledDeparture
        )
    }
}

/// Providers which can enumerate and then resolve an exact dated leg.
protocol FlightLegProviding: FlightStatusProviding {
    func flightLegs(
        for marketingNumber: String,
        on date: Date
    ) async throws -> [FlightLegCandidate]

    func trackedFlight(
        for marketingNumber: String,
        on date: Date?,
        matching selection: FlightLegSelection?
    ) async throws -> TrackedFlight
}

extension FlightLegProviding {
    func trackedFlight(
        for marketingNumber: String,
        on date: Date?
    ) async throws -> TrackedFlight {
        try await trackedFlight(
            for: marketingNumber,
            on: date,
            matching: nil
        )
    }
}

struct PendingFlightLegChoice: Identifiable, Sendable {
    let id = UUID()
    let marketingNumber: String
    let departureDate: Date
    let candidates: [FlightLegCandidate]
}

enum FlightNumberIdentity {
    private static let operationalToMarketing: [String: String] = [
        "AAL": "AA",
        "APZ": "YP",
        "ASA": "AS",
        "DAL": "DL",
        "FFT": "F9",
        "JBU": "B6",
        "NKS": "NK",
        "SWA": "WN",
        "UAL": "UA",
    ]

    static func canonical(_ value: String) -> String {
        let compact = value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: " ", with: "")
            .uppercased()
        let prefix = String(compact.prefix { $0.isLetter })
        let suffix = String(compact.dropFirst(prefix.count))
        guard !prefix.isEmpty, !suffix.isEmpty else { return compact }
        let canonicalPrefix = operationalToMarketing[prefix] ?? prefix
        let canonicalSuffix = Int(suffix).map(String.init) ?? suffix
        return canonicalPrefix + canonicalSuffix
    }
}

extension FlightStatusProviding {
    func trackedFlight(for marketingNumber: String) async throws -> TrackedFlight {
        try await trackedFlight(for: marketingNumber, on: nil)
    }
}

struct StaticFlightStatusProvider: FlightStatusProviding {
    func trackedFlight(for marketingNumber: String, on date: Date?) async throws -> TrackedFlight {
        let normalized = marketingNumber
            .replacingOccurrences(of: " ", with: "")
            .uppercased()

        switch normalized {
        case "YP111", "YP0111", "APZ111":
            return .airPremiaYP111
        default:
            throw FlightProviderError.unsupportedFlight(marketingNumber)
        }
    }
}

enum FlightProviderError: LocalizedError {
    case unsupportedFlight(String)
    case noLiveFlight(String)

    var errorDescription: String? {
        switch self {
        case .unsupportedFlight(let number):
            "No route preset for \(number)."
        case .noLiveFlight(let number):
            "No current or scheduled live flight found for \(number)."
        }
    }
}
