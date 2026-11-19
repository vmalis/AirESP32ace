// AirESP32ace
// Created by VM, November 2026
import XCTest
@testable import AirspaceCompanion

final class USBBootVerificationTests: XCTestCase {
    private let board = "001122334455"
    private let digest = String(repeating: "ac", count: 32)

    func testSlowBootHoldsSinglePortAndPreservesPartialReply() throws {
        var opens = 0, closes = [Int32](), attempts = 0, time = 0.0
        let id = try USBConfigurationClient.verifyBootSession(expected: digest, expectedBoard: board,
            open: { opens += 1; return 11 }, close: { closes.append($0) },
            now: { time }, pause: { time += 3 }) { fd, buffer in
                XCTAssertEqual(fd,11)
                XCTAssertEqual(buffer.count,attempts)
                attempts += 1
                if attempts < 5 {
                    buffer.append(0x41)
                    throw USBConfigurationClient.ClientError.timedOut
                }
                return BoardIdentity(deviceId: self.board, elfSHA256: self.digest.uppercased(),
                                     firmwareVersion: "candidate", ip: "")
            }
        XCTAssertEqual(id.deviceId, board)
        XCTAssertEqual(opens, 1); XCTAssertEqual(closes,[11]); XCTAssertEqual(attempts, 5)
    }

    func testCanReacquireOriginalPortAfterRealDisconnect() throws {
        var opens = 0, closes = [Int32](), time = 0.0
        _ = try USBConfigurationClient.verifyBootSession(expected: digest, expectedBoard: board,
            open: {
                opens += 1
                if opens == 2 { throw USBConfigurationClient.ClientError.openFailed("test port") }
                return Int32(opens)
            }, close: { closes.append($0) }, now: { time }, pause: { time += 1 }) { fd, buffer in
                if fd == 1 { buffer.append(0x41); throw USBConfigurationClient.ClientError.readFailed }
                XCTAssertEqual(fd,3); XCTAssertTrue(buffer.isEmpty)
                return BoardIdentity(deviceId: self.board, elfSHA256: self.digest, firmwareVersion: "candidate", ip: "")
            }
        XCTAssertEqual(closes,[1,3])
    }

    func testQuietPortTimesOutWithoutReopeningOrReportingSuccess() {
        var opens = 0, closes = 0, time = 0.0
        XCTAssertThrowsError(try USBConfigurationClient.verifyBootSession(expected: digest, expectedBoard: board,
            open: { opens += 1; return 11 }, close: { _ in closes += 1 },
            now: { time }, pause: { time += 5 }) { _, _ in throw USBConfigurationClient.ClientError.timedOut }) {
                guard case PairingFailure.boot = $0 else { return XCTFail("Unexpected error: \($0)") }
            }
        XCTAssertEqual(opens,1); XCTAssertEqual(closes,1); XCTAssertEqual(time,45)
    }

    func testRejectsWrongBoardOrImageWithoutCyclingPort() {
        for wrongBoard in [true, false] {
            var closes = 0
            XCTAssertThrowsError(try USBConfigurationClient.verifyBootSession(expected: digest, expectedBoard: board,
                open: { 11 }, close: { _ in closes += 1 }, pause: { XCTFail("A mismatch must be terminal") }) { _, _ in
                    BoardIdentity(deviceId: wrongBoard ? "FFFFFFFFFFFF" : self.board,
                                  elfSHA256: wrongBoard ? self.digest : String(repeating: "00",count:32),
                                  firmwareVersion: "candidate", ip: "")
                }) {
                    switch $0 {
                    case PairingFailure.identity where wrongBoard, PairingFailure.boot where !wrongBoard: break
                    default: XCTFail("Unexpected mismatch error: \($0)")
                    }
                }
            XCTAssertEqual(closes,1)
        }
    }

    func testLockConflictNeverRetries() {
        XCTAssertThrowsError(try USBConfigurationClient.verifyBootSession(expected: digest, expectedBoard: board,
            open: { throw DeviceFirmwareError.serialInUse }, close: { _ in XCTFail("No descriptor opened") },
            pause: { XCTFail("Don't hammer a port held by another process") }) { _, _ in
                XCTFail("A busy port cannot be probed")
                throw PairingFailure.boot
            }) { guard case DeviceFirmwareError.serialInUse = $0 else { return XCTFail("Unexpected error") } }
    }
}
