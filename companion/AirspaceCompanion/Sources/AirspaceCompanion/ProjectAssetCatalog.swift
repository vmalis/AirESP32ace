// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation

enum ProjectAssetCatalog {
    static func launchAgencyLogo(for agency: LaunchAgency) -> NSImage? {
        if agency == .spacex {
            return NSImage(
                contentsOf: assetRoot
                    .appendingPathComponent("launch")
                    .appendingPathComponent("logos")
                    .appendingPathComponent("spacex-soft-white.png")
            )
        }
        let name = agency == .nasa ? "iss_nasa" : "iss_roscosmos"
        guard let url = Bundle.module.url(
            forResource: name,
            withExtension: "png"
        ) else {
            return nil
        }
        return NSImage(contentsOf: url)
    }

    static func cityImage(for rawKey: String) -> NSImage? {
        let key = rawKey.uppercased()
        let custom = customCityIconDirectory
            .appendingPathComponent("\(key).png")
        if let image = NSImage(contentsOf: custom) {
            return image
        }
        let fileNames: [String: String] = [
            "SJC": "SJ.png",
            "SFO": "SFO.png",
            "SAN": "SanDiego.png",
            "JFK": "NY.png",
            "MOW": "moscow.png",
            "LED": "StPetersburgh.png",
            "DUS": "Dusseldorf.png",
            "ICN": "Seoul.png",
        ]
        guard let fileName = fileNames[key] else { return nil }

        for directory in cityAssetDirectories {
            if let image = NSImage(
                contentsOf: directory.appendingPathComponent(fileName)
            ) {
                return image
            }
        }
        return nil
    }

    static func installCityImage(_ image: NSImage, for rawKey: String) throws {
        let key = rawKey.uppercased()
        try FileManager.default.createDirectory(
            at: customCityIconDirectory,
            withIntermediateDirectories: true
        )
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let png = bitmap.representation(using: .png, properties: [:]) else {
            throw CocoaError(.fileWriteUnknown)
        }
        try png.write(
            to: customCityIconDirectory.appendingPathComponent("\(key).png"),
            options: .atomic
        )
    }

    struct AircraftEntry: Codable, Identifiable, Hashable {
        let slug: String
        let label: String
        let codes: [String]

        var id: String { slug }
        var primaryCode: String { codes.first ?? "—" }
    }

    struct AirlineEntry: Identifiable, Hashable, Sendable {
        let code: String
        let name: String

        var id: String { code }
    }

