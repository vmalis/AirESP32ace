// AirESP32ace
import Foundation

/// A flight number is not a unique identity: two prescribed departures can
/// have the same number. Keep the plan UUID, slot and number bound together.
struct TrackedPackageProof: Sendable {
    struct Flight: Equatable, Sendable {
        let slot: Int
        let id: UUID
        let number: String
    }
    struct SlotReceipt: Decodable {
        let deviceSlot: Int
        let planId: UUID
        let flightNumber: String
        let routeMapMask: Int
        let routeFlagMask: Int
    }
    private struct Receipt: Decodable {
        let deviceTrackingSupported: Bool
        let preparedFlightCount: Int
        let planId: UUID
        let flightNumber: String
        let deviceSlot: Int?
        let preparedSlots: [SlotReceipt]?
        enum CodingKeys: String, CodingKey {
            case deviceTrackingSupported, preparedFlightCount, planId, flightNumber, deviceSlot, preparedSlots, queueRevision, assetGeneration
        }
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            deviceTrackingSupported = try c.decode(Bool.self, forKey: .deviceTrackingSupported)
            preparedFlightCount = try c.decode(Int.self, forKey: .preparedFlightCount)
            planId = try c.decode(UUID.self, forKey: .planId)
            flightNumber = try c.decode(String.self, forKey: .flightNumber)
            deviceSlot = c.contains(.deviceSlot) ? try c.decode(Int.self, forKey: .deviceSlot) : nil
            if c.contains(.queueRevision) { _ = try c.decode(UInt32.self, forKey: .queueRevision) }
            if c.contains(.assetGeneration) { _ = try c.decode(UInt32.self, forKey: .assetGeneration) }
            // Earlier firmware legitimately omits the entire per-slot protocol.
            // New or malformed responses must never silently downgrade to it.
            preparedSlots = c.contains(.preparedSlots) || c.contains(.queueRevision) || c.contains(.assetGeneration)
                ? try c.decode([SlotReceipt].self, forKey: .preparedSlots) : nil
        }
    }
    let active: Flight
    let flights: [Flight]

    static func number(_ value: String) -> String { value.uppercased().filter { $0.isLetter || $0.isNumber } }
    static func fail(_ message: String) -> USBConfigurationClient.ClientError { .stateVerificationFailed(message) }

    init(flights: [Flight], activeID: UUID?) throws {
        guard (1...4).contains(flights.count), Set(flights.map(\.slot)).count == flights.count,
              Set(flights.map(\.id)).count == flights.count,
              flights.allSatisfy({ (0..<4).contains($0.slot) && !Self.number($0.number).isEmpty }),
              let active = flights.first(where: { $0.id == activeID }) else {
            throw Self.fail("The prepared queue does not uniquely identify each prescribed flight and its slot.")
        }
        self.active = active
        self.flights = flights
    }

    static func checkImages(globe: Data, frame: Data, originFlag: Data, destinationFlag: Data) throws {
        guard [globe,frame].allSatisfy({ $0.count == 466*466*2 && $0.contains(where: { $0 != 0 }) }),
              [originFlag,destinationFlag].allSatisfy({ $0.count == 40*40*2 && $0.contains(where: { $0 != 0 }) }) else {
            throw Self.fail("Each flight needs two complete maps and two nonblank endpoint flags before sync.")
        }
    }

    func checkUSBAssetNames(_ names: [String]) throws {
        let expected = Set(flights.flatMap { f in
            ["route_globe", "route_frame", "flag_origin", "flag_destination"].map { "\($0)_\(f.slot)" }
        })
        guard names.count == expected.count, Set(names) == expected else {
            throw Self.fail("The package is missing or repeats a flight slot's map or endpoint flag.")
        }
    }

    @discardableResult
    func verify(_ bytes: Data, requireSlots: Bool = false) throws -> Bool {
        let receipt: Receipt
        do { receipt = try JSONDecoder().decode(Receipt.self, from: bytes) }
        catch { throw Self.fail("The device's queue verification response is incomplete or malformed.") }
        guard receipt.deviceTrackingSupported, receipt.preparedFlightCount == flights.count,
              receipt.planId == active.id, Self.number(receipt.flightNumber) == Self.number(active.number),
              receipt.deviceSlot == nil || receipt.deviceSlot == active.slot else {
            throw Self.fail("The device did not confirm the requested queue count and next flight identity.")
        }
        guard let slots = receipt.preparedSlots else {
            if requireSlots { throw Self.fail("The device advertised per-slot verification but omitted the receipt.") }
            return false // legacy transaction-level proof only
        }
        guard receipt.deviceSlot == active.slot, slots.count == flights.count, flights.allSatisfy({ f in
            slots.contains { $0.deviceSlot == f.slot && $0.planId == f.id &&
                Self.number($0.flightNumber) == Self.number(f.number) && $0.routeMapMask == 3 && $0.routeFlagMask == 3 }
        }) else { throw Self.fail("A prescribed slot has the wrong flight, or is missing a map or endpoint flag.") }
        return true
    }
}
