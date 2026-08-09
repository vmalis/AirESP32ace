// AirESP32ace
// Created by VM, August 2026

import Foundation

/// Personal-app credentials stored in the user's Application Support
/// directory with owner-only file permissions. Ad-hoc rebuilds therefore
/// retain access without certificate or authorization prompts.
struct LocalCredentialStore {
    let service: String

    func string(for account: String) -> String? {
        values()[account]
    }

    func set(_ value: String, for account: String) {
        var stored = values()
        stored[account] = value
        write(stored)
    }

    func remove(for account: String) {
        var stored = values()
        stored.removeValue(forKey: account)
        write(stored)
    }

    private var directoryURL: URL {
        let base = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.homeDirectoryForCurrentUser
        return base.appendingPathComponent(
            service,
            isDirectory: true
        )
    }

    private var fileURL: URL {
        directoryURL.appendingPathComponent(
            "credentials.json",
            isDirectory: false
        )
    }

    private func values() -> [String: String] {
        guard
            let data = try? Data(contentsOf: fileURL),
            let stored = try? JSONDecoder().decode(
                [String: String].self,
                from: data
            )
        else {
            return [:]
        }
        return stored
    }

    private func write(_ values: [String: String]) {
        do {
            try FileManager.default.createDirectory(
                at: directoryURL,
                withIntermediateDirectories: true,
                attributes: [
                    .posixPermissions: 0o700
                ]
            )
            let data = try JSONEncoder().encode(values)
            try data.write(to: fileURL, options: .atomic)
            try FileManager.default.setAttributes(
                [.posixPermissions: 0o600],
                ofItemAtPath: fileURL.path
            )
        } catch {
            NSLog(
                "Could not store companion credentials: %@",
                error.localizedDescription
            )
        }
    }
}
