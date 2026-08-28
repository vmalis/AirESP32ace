// AirESP32ace
// Created by VM, August 2026

import Foundation

enum AppResources {
    static nonisolated let bundle: Bundle = {
        let bundleName = "AirESP32ace_AirspaceCompanion.bundle"
        let candidates = [
            Bundle.main.resourceURL?.appendingPathComponent(bundleName),
            Bundle.main.bundleURL.appendingPathComponent(bundleName)
        ]

        for candidate in candidates.compactMap({ $0 }) {
            if let bundle = Bundle(url: candidate) {
                return bundle
            }
        }

        // SwiftPM provides this path when the executable is run directly from
        // its build directory rather than from a packaged macOS app.
        return Bundle.module
    }()
}
