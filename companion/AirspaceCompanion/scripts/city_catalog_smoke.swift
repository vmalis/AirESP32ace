// AirESP32ace
// Created by VM, August 2026
import Foundation

@main
struct CityCatalogSmoke {
    static func main() throws {
        precondition(RadarLocationPreset.all.count == 8)
        precondition(RadarLocationPreset.catalog.count == 10)
        precondition(Set(RadarLocationPreset.catalog.map(\.key)).count == 10)
        for (key, metar) in [("CLT", "KCLT"), ("CAE", "KCAE")] {
            let city = RadarLocationPreset.preset(for: key)!
            precondition(city.metar == metar)
            precondition(city.timezone == "EST5EDT,M3.2.0,M11.1.0")
            precondition(!RadarLocationPreset.all.contains { $0.key == key })
            let payload = try JSONEncoder().encode(city.definition)
            let decoded = try JSONDecoder().decode(SharedConfiguration.LocationDefinition.self, from: payload)
            precondition(decoded == city.definition)
        }
        print("City catalog smoke: passed (10 choices, 8 unchanged defaults, serialized definitions)")
    }
}
