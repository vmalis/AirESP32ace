// AirESP32ace
// Created by VM, November 2026
import Foundation
import CoreLocation

enum ContentRepairError: LocalizedError {
    case unknownScope, unknownInventory, changedScope, invalidMap, missingAfterUpload, unknownCapacity
    case insufficientSpace(free: Int, required: Int)
    case unknownArtworkInventory, artworkNotVerified

    var errorDescription: String? {
        switch self {
        case .unknownScope: "The device did not confirm its active Radar city, coordinates and range. No city was guessed."
        case .unknownInventory: "The device did not confirm whether its active Radar map is present. No map was replaced."
        case .changedScope: "The device's Radar selection changed during repair. Retry with the desired city and range selected."
        case .invalidMap: "The prepared Radar map is not a complete 466 × 466 RGB565 image."
        case .missingAfterUpload: "The device did not verify the active Radar map after upload. Remaining artwork was not started."
        case .unknownCapacity: "The device did not report usable storage capacity. No further content was written."
        case .unknownArtworkInventory: "The device did not confirm whether this artwork exists. No replacement was attempted."
        case .artworkNotVerified: "The device did not confirm this artwork after upload. Further installation was stopped."
        case let .insufficientSpace(free, required):
            "Device storage has \(free) bytes free; the next file needs \(required) bytes including the 128 KiB settings reserve. Nothing was deleted."
        }
    }
}

struct ContentRepairBudget {
    static let reserve = 128 * 1024 // APP_STORAGE_STATE_RESERVE
    var free: Int?

    mutating func claim(_ bytes: Int) throws {
        guard let free, free >= 0, bytes >= 0, bytes <= Int.max - 4095 - Self.reserve else {
            throw ContentRepairError.unknownCapacity
        }
        // Do not assume partial/invalid files will reclaim any allocation.
        let allocated = ((bytes + 4095) / 4096) * 4096
        guard free >= allocated + Self.reserve else {
            throw ContentRepairError.insufficientSpace(free: free, required: allocated + Self.reserve)
        }
        self.free = free - allocated
    }
}

struct ActiveRadarRepair {
    static let mapBytes = 466 * 466 * 2
    let location: RadarLocationPreset
    let miles: Int

    init(status: DeviceClient.StatusSnapshot) throws {
        guard let key = status.locationKey?.uppercased(), (2...7).contains(key.count),
              key.utf8.allSatisfy({ (65...90).contains($0) || (48...57).contains($0) }),
              let miles = status.radarRadiusMiles, LocalRadarMapRenderer.supportedRadiusMiles.contains(Double(miles)),
              let lat = status.lat.flatMap(Double.init), let lon = status.lon.flatMap(Double.init),
              (-90...90).contains(lat), (-180...180).contains(lon) else { throw ContentRepairError.unknownScope }
        let coordinates = CLLocationCoordinate2D(latitude: lat, longitude: lon)
        // Only reuse a named Mac cache when both its city and coordinates match the device.
        location = RadarLocationPreset.catalog.first {
            $0.key == key && abs($0.coordinate.latitude - lat) < 0.00001 &&
                abs($0.coordinate.longitude - lon) < 0.00001
        } ?? RadarLocationPreset(key: key, title: key, coordinate: coordinates, timezone: "", metar: "", radarHalfDegrees: "")
        self.miles = miles
    }

    private func present(in status: DeviceClient.StatusSnapshot) throws -> Bool {
        let reported = try ActiveRadarRepair(status: status)
        guard reported.location == location, reported.miles == miles else { throw ContentRepairError.changedScope }
        guard let present = DeviceContentInventory(status).radarPresent(key: location.key, miles: miles) else {
            throw ContentRepairError.unknownInventory
        }
        return present
    }

    /// Always complete this phase before starting Space/Weather/other ranges.
    /// No retries, navigation commands, preference posts or asset deletion.
    @MainActor
    func restore(initial: DeviceClient.StatusSnapshot,
                 prepare: () async throws -> Data,
                 read: () async throws -> DeviceClient.StatusSnapshot,
                 upload: (Data) async throws -> Void) async throws -> DeviceClient.StatusSnapshot {
        if try present(in: initial) { return initial }
        var budget = ContentRepairBudget(free: initial.storageFreeBytes)
        try budget.claim(Self.mapBytes)
        let bytes = try await prepare()
        guard bytes.count == Self.mapBytes else { throw ContentRepairError.invalidMap }
        try Task.checkCancellation()
        let before = try await read()
        if try present(in: before) { return before }
        budget = ContentRepairBudget(free: before.storageFreeBytes)
        try budget.claim(bytes.count)
        try await upload(bytes)
        let after = try await read()
        guard try present(in: after) else { throw ContentRepairError.missingAfterUpload }
        return after
    }
}
