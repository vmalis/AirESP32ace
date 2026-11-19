// AirESP32ace
// Created by VM, August 2026

import Darwin
import Foundation

struct USBConfigurationClient: Sendable {
    struct AssetPayload: Sendable {
        let name: String
        let data: Data
    }

    struct TransferProgress: Sendable {
        let assetName: String
        let assetIndex: Int
        let assetCount: Int
        let sentBytes: Int
        let totalBytes: Int

        var fraction: Double {
            guard totalBytes > 0 else { return 1 }
            return min(1, max(0, Double(sentBytes) / Double(totalBytes)))
        }
    }

    enum ClientError: LocalizedError {
        case deviceNotConnected
        case openFailed(String)
        case configureFailed
        case readFailed
        case writeFailed
        case timedOut
        case deviceRejected(String)
        case invalidResponse
        case stateVerificationFailed(String)

        var errorDescription: String? {
            switch self {
            case .deviceNotConnected:
                "No supported ESP32 USB serial device is connected."
            case let .openFailed(port):
                "Could not open the USB connection at \(port)."
            case .configureFailed:
                "Could not configure the USB serial connection."
            case .readFailed:
                "The USB connection was lost while reading the device response."
            case .writeFailed:
                "Could not send configuration data over USB-C."
            case .timedOut:
                "The connected device did not answer the configuration request. Update its firmware first if needed."
            case let .deviceRejected(message):
                "The device rejected the configuration: \(message)"
            case .invalidResponse:
                "The device returned an invalid configuration response."
            case let .stateVerificationFailed(message):
                "Could not verify the device package: \(message)"
            }
        }
    }

    private struct StateReceipt: Decodable {
        let trackedFlightValid: Bool?
        let trackedFlightNumber: String?
        let deviceSlot: Int?
        let routeMapMask: Int?
        let routeFlagMask: Int?
        let preparedFlightCount: Int?
    }

    private static let okPrefix = "ASTCFG:OK:"
    private static let errorPrefix = "ASTCFG:ERROR:"
    private static let pongPrefix = "ASTCFG:PONG:"
    private static let capabilitiesPrefix = "ASTCFG:CAPS:"
    private static let fileReadyPrefix = "ASTFILE:READY:"
    private static let fileCachedPrefix = "ASTFILE:CACHED:"
    private static let filePreparedPrefix = "ASTFILE:PREPARED:"
    private static let fileACKPrefix = "ASTFILE:ACK:"
    private static let fileDonePrefix = "ASTFILE:DONE:"
    private static let fileErrorPrefix = "ASTFILE:ERROR:"
    private static let stateOKPrefix = "ASTSTATE:OK:"
    private static let stateErrorPrefix = "ASTSTATE:ERROR:"
    // Route maps are already the exact RGB565 bytes consumed by the display.
    // Send them directly over native USB Serial/JTAG rather than expanding
    // them as Base64. One acknowledgement per 64 KiB is enough flow control
    // while keeping the progress UI useful.
    private static let preferBinaryTransfer = true
    private static let base64ChunkSize = 12 * 1_024
    // Keep each acknowledged burst comfortably below the instrument's USB RX
    // and filesystem staging capacity. This is protocol flow control, not a
    // baud-rate throttle: the next 32 KiB burst begins as soon as the device
    // confirms that the previous one was consumed.
    /* The ESP32 USB Serial/JTAG peripheral has a 4 KiB receive ring. Keep a
     * complete binary frame below half of it so the command and the next USB
     * hardware packet always have back-pressure headroom. Larger application
     * writes can overrun this otherwise flow-control-free ring even though a
     * firmware image upload (which uses esptool's separate protocol) is fast. */
    private static let binaryChunkSize = 2 * 1_024
    // macOS exposes only legacy constants up to 230400 in termios. The
    // ESP32-S3 USB Serial/JTAG bridge supports a much faster virtual serial
    // rate; IOSSIOSPEED avoids turning two small RGB565 maps into a
    // minute-long 115200-baud transfer.
    private static let usbBaudRate: speed_t = 921_600
    // `_IOW('T', 2, speed_t)` is an 8-byte ioctl on arm64 macOS. Using the
    // 32-bit request value makes IOSSIOSPEED fail and silently leaves the
    // connection at the much slower termios fallback.
    private static let ioSetSpeedRequest: UInt = 0x8008_5402

