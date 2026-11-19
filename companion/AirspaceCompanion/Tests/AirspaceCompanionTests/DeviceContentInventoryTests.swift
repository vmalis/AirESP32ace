// AirESP32ace
// Created by VM, November 2026
import XCTest
@testable import AirspaceCompanion

final class DeviceContentInventoryTests: XCTestCase {
    private let cities = RadarLocationPreset.all
    private func report(_ mask: (Int) -> Int) -> String {
        cities.enumerated().map { "\($1.key):\(mask($0))" }.joined(separator: ",")
    }

    func testEmptyBoardAndExplicitRebuildScheduleAllEightCityPacks() {
        let empty = DeviceContentInventory(radar: nil, icons: nil, space: nil)
        for inventory in [empty, nil] {
            let jobs = DeviceContentInventory.radarJobs(for: cities, skipping: inventory)
            XCTAssertEqual(jobs.count,24)
            for city in cities { XCTAssertEqual(jobs.filter { $0.0.key == city.key }.map { $0.1 }, [10,20,50]) }
        }
    }

    func testRepairSchedulesOnlyMissingRangesAndSpaceArt() {
        let state = DeviceContentInventory(radar: report { $0 == 0 ? 0 : $0 == 1 ? 5 : 7 },
                                           icons: 0xff, space: 0xfff7)
        let jobs = DeviceContentInventory.radarJobs(for: cities, skipping: state)
        XCTAssertEqual(jobs.map { $0.0.key }, [cities[0].key, cities[0].key, cities[0].key, cities[1].key])
        XCTAssertEqual(jobs.map { $0.1 }, [10,20,50,20])
        XCTAssertEqual((0..<16).filter { !state.containsSpaceAsset($0) }, [3])
    }

    func testMalformedMissingAndDuplicateEntriesNeverSkipUnprovenAssets() {
        for raw in [nil, "", "\(cities[0].key):", "\(cities[0].key):8", "\(cities[0].key):7,\(cities[0].key):0"] as [String?] {
            let state = DeviceContentInventory(radar: raw, icons: 0xff, space: -1)
            XCTAssertEqual(DeviceContentInventory.radarJobs(for: cities, skipping: state).count,24)
            XCTAssertFalse(state.containsIcon(cities[0].key))
            XCTAssertFalse(state.containsSpaceAsset(0))
        }
    }

    func testIconMaskUsesBoardOrderNotMacCitySlotAndRepairIsIdempotent() {
        let inventory = cities.reversed().map { "\($0.key):7" }.joined(separator: ",")
        let partial = DeviceContentInventory(radar: inventory, icons: 1, space: 0xffff)
        XCTAssertTrue(partial.containsIcon(cities.last!.key)); XCTAssertFalse(partial.containsIcon(cities[0].key))
        let icons = Set(cities.map(\.key))
        XCTAssertFalse(partial.isComplete(cities: cities, expectedIcons: icons, spaceAssetCount: 16))
        let ready = DeviceContentInventory(radar: inventory, icons: 0xff, space: 0xffff)
        XCTAssertTrue(DeviceContentInventory.radarJobs(for: cities, skipping: ready).isEmpty)
        XCTAssertTrue(ready.isComplete(cities: cities, expectedIcons: icons, spaceAssetCount: 16))
        XCTAssertFalse(DeviceContentInventory(radar: nil, icons: 0xff, space: 0xffff)
            .isComplete(cities: cities, expectedIcons: [], spaceAssetCount: 16))
    }

    func testRadarSwitchCannotMutateStoredFlightsEvenWithStaleMacState() throws {
        let payload = try DeviceClient().encodedState(style: .xdrRadar, routeProgress: 0.75,
            flight: .airPremiaYP111, trackingActive: true, deviceSlot: 3)
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: payload) as? [String: String])
        XCTAssertEqual(object, ["style":"xdrRadar"])
    }

    func testAbsentActiveWeatherAndISSFlagsAreFirstAndNeverRepeated() {
        typealias Art = DeviceContentInventory.Artwork
        let jobs = Art.order(active: "DUS", cities: ["SJC","JFK","DUS"])
        let initial = DeviceContentInventory(radar:"SJC:7,JFK:7,DUS:7", icons:0, space:0x7fff)
        XCTAssertEqual(jobs.filter { initial.present($0) == false },
                       [.emblem("DUS"), .issFlags, .emblem("SJC"), .emblem("JFK")])
        XCTAssertEqual(Art.spaceNames[15],"iss_flags")
        let after = DeviceContentInventory(radar:"SJC:7,JFK:7,DUS:7", icons:4, space:0xffff)
        XCTAssertEqual(jobs.filter { after.present($0) == false }, [.emblem("SJC"),.emblem("JFK")])
    }

    func testActiveCityNotConfiguredOnMacStillComesFirstAndUnknownArtIsNotMissing() {
        let jobs = DeviceContentInventory.Artwork.order(active:"DUS", cities:["SJC"])
        XCTAssertEqual(Array(jobs.prefix(2)),[.emblem("DUS"),.issFlags])
        let unknown = DeviceContentInventory(radar:"SJC:7", icons:0, space:nil)
        XCTAssertNil(unknown.present(.emblem("DUS")))
        XCTAssertNil(unknown.present(.issFlags))
        XCTAssertEqual(unknown.present(.emblem("SJC")),false)
    }
}
