// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

/// Fills missing provider fields and repairs old saved plans without API calls.
/// Kept in the Mac resource bundle, not the almost-full firmware partition.
enum AirportCityCatalog {
    struct Entry: Decodable {
        let city: String
        let country: String
        let latitude: Double
        let longitude: Double
    }
    private struct Catalog: Decodable { let airports: [String: Entry] }
    static let entries: [String: Entry] = {
        guard let url = AppResources.bundle.url(forResource: "AirportCities", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let catalog = try? JSONDecoder().decode(Catalog.self, from: data)
        else { return [:] }
        return catalog.airports
    }()

    static func city(for airport: Airport) -> String? {
        let code = airport.code.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard let entry = entries[code] else { return nil }
        if let country = airport.countryCode?.trimmingCharacters(in: .whitespacesAndNewlines),
           !country.isEmpty, country.uppercased() != entry.country { return nil }
        // A reused code must not relabel an airport on another continent.
        guard CLLocationCoordinate2DIsValid(airport.coordinate),
              CLLocation(latitude: airport.coordinate.latitude, longitude: airport.coordinate.longitude)
                .distance(from: CLLocation(latitude: entry.latitude, longitude: entry.longitude)) < 100_000
        else { return nil }
        return entry.city
    }
}
