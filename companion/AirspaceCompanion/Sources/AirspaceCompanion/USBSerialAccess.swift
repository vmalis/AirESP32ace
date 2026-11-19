// AirESP32ace
// Created by VM, August 2026

import Foundation

/// The config protocol and esptool must never read/write the port concurrently.
/// Acquire on a worker, never the main actor; leases may cross executor threads.
enum USBSerialAccess {
    private static let semaphore = DispatchSemaphore(value: 1)

    final class Lease: @unchecked Sendable {
        private let lock = NSLock()
        private var released = false

        func release() {
            lock.lock()
            defer { lock.unlock() }
            guard !released else { return }
            released = true
            USBSerialAccess.semaphore.signal()
        }

        deinit { release() }
    }

    static func acquire() -> Lease {
        semaphore.wait()
        return Lease()
    }
}
