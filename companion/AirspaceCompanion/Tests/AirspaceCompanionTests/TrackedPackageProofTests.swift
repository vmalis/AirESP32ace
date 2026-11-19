import XCTest
import CoreLocation
@testable import AirspaceCompanion

final class TrackedPackageProofTests: XCTestCase {
    private let a = UUID(), b = UUID()
    private var flights: [TrackedPackageProof.Flight] { [.init(slot: 3, id: a, number: "XX123"), .init(slot: 1, id: b, number: "XX123")] }

    private func receipt(_ rows: Any?, activeID: UUID? = nil, includeSlot: Bool = true, generation: Bool = false) throws -> Data {
        var root: [String: Any] = ["deviceTrackingSupported":true,"preparedFlightCount":2,
                                   "planId":(activeID ?? a).uuidString,"flightNumber":"XX 123"]
        if includeSlot { root["deviceSlot"] = 3 }
        if let rows { root["preparedSlots"] = rows }
        if generation { root["assetGeneration"] = 0 }
        return try JSONSerialization.data(withJSONObject: root)
    }
    private var rows: [[String:Any]] { flights.map { ["deviceSlot":$0.slot,"planId":$0.id.uuidString,
        "flightNumber":$0.number,"routeMapMask":3,"routeFlagMask":3] } }

    func testFutureSlotMissingOneFlagFailsEvenWhenActiveFlightPasses() throws {
        let proof = try TrackedPackageProof(flights:flights, activeID:a)
        XCTAssertTrue(try proof.verify(receipt(Array(rows.reversed()))))
        var incomplete = rows; incomplete[1]["routeFlagMask"] = 1
        XCTAssertThrowsError(try proof.verify(receipt(incomplete)))
        XCTAssertThrowsError(try proof.verify(receipt([rows[0],rows[0]])))
        XCTAssertThrowsError(try proof.verify(receipt(rows,activeID:b))) // duplicate number, wrong departure
        var swapped = rows; swapped[1]["planId"] = a.uuidString
        XCTAssertThrowsError(try proof.verify(receipt(swapped)))
    }

    func testLegacyWithoutSlotReceiptsIsTransactionLevelOnlyAndNeverBypassesMalformedNewReceipt() throws {
        let proof = try TrackedPackageProof(flights:flights, activeID:a)
        XCTAssertFalse(try proof.verify(receipt(nil, includeSlot:false)))
        XCTAssertThrowsError(try proof.verify(receipt(nil,includeSlot:false),requireSlots:true))
        XCTAssertThrowsError(try proof.verify(receipt(nil,activeID:b,includeSlot:false)))
        XCTAssertThrowsError(try proof.verify(receipt(nil,generation:true)))
        XCTAssertThrowsError(try proof.verify(receipt(NSNull())))
        XCTAssertThrowsError(try proof.verify(receipt([["deviceSlot":1]])))
        XCTAssertThrowsError(try proof.verify(receipt(rows,includeSlot:false)))
    }

    func testNoSlotClampingAndEachEndpointIsRequired() throws {
        let proof = try TrackedPackageProof(flights:flights,activeID:a)
        XCTAssertThrowsError(try TrackedPackageProof(flights:[flights[0],.init(slot:3,id:b,number:"YY")],activeID:a))
        XCTAssertThrowsError(try TrackedPackageProof(flights:[.init(slot:4,id:a,number:"YY")],activeID:a))
        let names = flights.flatMap { f in
            ["route_globe","route_frame","flag_origin","flag_destination"].map { "\($0)_\(f.slot)" }
        }
        XCTAssertNoThrow(try proof.checkUSBAssetNames(names))
        XCTAssertThrowsError(try proof.checkUSBAssetNames(Array(names.dropLast()) + [names[0]]))
        let map = Data(repeating:1,count:466*466*2), flag = Data(repeating:1,count:3200)
        XCTAssertNoThrow(try TrackedPackageProof.checkImages(globe:map,frame:map,originFlag:flag,destinationFlag:flag))
        XCTAssertThrowsError(try TrackedPackageProof.checkImages(globe:map,frame:map,originFlag:flag,destinationFlag:Data(repeating:0,count:3200)))
        XCTAssertThrowsError(try TrackedPackageProof.checkImages(globe:map,frame:Data(),originFlag:flag,destinationFlag:flag))
    }

    func testEndpointCountryRecoveredWithoutProviderOrElseExplicitFailure() throws {
        var sf = Airport.sfo
        sf.countryCode = nil
        XCTAssertEqual(AirportCityCatalog.flagCountry(for:sf),"US")
        let bytes = try FlagRenderer.trackedEndpoint(sf)
        XCTAssertEqual(bytes.count,3200)
        XCTAssertTrue(bytes.contains(where: { $0 != 0 }))
        let mismatch = Airport(code: "SFO", name:"",city:"",coordinate:.init(latitude: 0,longitude: 0))
        XCTAssertNil(AirportCityCatalog.flagCountry(for:mismatch))
        XCTAssertThrowsError(try FlagRenderer.trackedEndpoint(mismatch))
    }
}
