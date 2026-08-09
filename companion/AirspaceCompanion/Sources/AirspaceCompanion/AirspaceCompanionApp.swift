// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation
import SwiftUI

@main
struct AirspaceCompanionApp: App {
    @StateObject private var model = CompanionModel()

    init() {
        if Self.isCommandLineRenderer {
            Self.renderMapFromCommandLineIfRequested()
        } else {
            NSApplication.shared.setActivationPolicy(.regular)
        }
    }

    var body: some Scene {
        Window("AirESP32ace", id: "main") {
            ContentView(model: model)
                .frame(minWidth: 980, minHeight: 680)
                .onAppear {
                    NSApplication.shared.activate(ignoringOtherApps: true)
                    NSApplication.shared.windows.first?.makeKeyAndOrderFront(nil)
                }
        }
        .windowResizability(.contentSize)
    }

    private static var isCommandLineRenderer: Bool {
        let arguments = CommandLine.arguments
        return arguments.contains("--render-local-radar-map") ||
            arguments.contains("--render-all-radar-maps") ||
            arguments.contains("--render-yp111-map") ||
            arguments.contains("--render-yp111-geodesic-map") ||
            arguments.contains("--sync-saved-flight") ||
            arguments.contains("--clear-tracked-assets")
    }

    private static func renderMapFromCommandLineIfRequested() {
        let arguments = CommandLine.arguments
        if arguments.contains("--clear-tracked-assets") {
            Task {
                do {
                    try await USBConfigurationClient()
                        .clearTrackedFlightAssets()
                    print("Tracked-flight assets cleared")
                    Foundation.exit(0)
                } catch {
                    FileHandle.standardError.write(
                        Data("Asset cleanup failed: \(error.localizedDescription)\n".utf8)
                    )
                    Foundation.exit(1)
                }
            }
            RunLoop.current.run()
            return
        }
        if arguments.contains("--sync-saved-flight") {
            let pushHost: String? = {
                guard let index = arguments.firstIndex(of: "--push-host"),
                      arguments.indices.contains(index + 1)
                else {
                    return nil
                }
                return arguments[index + 1]
            }()
            Task { @MainActor in
                let model = CompanionModel(resumeSavedMonitoring: false)
                if let pushHost {
                    model.deviceHost = pushHost
                }
                FileHandle.standardError.write(
                    Data("Synchronizing the saved flight schedule…\n".utf8)
                )
                await model.syncTrackedFlightsToDevice()
                print(model.pushState.message)
                if case .failed = model.pushState {
                    Foundation.exit(1)
                }
                Foundation.exit(0)
            }
            RunLoop.current.run()
            return
        }

        if arguments.contains("--render-local-radar-map") || arguments.contains("--render-all-radar-maps") {
            let renderAll = arguments.contains("--render-all-radar-maps")
            let pushHost: String? = {
                guard let index = arguments.firstIndex(of: "--push-host"),
                      arguments.indices.contains(index + 1)
                else {
                    return nil
                }
                return arguments[index + 1]
            }()
            Task { @MainActor in
                do {
                    let renderer = LocalRadarMapRenderer()
                    let locations = renderAll
                        ? RadarLocationPreset.all
                        : Array(RadarLocationPreset.all.prefix(1))
                    let client = DeviceClient()
                    var completed = 0
                    let total = locations.count * LocalRadarMapRenderer.supportedRadiusMiles.count
                    for location in locations {
                        for radiusMiles in LocalRadarMapRenderer.supportedRadiusMiles {
                            let image = try await renderer.renderMap(
                                center: location.coordinate,
                                radiusMiles: radiusMiles,
                                size: CGSize(width: 466, height: 466)
                            )
                            let asset = try ImageWriter.writeDeviceImage(
                                image,
                                name: "\(location.key)_radar_map_\(Int(radiusMiles))mi_466"
                            )
                            completed += 1
                            print("[\(completed)/\(total)] \(location.key) \(Int(radiusMiles)) mi")
                            print(asset.pngURL.path)
                            print(asset.rgb565URL.path)
                            if let pushHost {
                                let rgb565 = try Data(contentsOf: asset.rgb565URL)
                                try await client.pushRadarMap(
                                    host: pushHost,
                                    rgb565: rgb565,
                                    rangeMiles: radiusMiles,
                                    locationKey: location.key
                                )
                                print("pushed to \(pushHost)")
                                try await Task.sleep(nanoseconds: 350_000_000)
                            }
                        }
                    }
                    Foundation.exit(0)
                } catch {
                    FileHandle.standardError.write(Data("Local radar map render failed: \(error.localizedDescription)\n".utf8))
                    Foundation.exit(1)
                }
            }

            RunLoop.current.run()
            return
        }

        let renderGeodesic = arguments.contains(
            "--render-yp111-geodesic-map"
        )
        guard arguments.contains("--render-yp111-map") || renderGeodesic else {
            return
        }

        let useLive = arguments.contains("--live")
        let pushHost: String? = {
            guard let index = arguments.firstIndex(
                of: "--push-host"
            ),
            arguments.indices.contains(index + 1)
            else {
                return nil
            }
            return arguments[index + 1]
        }()
        let progress: Double
        if let index = arguments.firstIndex(of: "--progress"), arguments.indices.contains(index + 1),
           let parsed = Double(arguments[index + 1]) {
            progress = parsed
        } else {
            progress = 0.61
        }

        Task { @MainActor in
            do {
                let flight: TrackedFlight
                let routeProgress: Double
                if useLive {
                    let key = ProcessInfo.processInfo.environment["FLIGHTAWARE_AEROAPI_KEY"]
                        ?? ProcessInfo.processInfo.environment["AEROAPI_KEY"]
                        ?? ""
                    let trimmedKey = key.trimmingCharacters(in: .whitespacesAndNewlines)
                    guard !trimmedKey.isEmpty else {
                        throw FlightProviderError.noLiveFlight("YP111: set FLIGHTAWARE_AEROAPI_KEY")
                    }
                    flight = try await FlightAwareFlightStatusProvider(apiKey: trimmedKey).trackedFlight(for: "YP111")
                    routeProgress = flight.routeProgress ?? progress
                } else {
                    flight = .airPremiaYP111
                    routeProgress = progress
                }

                let renderer: FlightMapRendering = renderGeodesic
                    ? MapKitFlightMapRenderer()
                    : RouteFrameMapRenderer()
                let image = try await renderer.renderMap(
                    for: flight,
                    progress: routeProgress,
                    size: CGSize(width: 466, height: 466)
                )
                let asset = try ImageWriter.writeDeviceImage(
                    image,
                    name: renderGeodesic
                        ? "\(flight.marketingNumber)_geodesic_map_466"
                        : "\(flight.marketingNumber)_route_map_466"
                )
                if useLive {
                    print(flight.liveStatusSummary)
                }
                print(asset.pngURL.path)
                print(asset.rgb565URL.path)
                if let pushHost {
                    let client = DeviceClient()
                    try await client.pushMap(
                        host: pushHost,
                        rgb565: Data(
                            contentsOf: asset.rgb565URL
                        ),
                        view: renderGeodesic
                            ? .globe
                            : .frame
                    )
                    try await client.pushRouteGeometry(
                        host: pushHost,
                        flight: flight
                    )
                    print("pushed to \(pushHost)")
                }
                Foundation.exit(0)
            } catch {
                FileHandle.standardError.write(Data("Map render failed: \(error.localizedDescription)\n".utf8))
                Foundation.exit(1)
            }
        }

        RunLoop.current.run()
    }
}
