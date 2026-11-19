// AirESP32ace
// Created by VM, November 2026
import Foundation

/// A snapshot from the paired board, not the Mac's image cache. Firmware
/// checks FAT file lengths for map bits 0/1/2 (10/20/50 miles). City icon
/// bits use device preset order, NOT the user's arranged eight-city list.
struct DeviceContentInventory {
    private var mapMasks: [String: Int] = [:]
    private var iconBits: [String: Int] = [:]
    private let iconMask: UInt32?
    private let spaceMask: UInt32?

    init(radar: String?, icons: Int?, space: Int?) {
        iconMask = icons.flatMap { UInt32(exactly: $0) }
        spaceMask = space.flatMap { UInt32(exactly: $0) }
        var seen = Set<String>()
        var orderIsKnown = true
        for (i, record) in (radar ?? "").split(separator: ",", omittingEmptySubsequences: false).enumerated() {
            let fields = record.split(separator: ":", omittingEmptySubsequences: false)
            let key = fields.first.map { String($0).uppercased() } ?? ""
            guard !key.isEmpty, key.utf8.allSatisfy({ (65...90).contains($0) || (48...57).contains($0) }) else {
                orderIsKnown = false
                continue
            }
            guard seen.insert(key).inserted else {
                mapMasks.removeValue(forKey: key)
                orderIsKnown = false
                continue
            }
            guard fields.count == 2, fields[1].count == 1, let mask = Int(fields[1]), (0...7).contains(mask) else {
                orderIsKnown = false
                continue
            }
            mapMasks[key] = mask
            if i < 32 { iconBits[key] = i }
        }
        if !orderIsKnown { iconBits.removeAll() }
    }

    init(_ status: DeviceClient.StatusSnapshot) {
        self.init(radar: status.radarMapInventory, icons: status.weatherCityIconMask, space: status.spaceAssetFileMask)
    }

    func containsIcon(_ key: String) -> Bool {
        present(.emblem(key)) == true
    }

    func containsSpaceAsset(_ bit: Int) -> Bool {
        present(.space(bit)) == true
    }

    enum Artwork: Equatable {
        case emblem(String), space(Int)
        // Device mask order is a wire format, independent of upload priority.
        static let spaceNames = ["sun", "earth", "moon", "iss", "world", "nasa", "roscosm",
            "launch_sls", "launch_falcon9", "launch_soyuz", "launch_starship",
            "launch_spacex", "launch_florida", "launch_starbase", "launch_baikonur", "iss_flags"]
        static let issFlags = Artwork.space(15)

        static func order(active: String, cities: [String]) -> [Self] {
            [.emblem(active), issFlags] + spaceNames.indices.map(Self.space).filter { $0 != issFlags }
                + cities.filter { $0.caseInsensitiveCompare(active) != .orderedSame }.map(Self.emblem)
        }
    }

    func present(_ asset: Artwork) -> Bool? {
        switch asset {
        case let .emblem(key):
            guard let bit = iconBits[key.uppercased()], let iconMask else { return nil }
            return iconMask & (1 << bit) != 0
        case let .space(bit):
            guard (0..<32).contains(bit), let spaceMask else { return nil }
            return spaceMask & (1 << bit) != 0
        }
    }

    func radarPresent(key: String, miles: Int) -> Bool? {
        guard let mask = mapMasks[key.uppercased()],
              let bit = LocalRadarMapRenderer.supportedRadiusMiles.firstIndex(of: Double(miles)) else { return nil }
        return mask & (1 << bit) != 0
    }

    /// nil means an explicit rebuild: never skip a range or city.
    static func radarJobs(for cities: [RadarLocationPreset], skipping inventory: Self?) -> [(RadarLocationPreset, Double)] {
        cities.flatMap { city in
            LocalRadarMapRenderer.supportedRadiusMiles.enumerated().compactMap { bit, miles in
                let mask = inventory?.mapMasks[city.key.uppercased()] ?? 0
                return mask & (1 << bit) == 0 ? (city, miles) : nil
            }
        }
    }

    func isComplete(cities: [RadarLocationPreset], expectedIcons: Set<String>, spaceAssetCount: Int) -> Bool {
        !cities.isEmpty && Self.radarJobs(for: cities, skipping: self).isEmpty &&
            expectedIcons.allSatisfy(containsIcon) && (0..<spaceAssetCount).allSatisfy(containsSpaceAsset)
    }
}
