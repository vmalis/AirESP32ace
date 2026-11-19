// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class USBSerialAccessTests: XCTestCase {
    func testFirmwareLeaseBlocksPollingUntilReleased() {
        let firmware = USBSerialAccess.acquire()
        let entered = DispatchSemaphore(value: 0)
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            let poll = USBSerialAccess.acquire()
            entered.signal()
            poll.release()
            finished.signal()
        }
        XCTAssertEqual(entered.wait(timeout: .now() + 0.1), .timedOut)
        firmware.release()
        firmware.release() // Idempotent; cannot allow two concurrent users.
        XCTAssertEqual(entered.wait(timeout: .now() + 2), .success)
        XCTAssertEqual(finished.wait(timeout: .now() + 2), .success)
    }

    func testThrowingTransactionReleasesUSBAccess() {
        enum Failure: Error { case expected }
        func fail() throws {
            let lease = USBSerialAccess.acquire()
            defer { lease.release() }
            throw Failure.expected
        }
        XCTAssertThrowsError(try fail())
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            let next = USBSerialAccess.acquire()
            next.release()
            finished.signal()
        }
        XCTAssertEqual(finished.wait(timeout: .now() + 2), .success)
    }
}