    func fetchConfig(port: String? = nil) async throws -> SharedConfiguration {
        let selectedPort = try resolvedPort(port)
        return try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            let data = try Self.transact(
                descriptor: descriptor,
                pending: &pending,
                command: "ASTCFG:GET\n",
                timeout: 8
            )
            return try JSONDecoder().decode(SharedConfiguration.self, from: data)
        }.value
    }

    func fetchTracker(port: String? = nil) async throws -> DeviceTrackerSnapshot {
        let selectedPort = try resolvedPort(port)
        return try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(descriptor: descriptor, pending: &pending)
            guard Self.capabilities(descriptor: descriptor, pending: &pending).contains("TRACKER1") else {
                throw ClientError.deviceRejected("Update firmware to enable autonomous flight tracking.")
            }
            let data = try Self.transact(descriptor: descriptor, pending: &pending,
                command: "ASTTRACK:GET\n", timeout: 8)
            return try JSONDecoder().decode(DeviceTrackerSnapshot.self, from: data)
        }.value
    }

    func fetchLaunches(port: String? = nil) async throws -> DeviceLaunchSnapshot {
        let selectedPort = try resolvedPort(port)
        return try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(descriptor: descriptor, pending: &pending)
            guard Self.capabilities(descriptor: descriptor, pending: &pending).contains("LAUNCH1") else {
                throw ClientError.deviceRejected("Update firmware to enable autonomous space launches.")
            }
            let data = try Self.transact(descriptor: descriptor, pending: &pending,
                command: "ASTLAUNCH:GET\n", timeout: 8)
            return try JSONDecoder().decode(DeviceLaunchSnapshot.self, from: data)
        }.value
    }

    func pushConfig(
        payload: DeviceClient.DeviceConfigPayload,
        port: String? = nil
    ) async throws -> SharedConfiguration {
        let selectedPort = try resolvedPort(port)
        let encoded = try JSONEncoder().encode(payload)
        guard let json = String(data: encoded, encoding: .utf8) else {
            throw ClientError.invalidResponse
        }
        return try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            let data = try Self.transact(
                descriptor: descriptor,
                pending: &pending,
                command: "ASTCFG:SET:\(json)\n",
                timeout: 10
            )
            return try JSONDecoder().decode(SharedConfiguration.self, from: data)
        }.value
    }

    func pushAsset(
        name: String,
        data: Data,
        port: String? = nil
    ) async throws {
        let selectedPort = try resolvedPort(port)
        try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            let capabilities = Self.capabilities(
                descriptor: descriptor,
                pending: &pending
            )
            let supportsBinary = Self.preferBinaryTransfer &&
                capabilities.contains("BIN1")
            try Self.transferFile(
                descriptor: descriptor,
                pending: &pending,
                name: name,
                data: data,
                useBinary: supportsBinary
            )
        }.value
    }

    /// Removes only the pre-rendered tracked-flight assets. This is used by
    /// the command-line transfer benchmark; normal synchronization keeps and
    /// hash-checks matching assets so repeat syncs remain nearly instant.
    func clearTrackedFlightAssets(port: String? = nil) async throws {
        let selectedPort = try resolvedPort(port)
        try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            try Self.prepareTrackedFlightStorage(
                descriptor: descriptor,
                pending: &pending,
                retainedSlotMask: 0
            )
        }.value
    }

    func pushState(
        data: Data,
        port: String? = nil,
        expectedFlightNumber: String? = nil,
        expectedClearedFlight: Bool = false
    ) async throws {
        let selectedPort = try resolvedPort(port)
        guard let json = String(data: data, encoding: .utf8) else {
            throw ClientError.invalidResponse
        }
        try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            try Self.sendState(
                descriptor: descriptor,
                pending: &pending,
                json: json,
                expectedFlightNumber: expectedFlightNumber,
                expectedClearedFlight: expectedClearedFlight,
                expectedRouteMapMask: nil,
                expectedRouteFlagMask: nil
            )
        }.value
    }

    /// Installs a complete set of assets and its state without closing the
    /// native USB Serial/JTAG connection between files. Closing this port can
    /// reset/re-enumerate an ESP32-S3, so a tracked-flight pack must be one
    /// acknowledged transaction from first map through final state receipt.
    func pushPackage(
        assets: [AssetPayload],
        state: Data,
        port: String? = nil,
        launchPackage: Bool = false,
        expectedFlightNumber: String? = nil,
        trackedProof: TrackedPackageProof? = nil,
        expectedClearedFlight: Bool = false,
        progress: (@Sendable (TransferProgress) -> Void)? = nil,
        finalizing: (@Sendable () -> Void)? = nil
    ) async throws {
        try trackedProof?.checkUSBAssetNames(assets.map(\.name))
        let selectedPort = try resolvedPort(port)
        guard let json = String(data: state, encoding: .utf8) else {
            throw ClientError.invalidResponse
        }
        try await Task.detached(priority: .userInitiated) {
            let access = USBSerialAccess.acquire()
            defer { access.release() }
            let descriptor = try Self.openConfiguredPort(selectedPort)
            defer { Darwin.close(descriptor) }
            var pending = Data()
            try Self.waitUntilResponsive(
                descriptor: descriptor,
                pending: &pending
            )
            let capabilities = Self.capabilities(
                descriptor: descriptor,
                pending: &pending
            )
            let supportsBinary = Self.preferBinaryTransfer &&
                capabilities.contains("BIN1")
            if launchPackage && !capabilities.contains("LAUNCH1") {
                throw ClientError.deviceRejected("Update firmware to enable autonomous space launches.")
            }
            let retainedSlotMask = Self.packageRetainedSlotMask(assets: assets, launchPackage: launchPackage)
            try Self.prepareTrackedFlightStorage(
                descriptor: descriptor,
                pending: &pending,
                retainedSlotMask: retainedSlotMask
            )
            let totalBytes = assets.reduce(0) { $0 + $1.data.count }
            var completedBytes = 0
            var lastReportedPercent = -1
            for (index, asset) in assets.enumerated() {
                fputs(
                    "USB tracked-flight asset \(index + 1)/\(assets.count): \(asset.name)\n",
                    stderr
                )
                try Self.transferFile(
                    descriptor: descriptor,
                    pending: &pending,
                    name: asset.name,
                    data: asset.data,
                    useBinary: supportsBinary,
                    progress: { assetBytes in
                        let sentBytes = completedBytes + assetBytes
                        let percent = totalBytes > 0
                            ? min(100, max(0, sentBytes * 100 / totalBytes))
                            : 100
                        // A route map is sent in many small acknowledged USB
                        // frames. Publishing every frame can queue hundreds of
                        // redundant SwiftUI updates and let an old callback
                        // arrive after completion. One update per percentage
                        // remains visually smooth without flooding MainActor.
                        guard percent != lastReportedPercent else { return }
                        lastReportedPercent = percent
                        progress?(
                            TransferProgress(
                                assetName: asset.name,
                                assetIndex: index + 1,
                                assetCount: assets.count,
                                sentBytes: sentBytes,
                                totalBytes: totalBytes
                            )
                        )
                    }
                )
                completedBytes += asset.data.count
            }
            // Asset bytes are complete, but the instrument still has to
            // parse, persist, select, and verify the tracked-flight state.
            // Surface that as a distinct phase instead of leaving the UI at
            // the misleading "asset 4/4" label while awaiting its receipt.
            finalizing?()
            try Self.sendState(
                descriptor: descriptor,
                pending: &pending,
                json: json,
                expectedFlightNumber: expectedFlightNumber,
                expectedClearedFlight: expectedClearedFlight,
                expectedRouteMapMask: expectedFlightNumber == nil ? nil : 3,
                expectedRouteFlagMask: expectedFlightNumber == nil ? nil : 3,
                expectedDeviceSlot: trackedProof?.active.slot
            )
            if let trackedProof {
                // Never reopen this port between queue activation and proof.
                try trackedProof.verify(Self.transact(descriptor: descriptor, pending: &pending,
                    command: "ASTTRACK:GET\n", timeout: 8), requireSlots: capabilities.contains("SLOTS1"))
            }
        }.value
    }

    private static func normalizedFlightNumber(_ value: String) -> String {
        value.uppercased().filter { $0.isLetter || $0.isNumber }
    }

    static func packageRetainedSlotMask(assets: [AssetPayload], launchPackage: Bool) -> Int {
        if launchPackage { return 15 }
        return assets.reduce(0) { mask, asset in
            guard let slot = assetSlot(asset.name) else { return mask }
            return mask | (1 << slot)
        }
    }

    private func resolvedPort(_ port: String?) throws -> String {
        if let port, !port.isEmpty { return port }
        guard let selected = DeviceSerialDiscovery.preferredPort() else {
            throw ClientError.deviceNotConnected
        }
        return selected
    }

    private static func transact(
        descriptor: Int32,
        pending: inout Data,
        command: String,
        timeout: TimeInterval
    ) throws -> Data {
        try writeAll(descriptor: descriptor, text: command)
        let line = try readProtocolLine(
            descriptor: descriptor,
            pending: &pending,
            acceptedPrefixes: [okPrefix],
            errorPrefixes: [errorPrefix],
            timeout: timeout
        )
        guard let body = line.dropFirst(okPrefix.count).data(using: .utf8) else {
            throw ClientError.invalidResponse
        }
        return body
    }

    private static func transferFile(
        descriptor: Int32,
        pending: inout Data,
        name: String,
        data: Data,
        useBinary: Bool,
        progress: ((Int) -> Void)? = nil
    ) throws {
        let hash = fnv1a(data)
        let beginCommand = "ASTFILE:BEGIN:\(name):\(data.count):" +
            "\(String(format: "%08x", hash))\n"
        try writeAll(
            descriptor: descriptor,
            text: beginCommand
        )
        let ready = try readProtocolLine(
            descriptor: descriptor,
            pending: &pending,
            acceptedPrefixes: [fileReadyPrefix, fileCachedPrefix],
            errorPrefixes: [fileErrorPrefix],
            timeout: 8
        )
        if ready == fileCachedPrefix + name {
            progress?(data.count)
            return
        }
        guard ready == fileReadyPrefix + name else {
            throw ClientError.invalidResponse
        }

        var offset = 0
        while offset < data.count {
            let remaining = data.count - offset
            // Native binary is ideal for complete transport blocks. Send the
            // final short tail through the existing checksummed Base64 command
            // instead: some macOS USB-serial stacks can hold a trailing short
            // binary write while the application is already waiting for its
            // ACK. This keeps large maps fast and makes small flags/logos and
            // every file tail deterministic.
            let sendBinaryBlock = useBinary && remaining >= binaryChunkSize
            let count = min(
                sendBinaryBlock ? binaryChunkSize : base64ChunkSize,
                remaining
            )
            let chunk = data.subdata(in: offset ..< offset + count)
            if sendBinaryBlock {
                try writeAll(
                    descriptor: descriptor,
                    text: "ASTFILE:DATA:\(offset):\(count)\n"
                )
                try writeAll(descriptor: descriptor, data: chunk)
            } else {
                let encoded = chunk.base64EncodedString()
                try writeAll(
                    descriptor: descriptor,
                    text: "ASTFILE:CHUNK:\(offset):\(encoded)\n"
                )
            }
            let acknowledgement = try readProtocolLine(
                descriptor: descriptor,
                pending: &pending,
                acceptedPrefixes: [fileACKPrefix],
                errorPrefixes: [fileErrorPrefix],
                timeout: 15
            )
            let acknowledgedOffset = Int(
                acknowledgement.dropFirst(fileACKPrefix.count)
            )
            guard acknowledgedOffset == offset + count else {
                throw ClientError.invalidResponse
            }
            offset += count
            progress?(offset)
        }

        try writeAll(descriptor: descriptor, text: "ASTFILE:END\n")
        let done = try readProtocolLine(
            descriptor: descriptor,
            pending: &pending,
            acceptedPrefixes: [fileDonePrefix],
            errorPrefixes: [fileErrorPrefix],
            timeout: 20
        )
        guard done == fileDonePrefix + name else {
            throw ClientError.invalidResponse
        }
        progress?(data.count)
    }

    private static func prepareTrackedFlightStorage(
        descriptor: Int32,
        pending: inout Data,
        retainedSlotMask: Int
    ) throws {
        try writeAll(
            descriptor: descriptor,
            text: "ASTFILE:PREPARE:\(String(retainedSlotMask, radix: 16))\n"
        )
        let prepared = try readProtocolLine(
            descriptor: descriptor,
            pending: &pending,
            acceptedPrefixes: [filePreparedPrefix],
            errorPrefixes: [fileErrorPrefix],
            timeout: 8
        )
        guard prepared == filePreparedPrefix + "READY" else {
            throw ClientError.invalidResponse
        }
    }

    private static func assetSlot(_ name: String) -> Int? {
        guard let suffix = name.split(separator: "_").last,
              let slot = Int(suffix),
              (0 ... 3).contains(slot)
        else {
            return nil
        }
        return slot
    }

    private static func sendState(
        descriptor: Int32,
        pending: inout Data,
        json: String,
        expectedFlightNumber: String?,
        expectedClearedFlight: Bool,
        expectedRouteMapMask: Int?,
        expectedRouteFlagMask: Int?,
        expectedDeviceSlot: Int? = nil
    ) throws {
        try writeAll(
            descriptor: descriptor,
            text: "ASTSTATE:SET:\(json)\n"
        )
        let line = try readProtocolLine(
            descriptor: descriptor,
            pending: &pending,
            acceptedPrefixes: [stateOKPrefix],
            errorPrefixes: [stateErrorPrefix],
            timeout: 15
        )
        guard let responseData = line
            .dropFirst(stateOKPrefix.count)
            .data(using: .utf8)
        else {
            throw ClientError.invalidResponse
        }
        let receipt = try JSONDecoder().decode(
            StateReceipt.self,
            from: responseData
        )
        if let root = try JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: Any],
           let queue = root["trackerQueue"] as? [Any],
           receipt.preparedFlightCount != queue.count {
            throw ClientError.stateVerificationFailed("The device did not retain the complete flight queue.")
        }
        if expectedClearedFlight {
            guard receipt.trackedFlightValid == false else {
                throw ClientError.stateVerificationFailed(
                    "the previous flight is still active"
                )
            }
        }
        if let expectedDeviceSlot, receipt.deviceSlot != expectedDeviceSlot {
            throw ClientError.stateVerificationFailed("The active device slot does not match the prescribed flight.")
        }
        if let expectedFlightNumber {
            let expected = normalizedFlightNumber(expectedFlightNumber)
            let received = normalizedFlightNumber(
                receipt.trackedFlightNumber ?? ""
            )
            guard receipt.trackedFlightValid == true,
                  !expected.isEmpty,
                  received == expected
            else {
                let actual = received.isEmpty ? "no flight" : received
                throw ClientError.stateVerificationFailed(
                    "expected \(expected), received \(actual)"
                )
            }
        }
        if let expectedRouteMapMask,
           receipt.routeMapMask != expectedRouteMapMask {
            throw ClientError.stateVerificationFailed(
                "flight retained, but its maps were not installed " +
                    "(mask \(receipt.routeMapMask ?? -1))"
            )
        }
        if let expectedRouteFlagMask,
           receipt.routeFlagMask != expectedRouteFlagMask {
            throw ClientError.stateVerificationFailed(
                "flight retained, but its endpoint flags were not installed " +
                    "(mask \(receipt.routeFlagMask ?? -1))"
            )
        }
    }

    private static func waitUntilResponsive(
        descriptor: Int32,
        pending: inout Data,
        timeout: TimeInterval = 40,
        verifyPair: Bool = true
    ) throws {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            try writeAll(
                descriptor: descriptor,
                text: "ASTCFG:PING\n"
            )
            do {
                let line = try readProtocolLine(
                    descriptor: descriptor,
                    pending: &pending,
                    acceptedPrefixes: [pongPrefix],
                    errorPrefixes: [],
                    timeout: min(1.25, max(0.1, deadline.timeIntervalSinceNow))
                )
                if line == pongPrefix + "READY" {
                    if verifyPair {
                        guard let paired = DevicePairing.stored() else { throw PairingFailure.unpaired }
                        let id = try readIdentity(descriptor, pending: &pending)
                        guard id.deviceId == paired.deviceId else { throw PairingFailure.identity }
                    }
                    return
                }
            } catch ClientError.timedOut {
                continue
            }
        }
        throw ClientError.timedOut
    }

    private static func readIdentity(_ fd: Int32, pending: inout Data) throws -> BoardIdentity {
        let bytes = try transact(descriptor: fd, pending: &pending, command: "ASTCFG:IDENTITY\n", timeout: 4)
        return try JSONDecoder().decode(BoardIdentity.self, from: bytes)
    }

    static func identity(port: String, pairing: Bool = false, expectedBoard: String? = nil) async throws -> BoardIdentity {
        try await Task.detached {
            let lease = USBSerialAccess.acquire()
            defer { lease.release() }
            let fd = try openConfiguredPort(port)
            defer { Darwin.close(fd) }
            var pending = Data()
            try waitUntilResponsive(descriptor: fd, pending: &pending, timeout: 4, verifyPair: false)
            let id = try readIdentity(fd, pending: &pending)
            if let expectedBoard, expectedBoard != id.deviceId { throw PairingFailure.identity }
            if pairing {
                let (pair, json) = try DevicePairing.make(for: id.deviceId)
                _ = try transact(descriptor: fd, pending: &pending, command: "ASTCFG:PAIR:\(json)\n", timeout: 8)
                try pair.save()
            } else {
                guard let pair = DevicePairing.stored() else { throw PairingFailure.unpaired }
                guard pair.deviceId == id.deviceId else { throw PairingFailure.identity }
            }
            return id
        }.value
    }

    /// Caller holds the exclusive serial lease across flashing and verification.
    static func verifyBoot(port: String, expected: String, expectedBoard: String?) throws -> BoardIdentity {
        try verifyBootSession(expected: expected, expectedBoard: expectedBoard,
                              open: { try openConfiguredPort(port) },
                              close: { _ = Darwin.close($0) }) { fd, pending in
            try waitUntilResponsive(descriptor: fd, pending: &pending, timeout: 2, verifyPair: false)
            return try readIdentity(fd, pending: &pending)
        }
    }

    // Opening or closing a native USB serial port can change its control lines.
    // Keep the same descriptor AND its partial response while the application
    // starts; release it only on return or actual loss of this connection.
    static func verifyBootSession(expected: String, expectedBoard: String?,
                                  open: () throws -> Int32, close: (Int32) -> Void,
                                  now: () -> TimeInterval = { ProcessInfo.processInfo.systemUptime },
                                  pause: () -> Void = { Thread.sleep(forTimeInterval: 0.35) },
                                  probe: (Int32, inout Data) throws -> BoardIdentity) throws -> BoardIdentity {
        let deadline = now() + 45
        var descriptor: Int32?
        var pending = Data()
        defer { if let descriptor { close(descriptor) } }
        while now() < deadline {
            if descriptor == nil {
                do { descriptor = try open() }
                catch ClientError.openFailed { pause(); continue } // waiting for re-enumeration
                catch ClientError.configureFailed { pause(); continue }
            }
            guard let fd = descriptor else { throw PairingFailure.boot }
            do {
                let id = try probe(fd, &pending)
                guard expectedBoard == nil || expectedBoard == id.deviceId else { throw PairingFailure.identity }
                guard id.elfSHA256.lowercased() == expected else { throw PairingFailure.boot }
                return id
            } catch ClientError.timedOut {
                // A quiet board isn't a disconnected board. Don't re-open it.
            } catch is DecodingError {
                // A partial/malformed identity cannot prove a boot. Try again.
            } catch let failure as ClientError {
                switch failure {
                case .readFailed, .writeFailed:
                    close(fd)
                    descriptor = nil
                    pending.removeAll()
                default: throw failure
                }
            }
            pause()
        }
        throw PairingFailure.boot
    }

    static func imageELFDigest(_ bytes: Data) throws -> String {
        // ESP image header (24), segment header (8), esp_app_desc_t hash offset (144).
        guard bytes.count >= 208, bytes[0] == 0xe9, Array(bytes[32..<36]) == [0x32,0x54,0xcd,0xab] else {
            throw PairingFailure.boot
        }
        return bytes[176..<208].map { String(format: "%02x",$0) }.joined()
    }

    private static func capabilities(
        descriptor: Int32,
        pending: inout Data
    ) -> Set<String> {
        do {
            try writeAll(descriptor: descriptor, text: "ASTCFG:CAPS\n")
            let line = try readProtocolLine(
                descriptor: descriptor,
                pending: &pending,
                acceptedPrefixes: [capabilitiesPrefix],
                errorPrefixes: [],
                timeout: 0.6
            )
            return Set(line
                .dropFirst(capabilitiesPrefix.count)
                .split(separator: ",")
                .map(String.init))
        } catch {
            // Firmware released before BIN1 simply ignores the capability
            // query. Continue with its compatible Base64 transport.
            return []
        }
    }

    private static func openConfiguredPort(_ port: String) throws -> Int32 {
        try FirmwareUpdateSafety.checkExclusiveCompanion()
        let descriptor = Darwin.open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard descriptor >= 0 else {
            throw ClientError.openFailed(port)
        }
        do { try FirmwareUpdateSafety.claimSerialDescriptor(descriptor) }
        catch { Darwin.close(descriptor); throw error }
        var options = termios()
        guard tcgetattr(descriptor, &options) == 0 else {
            Darwin.close(descriptor)
            throw ClientError.configureFailed
        }
        cfmakeraw(&options)
        options.c_cflag |= tcflag_t(CLOCAL | CREAD)
        options.c_cflag &= ~tcflag_t(HUPCL)
        guard cfsetspeed(&options, speed_t(B230400)) == 0,
              tcsetattr(descriptor, TCSANOW, &options) == 0 else {
            Darwin.close(descriptor)
            throw ClientError.configureFailed
        }
        var usbBaudRate = Self.usbBaudRate
        if ioctl(descriptor, Self.ioSetSpeedRequest, &usbBaudRate) != 0 {
            // Some third-party USB serial adapters reject the custom rate.
            // Keep the portable 230400 fallback instead of making setup fail.
            var fallbackOptions = options
            guard cfsetspeed(&fallbackOptions, speed_t(B230400)) == 0,
                  tcsetattr(descriptor, TCSANOW, &fallbackOptions) == 0 else {
                Darwin.close(descriptor)
                throw ClientError.configureFailed
            }
        }
        tcflush(descriptor, TCIFLUSH)
        return descriptor
    }

    private static func writeAll(descriptor: Int32, text: String) throws {
        guard let data = text.data(using: .utf8) else {
            throw ClientError.writeFailed
        }
        try writeAll(descriptor: descriptor, data: data)
    }

    private static func writeAll(
        descriptor: Int32,
        data: Data,
        timeout: TimeInterval = 20
    ) throws {
        var sent = 0
        let deadline = Date().addingTimeInterval(timeout)
        while sent < data.count {
            guard Date() < deadline else {
                throw ClientError.timedOut
            }
            let result = data.withUnsafeBytes { buffer in
                Darwin.write(
                    descriptor,
                    buffer.baseAddress!.advanced(by: sent),
                    data.count - sent
                )
            }
            if result > 0 {
                sent += result
            } else if result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                Thread.sleep(forTimeInterval: 0.002)
            } else {
                throw ClientError.writeFailed
            }
        }
        // Each file chunk is followed by a device acknowledgement. Waiting
        // for that ACK is the protocol's flow control; an additional tcdrain
        // here only serializes the native USB/JTAG transport unnecessarily.
    }

    private static func readProtocolLine(
        descriptor: Int32,
        pending: inout Data,
        acceptedPrefixes: [String],
        errorPrefixes: [String],
        timeout: TimeInterval
    ) throws -> String {
        let deadline = Date().addingTimeInterval(timeout)
        var buffer = [UInt8](repeating: 0, count: 4_096)
        let bufferCapacity = buffer.count
        while Date() < deadline {
            let count = buffer.withUnsafeMutableBytes { bytes in
                Darwin.read(descriptor, bytes.baseAddress, bufferCapacity)
            }
            if count > 0 {
                pending.append(buffer, count: count)
                while let newline = pending.firstIndex(of: 0x0A) {
                    let lineData = pending.prefix(upTo: newline)
                    pending.removeSubrange(...newline)
                    guard var line = String(data: lineData, encoding: .utf8) else {
                        continue
                    }
                    line = line.trimmingCharacters(in: .whitespacesAndNewlines)
                    if ProcessInfo.processInfo.environment["AST_USB_TRACE"] == "1",
                       !line.isEmpty {
                        fputs("[USB] \(line)\n", stderr)
                    }
                    if acceptedPrefixes.contains(where: line.hasPrefix) {
                        return line
                    }
                    if let prefix = errorPrefixes.first(where: line.hasPrefix) {
                        throw ClientError.deviceRejected(
                            String(line.dropFirst(prefix.count))
                        )
                    }
                }
            } else if count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR {
                throw ClientError.readFailed
            } else {
                Thread.sleep(forTimeInterval: 0.002)
            }
        }
        throw ClientError.timedOut
    }

    private static func fnv1a(_ data: Data) -> UInt32 {
        var hash: UInt32 = 2_166_136_261
        data.withUnsafeBytes { bytes in
            for byte in bytes {
                hash ^= UInt32(byte)
                hash = hash &* 16_777_619
            }
        }
        return hash
    }
}
