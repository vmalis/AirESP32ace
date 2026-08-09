// AirESP32ace
// Created by VM, August 2026

import XCTest
@testable import AirspaceCompanion

final class DeviceSerialDiscoveryTests: XCTestCase {
    func testLiveDiscoveryOnlyReturnsSupportedCalloutPorts() {
        for candidate in DeviceSerialDiscovery.serialCandidates() {
            XCTAssertTrue(candidate.path.hasPrefix("/dev/cu."))
        }
    }

    func testPrefersNativeEspressifSerialJTAGOverGenericSerialPort() {
        let generic = DeviceSerialPortCandidate(
            path: "/dev/cu.usbmodem0001",
            vendorID: 0x1234,
            productID: 0x0001,
            vendorName: "Other",
            productName: "Development board"
        )
        let tracker = DeviceSerialPortCandidate(
            path: "/dev/cu.usbmodem114301",
            vendorID: 0x303A,
            productID: 0x1001,
            vendorName: "Espressif",
            productName: "USB JTAG/serial debug unit"
        )

        XCTAssertEqual(
            DeviceSerialDiscovery.preferredCandidate(from: [generic, tracker]),
            tracker
        )
    }

    func testFallsBackToSupportedUSBSerialBridge() {
        let bridge = DeviceSerialPortCandidate(
            path: "/dev/cu.wchusbserial1410",
            vendorID: nil,
            productID: nil,
            vendorName: nil,
            productName: nil
        )

        XCTAssertEqual(
            DeviceSerialDiscovery.preferredCandidate(from: [bridge]),
            bridge
        )
    }
}