    static let majorAirlines: [AirlineEntry] = [
        .init(code: "UA", name: "United Airlines"),
        .init(code: "AA", name: "American Airlines"),
        .init(code: "DL", name: "Delta Air Lines"),
        .init(code: "WN", name: "Southwest Airlines"),
        .init(code: "AS", name: "Alaska Airlines"),
        .init(code: "B6", name: "JetBlue"),
        .init(code: "NK", name: "Spirit Airlines"),
        .init(code: "F9", name: "Frontier Airlines"),
        .init(code: "HA", name: "Hawaiian Airlines"),
        .init(code: "AC", name: "Air Canada"),
        .init(code: "WS", name: "WestJet"),
        .init(code: "AM", name: "Aeromexico"),
        .init(code: "YP", name: "Air Premia"),
        .init(code: "OO", name: "SkyWest Airlines"),
        .init(code: "YX", name: "Republic Airways"),
        .init(code: "MQ", name: "Envoy Air"),
        .init(code: "9E", name: "Endeavor Air"),
        .init(code: "OH", name: "PSA Airlines"),
        .init(code: "C5", name: "CommuteAir"),
        .init(code: "G7", name: "GoJet Airlines"),
        .init(code: "UPS", name: "UPS Airlines"),
        .init(code: "FDX", name: "FedEx Express"),
        .init(code: "AMZ", name: "Amazon Air"),
        .init(code: "GTI", name: "Atlas Air"),
        .init(code: "CLX", name: "Cargolux"),
        .init(code: "DHL", name: "DHL Aviation"),
        .init(code: "5Y", name: "Atlas Air"),
        .init(code: "PO", name: "Polar Air Cargo"),
        .init(code: "K4", name: "Kalitta Air"),
        .init(code: "AV", name: "Avianca"),
        .init(code: "CM", name: "Copa Airlines"),
        .init(code: "LA", name: "LATAM Airlines"),
        .init(code: "G3", name: "GOL"),
        .init(code: "AD", name: "Azul"),
        .init(code: "AR", name: "Aerolineas Argentinas"),
        .init(code: "VB", name: "Viva Aerobus"),
        .init(code: "Y4", name: "Volaris"),
        .init(code: "JA", name: "JetSMART"),
        .init(code: "H2", name: "SKY Airline"),
        .init(code: "BA", name: "British Airways"),
        .init(code: "VS", name: "Virgin Atlantic"),
        .init(code: "LH", name: "Lufthansa"),
        .init(code: "LX", name: "SWISS"),
        .init(code: "OS", name: "Austrian Airlines"),
        .init(code: "AF", name: "Air France"),
        .init(code: "KL", name: "KLM"),
        .init(code: "IB", name: "Iberia"),
        .init(code: "UX", name: "Air Europa"),
        .init(code: "TP", name: "TAP Air Portugal"),
        .init(code: "AZ", name: "ITA Airways"),
        .init(code: "EI", name: "Aer Lingus"),
        .init(code: "FR", name: "Ryanair"),
        .init(code: "U2", name: "easyJet"),
        .init(code: "W6", name: "Wizz Air"),
        .init(code: "VY", name: "Vueling"),
        .init(code: "SK", name: "SAS"),
        .init(code: "DY", name: "Norwegian"),
        .init(code: "FI", name: "Icelandair"),
        .init(code: "AY", name: "Finnair"),
        .init(code: "LO", name: "LOT Polish Airlines"),
        .init(code: "A3", name: "Aegean Airlines"),
        .init(code: "TK", name: "Turkish Airlines"),
        .init(code: "PC", name: "Pegasus Airlines"),
        .init(code: "SU", name: "Aeroflot"),
        .init(code: "S7", name: "S7 Airlines"),
        .init(code: "EW", name: "Eurowings"),
        .init(code: "DE", name: "Condor"),
        .init(code: "BT", name: "airBaltic"),
        .init(code: "JU", name: "Air Serbia"),
        .init(code: "QS", name: "Smartwings"),
        .init(code: "RO", name: "TAROM"),
        .init(code: "EK", name: "Emirates"),
        .init(code: "QR", name: "Qatar Airways"),
        .init(code: "EY", name: "Etihad Airways"),
        .init(code: "SV", name: "Saudia"),
        .init(code: "FZ", name: "flydubai"),
        .init(code: "WY", name: "Oman Air"),
        .init(code: "GF", name: "Gulf Air"),
        .init(code: "LY", name: "EL AL"),
        .init(code: "MS", name: "EgyptAir"),
        .init(code: "ET", name: "Ethiopian Airlines"),
        .init(code: "KQ", name: "Kenya Airways"),
        .init(code: "AT", name: "Royal Air Maroc"),
        .init(code: "SA", name: "South African Airways"),
        .init(code: "WB", name: "RwandAir"),
        .init(code: "DT", name: "TAAG Angola Airlines"),
        .init(code: "SQ", name: "Singapore Airlines"),
        .init(code: "CX", name: "Cathay Pacific"),
        .init(code: "KE", name: "Korean Air"),
        .init(code: "OZ", name: "Asiana Airlines"),
        .init(code: "JL", name: "Japan Airlines"),
        .init(code: "NH", name: "ANA"),
        .init(code: "CA", name: "Air China"),
        .init(code: "MU", name: "China Eastern"),
        .init(code: "CZ", name: "China Southern"),
        .init(code: "HU", name: "Hainan Airlines"),
        .init(code: "BR", name: "EVA Air"),
        .init(code: "CI", name: "China Airlines"),
        .init(code: "PR", name: "Philippine Airlines"),
        .init(code: "TG", name: "Thai Airways"),
        .init(code: "MH", name: "Malaysia Airlines"),
        .init(code: "GA", name: "Garuda Indonesia"),
        .init(code: "VN", name: "Vietnam Airlines"),
        .init(code: "AI", name: "Air India"),
        .init(code: "6E", name: "IndiGo"),
        .init(code: "IX", name: "Air India Express"),
        .init(code: "QF", name: "Qantas"),
        .init(code: "NZ", name: "Air New Zealand"),
        .init(code: "JQ", name: "Jetstar"),
        .init(code: "VA", name: "Virgin Australia"),
        .init(code: "TR", name: "Scoot"),
        .init(code: "AK", name: "AirAsia"),
        .init(code: "D7", name: "AirAsia X"),
        .init(code: "ID", name: "Batik Air"),
        .init(code: "HY", name: "Uzbekistan Airways"),
        .init(code: "KC", name: "Air Astana"),
    ]

