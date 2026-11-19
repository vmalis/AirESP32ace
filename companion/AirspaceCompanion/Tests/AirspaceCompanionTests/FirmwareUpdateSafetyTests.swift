// AirESP32ace
// Created by VM, November 2026
import XCTest
import Darwin
@testable import AirspaceCompanion

final class FirmwareUpdateSafetyTests: XCTestCase {
    func testRejectsPortAlreadyClaimedByEsptool() throws {
        // Independent open file descriptions exercise the same Darwin flock
        // protocol without toggling lines on any serial device.
        let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let writer = open(file.path, O_CREAT | O_EXCL | O_RDWR, 0o600)
        XCTAssertGreaterThanOrEqual(writer, 0)
        guard writer >= 0 else { return }
        defer { close(writer); try? FileManager.default.removeItem(at: file) }
        let reader = open(file.path, O_RDWR)
        XCTAssertGreaterThanOrEqual(reader, 0)
        guard reader >= 0 else { return }
        defer { close(reader) }
        XCTAssertEqual(flock(writer, LOCK_EX | LOCK_NB), 0)
        XCTAssertThrowsError(try FirmwareUpdateSafety.claimSerialDescriptor(reader))
        XCTAssertEqual(flock(writer, LOCK_UN), 0)
        XCTAssertNoThrow(try FirmwareUpdateSafety.claimSerialDescriptor(reader))
    }
    func testStopsForOtherVersionsEvenWithSameNameOrDifferentPath() {
        XCTAssertFalse(FirmwareUpdateSafety.hasOtherInstance(ownPID: 50, companionPIDs: [50, 0, -1]))
        XCTAssertTrue(FirmwareUpdateSafety.hasOtherInstance(ownPID: 50, companionPIDs: [50, 12]))
        XCTAssertTrue(FirmwareUpdateSafety.hasOtherInstance(ownPID: 50, companionPIDs: [12]))
    }

    func testInterruptedWriteDoesNotEchoLogsOrPromiseSuccessfulBoot() {
        let detail = FirmwareUpdateSafety.failureMessage(output:
            "sensitive-test-value\nTraceback p = self.read()\nA fatal error occurred: The chip stopped responding.",
            status: 2, flashing: true)
        XCTAssertTrue(detail.contains("may be incomplete"))
        XCTAssertTrue(detail.contains("Do not erase"))
        XCTAssertFalse(detail.contains("sensitive-test-value"))
        XCTAssertFalse(detail.contains("Traceback"))
    }

    func testBusyPortAndPreparationFailuresRemainDistinct() {
        XCTAssertTrue(FirmwareUpdateSafety.failureMessage(output: "Resource busy", status: 2,
                                                         flashing: true).contains("USB port is busy"))
        XCTAssertTrue(FirmwareUpdateSafety.failureMessage(output: "bad CSV containing private data",
            status: 10, flashing: false).contains("No firmware was written"))
    }
}
