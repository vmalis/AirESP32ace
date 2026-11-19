// AirESP32ace
// Created by VM, November 2026

import AppKit
import Foundation
import Darwin

enum FirmwareUpdateSafety {
    // Identify all release versions and all locations, not merely this app's path.
    static let companionBundleID = "com.vadimmalis.airspace-companion"

    static func hasOtherInstance(ownPID: pid_t, companionPIDs: [pid_t]) -> Bool {
        companionPIDs.contains { $0 > 0 && $0 != ownPID }
    }

    static func checkExclusiveCompanion() throws {
        let instances = NSRunningApplication.runningApplications(withBundleIdentifier: companionBundleID)
        if hasOtherInstance(ownPID: ProcessInfo.processInfo.processIdentifier,
                            companionPIDs: instances.filter { !$0.isTerminated }.map(\.processIdentifier)) {
            throw DeviceFirmwareError.anotherCompanion
        }
    }

    // Same advisory lock that the bundled esptool's pyserial exclusive=True
    // obtains. Check before changing line settings, toggling DTR or reading.
    // Legacy companions don't take this lock, hence the peer check above.
    static func claimSerialDescriptor(_ fd: Int32) throws {
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { throw DeviceFirmwareError.serialInUse }
    }

    // Tool output can contain Python tracebacks, paths, or provisioning CSV data.
    // Inspect it locally but only surface fixed diagnostic messages to the UI.
    static func failureMessage(output: String, status: Int32, flashing: Bool) -> String {
        guard flashing else {
            return "Could not prepare the device settings (helper exit \(status)). No firmware was written."
        }
        let text = output.lowercased()
        if ["resource busy", "busy port", "exclusive lock", "port is busy"].contains(where: text.contains) {
            return "The USB port is busy. Quit other copies of AirESP32ace and any serial monitor before retrying."
        }
        if ["stopped responding", "serial data stream stopped", "packet transfer stopped",
            "serialexception", "write timeout", "timed out", "invalid head of packet"].contains(where: text.contains) {
            return "Communication with the board was interrupted. The application image may be incomplete. Keep power connected, quit other companion copies and serial monitors, then retry the same update over a direct USB data cable. Do not erase the device to recover an interrupted update."
        }
        return "The firmware helper exited with code \(status). Boot has not been verified. Keep the board powered and retry the same update after checking USB."
    }
}
