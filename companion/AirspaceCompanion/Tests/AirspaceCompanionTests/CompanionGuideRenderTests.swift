// AirESP32ace
// Created by VM, August 2026

import AppKit
import SwiftUI
import XCTest
@testable import AirspaceCompanion

@MainActor
final class CompanionGuideRenderTests: XCTestCase {
    func testRenderCompanionGuideScreens() throws {
        let environment = ProcessInfo.processInfo.environment
        let outputDirectory = URL(
            fileURLWithPath: environment["AIRSPACE_GUIDE_OUTPUT_DIR"] ??
                "/private/tmp/airspace-companion-guide",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )

        let model = CompanionModel(resumeSavedMonitoring: false)
        model.isGuidePresentation = true
        model.configureGuideTrackingPresentation()
        let workspacePages = [
            "radar",
            "tracker",
            "weather",
            "clock",
            "space",
            "launches",
            "dock",
            "settings",
        ]
        for page in workspacePages {
            try render(
                ContentView(model: model, guidePage: page)
                    .frame(width: 1_360, height: 820),
                size: CGSize(width: 1_360, height: 820),
                output: outputDirectory
                    .appendingPathComponent("workspace-\(page).png")
            )
        }

        let legDate = Calendar.current.startOfDay(for: Date())
        let legChoice = PendingFlightLegChoice(
            marketingNumber: "WN982",
            departureDate: legDate,
            candidates: [
                FlightLegCandidate(
                    id: "wn982-las-san",
                    marketingNumber: "WN982",
                    originCode: "LAS",
                    destinationCode: "SAN",
                    scheduledDeparture: legDate.addingTimeInterval(10 * 3600),
                    departureTimeZoneIdentifier: "America/Los_Angeles",
                    statusSummary: "scheduled",
                    isComplete: false,
                    providerIdentifier: "fa:wn982-las-san"
                ),
                FlightLegCandidate(
                    id: "wn982-san-smf",
                    marketingNumber: "WN982",
                    originCode: "SAN",
                    destinationCode: "SMF",
                    scheduledDeparture: legDate.addingTimeInterval(13 * 3600),
                    departureTimeZoneIdentifier: "America/Los_Angeles",
                    statusSummary: "scheduled",
                    isComplete: false,
                    providerIdentifier: "fa:wn982-san-smf"
                ),
                FlightLegCandidate(
                    id: "wn982-smf-pdx",
                    marketingNumber: "WN982",
                    originCode: "SMF",
                    destinationCode: "PDX",
                    scheduledDeparture: legDate.addingTimeInterval(16 * 3600),
                    departureTimeZoneIdentifier: "America/Los_Angeles",
                    statusSummary: "scheduled",
                    isComplete: false,
                    providerIdentifier: "fa:wn982-smf-pdx"
                ),
            ]
        )
        try render(
            ZStack {
                Color.black
                FlightLegChooser(
                    choice: legChoice,
                    select: { _ in },
                    cancel: {}
                )
            }
            .frame(width: 650, height: 590),
            size: CGSize(width: 650, height: 590),
            output: outputDirectory
                .appendingPathComponent("workspace-tracker-leg-choice.png")
        )

        try render(
            ContentView(
                model: model,
                guidePage: "radar",
                guidePreviewPageIndex: 1
            )
            .frame(width: 1_360, height: 820),
            size: CGSize(width: 1_360, height: 820),
            output: outputDirectory
                .appendingPathComponent("workspace-radar-settings.png")
        )

        try render(
            CompanionUtilityGuideView(
                model: model,
                utilityName: "apiKeys"
            )
            .frame(width: 820, height: 660),
            size: CGSize(width: 820, height: 660),
            output: outputDirectory
                .appendingPathComponent("utility-apiKeys.png")
        )

        for library in ["aircraft", "airlines"] {
            try render(
                CompanionUtilityGuideView(
                    model: model,
                    utilityName: "libraries",
                    selectedLibrary: library
                )
                .frame(width: 820, height: 660),
                size: CGSize(width: 820, height: 660),
                output: outputDirectory
                    .appendingPathComponent("utility-gallery-\(library).png")
            )
        }

        let deviceSteps: [DeviceManagementGuideStep] = [
            .chooser,
            .firmware,
            .firmwareConnected,
            .setupWarning,
            .setupConnect,
            .setupForm,
            .firmwareWorking,
            .firmwareSuccess,
        ]
        for step in deviceSteps {
            try render(
                DeviceManagementView(
                    model: model,
                    guideStep: step,
                    dismiss: {}
                )
                .frame(width: 650, height: 610),
                size: CGSize(width: 650, height: 610),
                output: outputDirectory
                    .appendingPathComponent("device-\(step.rawValue).png")
            )
        }
    }

    private func render<Content: View>(
        _ content: Content,
        size: CGSize,
        output: URL
    ) throws {
        let hostingView = NSHostingView(rootView: content)
        hostingView.frame = CGRect(origin: .zero, size: size)
        hostingView.appearance = NSAppearance(named: .darkAqua)
        hostingView.layoutSubtreeIfNeeded()

        RunLoop.current.run(until: Date().addingTimeInterval(0.12))
        hostingView.layoutSubtreeIfNeeded()

        guard let representation = hostingView.bitmapImageRepForCachingDisplay(
            in: hostingView.bounds
        ) else {
            throw GuideRenderError.couldNotAllocate(output.lastPathComponent)
        }
        hostingView.cacheDisplay(in: hostingView.bounds, to: representation)
        guard let png = representation.representation(
            using: .png,
            properties: [:]
        ) else {
            throw GuideRenderError.couldNotEncode(output.lastPathComponent)
        }
        try png.write(to: output, options: .atomic)
    }
}

private enum GuideRenderError: LocalizedError {
    case couldNotAllocate(String)
    case couldNotEncode(String)

    var errorDescription: String? {
        switch self {
        case let .couldNotAllocate(name):
            "Could not allocate the companion guide screenshot \(name)."
        case let .couldNotEncode(name):
            "Could not encode the companion guide screenshot \(name)."
        }
    }
}