    static let aircraftEntries: [AircraftEntry] = {
        let url = assetRoot
            .appendingPathComponent("aircraft_icons")
            .appendingPathComponent("type_registry.json")
        guard
            let data = try? Data(contentsOf: url),
            let entries = try? JSONDecoder().decode(
                [AircraftEntry].self,
                from: data
            )
        else {
            return []
        }
        return entries.sorted {
            $0.label.localizedStandardCompare($1.label) ==
                .orderedAscending
        }
    }()

    static func aircraftImage(
        for entry: AircraftEntry
    ) -> NSImage? {
        NSImage(
            contentsOf: assetRoot
                .appendingPathComponent("aircraft_icons")
                .appendingPathComponent("individual_direct")
                .appendingPathComponent(entry.slug)
                .appendingPathComponent("side_detail.png")
        )
    }

    @MainActor
    static func airlineLogo(for rawCode: String) async -> NSImage? {
        let code = normalizedAirlineCode(rawCode)
        guard !code.isEmpty else { return nil }

        for directory in ["airline_logos", "cargo_logos"] {
            let bundled = assetRoot
                .appendingPathComponent(directory)
                .appendingPathComponent("\(code).png")
            if let image = NSImage(contentsOf: bundled) {
                return image
            }
        }

        let cached = airlineLogoCacheDirectory
            .appendingPathComponent("\(code).png")
        if let image = NSImage(contentsOf: cached) {
            return image
        }

        guard let url = URL(
            string: "https://airlabs.co/img/airline/s/\(code).png"
        ) else {
            return nil
        }
        do {
            let (data, response) = try await URLSession.shared.data(
                from: url
            )
            guard
                let http = response as? HTTPURLResponse,
                (200..<300).contains(http.statusCode),
                let image = NSImage(data: data)
            else {
                return nil
            }
            try FileManager.default.createDirectory(
                at: airlineLogoCacheDirectory,
                withIntermediateDirectories: true
            )
            try? data.write(to: cached, options: .atomic)
            return image
        } catch {
            return nil
        }
    }

    static func normalizedAirlineCode(_ value: String) -> String {
        String(
            value
                .uppercased()
                .filter { $0.isLetter || $0.isNumber }
                .prefix(3)
        )
    }

    static var assetRoot: URL {
        let manager = FileManager.default
        if let resources = Bundle.main.resourceURL {
            let bundled = resources.appendingPathComponent(
                "assets",
                isDirectory: true
            )
            if manager.fileExists(atPath: bundled.path) {
                return bundled
            }
        }

        #if DEBUG
            var directory = URL(fileURLWithPath: #filePath)
                .deletingLastPathComponent()
            for _ in 0..<7 {
                let candidate = directory.appendingPathComponent(
                    "assets",
                    isDirectory: true
                )
                if manager.fileExists(
                    atPath: candidate
                        .appendingPathComponent("aircraft_icons")
                        .path
                ) {
                    return candidate
                }
                directory.deleteLastPathComponent()
            }
        #endif
        return URL(
            fileURLWithPath:
                FileManager.default.currentDirectoryPath
        ).appendingPathComponent("assets", isDirectory: true)
    }

    private static var cityAssetDirectories: [URL] {
        var directories: [URL] = []
        if let resources = Bundle.main.resourceURL {
            directories.append(
                resources.appendingPathComponent(
                    "cities",
                    isDirectory: true
                )
            )
        }
        directories.append(
            assetRoot.deletingLastPathComponent()
                .appendingPathComponent("cities", isDirectory: true)
        )

        #if DEBUG
            var directory = URL(fileURLWithPath: #filePath)
                .deletingLastPathComponent()
            for _ in 0..<7 {
                let candidate = directory.appendingPathComponent(
                    "Cities",
                    isDirectory: true
                )
                if FileManager.default.fileExists(atPath: candidate.path) {
                    directories.append(candidate)
                    break
                }
                directory.deleteLastPathComponent()
            }
        #endif
        return directories
    }

    private static var airlineLogoCacheDirectory: URL {
        let base = FileManager.default.urls(
            for: .cachesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory
        return base
            .appendingPathComponent(
                "com.vadimmalis.airspace-companion",
                isDirectory: true
            )
            .appendingPathComponent(
                "AirlineLogos",
                isDirectory: true
            )
    }

    private static var customCityIconDirectory: URL {
        let base = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory
        return base
            .appendingPathComponent("AirspaceCompanion", isDirectory: true)
            .appendingPathComponent("CityIcons", isDirectory: true)
    }
}
