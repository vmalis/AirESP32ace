import XCTest
@testable import AirspaceCompanion

@MainActor
final class ActiveRadarRepairTests: XCTestCase {
    private func status(_ mask: String = "DUS:0,SJC:7", miles: Int? = 50,
                        free: Int? = 2_000_000) throws -> DeviceClient.StatusSnapshot {
        var fields: [String: Any] = ["trackedFlightValid": true, "trackedFlightNumber": "TEST",
            "routeMapMask": 3, "locationKey": "DUS", "lat": "51.23", "lon": "6.78", "radarMapInventory": mask]
        fields["radarRadiusMiles"] = miles
        fields["storageFreeBytes"] = free
        return try JSONDecoder().decode(DeviceClient.StatusSnapshot.self, from: JSONSerialization.data(withJSONObject: fields))
    }

    func testActiveBoardCityAndExactRangeUploadBeforeBulkRegardlessOfMacCityOrder() async throws {
        let first = try status("SJC:7,DUS:3")
        let target = try ActiveRadarRepair(status: first)
        XCTAssertEqual(target.location.key,"DUS"); XCTAssertEqual(target.miles,50)
        XCTAssertEqual(target.location.coordinate.latitude,51.23)
        var steps = [String]()
        let verified = try await target.restore(initial: first, prepare: {
            steps.append("render"); return Data(repeating: 0, count: ActiveRadarRepair.mapBytes)
        }, read: {
            steps.append("check")
            return steps.contains("write") ? try self.status("SJC:7,DUS:7") : first
        }, upload: { _ in steps.append("write") })
        XCTAssertEqual(DeviceContentInventory(verified).radarPresent(key:"DUS",miles:50),true)
        steps.append("other artwork")
        XCTAssertEqual(steps,["render","check","write","check","other artwork"])
    }

    func testNoWorkForAlreadyPresentMapAndNoGuessWhenScopeMissing() async throws {
        let first = try status("DUS:4")
        _ = try await ActiveRadarRepair(status:first).restore(initial:first, prepare: {
            XCTFail("Rendered existing map"); return Data()
        }, read: { XCTFail("Redundant second check"); return first },
            upload: { _ in XCTFail("Replaced existing map") })
        XCTAssertThrowsError(try ActiveRadarRepair(status: status(miles:nil)))
        XCTAssertThrowsError(try ActiveRadarRepair(status: status(miles:25)))
    }

    func testMissingProofOrCapacityStopsBeforeRenderingOrAnyWrite() async throws {
        for first in [try status("SJC:7"), try status(free: nil), try status(free: 434312)] {
            do {
                _ = try await ActiveRadarRepair(status:first).restore(initial:first, prepare: {
                    XCTFail("Rendered before confirming inventory and reserve"); return Data()
                }, read: { XCTFail("Redundant check"); return first },
                    upload: { _ in XCTFail("Unsafe upload") })
                XCTFail("Should reject")
            } catch is ContentRepairError {}
        }
    }

    func testNoOverwriteIfAnotherRepairCompletedAndNoWriteAfterChangedRange() async throws {
        let first = try status()
        for before in [try status("DUS:4"), try status(miles:20), try status(free:0)] {
            do {
                let result = try await ActiveRadarRepair(status:first).restore(initial:first, prepare: {
                    Data(repeating:0,count:ActiveRadarRepair.mapBytes)
                }, read: { before }, upload: { _ in XCTFail("Should not write") })
                XCTAssertEqual(result.radarMapInventory,"DUS:4")
            } catch is ContentRepairError {
                XCTAssertNotEqual(before.radarMapInventory,"DUS:4")
            }
        }
    }

    func testFailedReadbackPreventsBulkUpload() async throws {
        let first = try status()
        var writes = 0
        do {
            _ = try await ActiveRadarRepair(status:first).restore(initial:first, prepare: {
                Data(repeating:0,count:ActiveRadarRepair.mapBytes)
            }, read: { first }, upload: { _ in writes += 1 })
            XCTFail("Bulk phase must not start after false success")
        } catch ContentRepairError.missingAfterUpload {}
        XCTAssertEqual(writes,1)
    }

    func testRemainingArtworkCannotConsumeReserveEvenWithCachedStatus() throws {
        var budget = ContentRepairBudget(free: ContentRepairBudget.reserve + 8192)
        try budget.claim(1)
        try budget.claim(4010)
        XCTAssertThrowsError(try budget.claim(1))
        XCTAssertEqual(budget.free,ContentRepairBudget.reserve)
    }
}
