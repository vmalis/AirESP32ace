// AirESP32ace
// Created by VM, August 2026

import AppKit
import SwiftUI
import XCTest
@testable import AirspaceCompanion

@MainActor
final class InstrumentPreviewRenderTests: XCTestCase {
    func testRouteFrameMapsCoverRegionalAndLongHaulRoutes() async throws {
        let sjc = Airport(
            code: "SJC",
            name: "San Jose International",
            city: "San Jose",
            coordinate: .init(latitude: 37.3639, longitude: -121.9289),
            countryCode: "US",
            timeZoneIdentifier: "America/Los_Angeles"
        )
        let san = Airport(
            code: "SAN",
            name: "San Diego International",
            city: "San Diego",
            coordinate: .init(latitude: 32.7338, longitude: -117.1933),
            countryCode: "US",
            timeZoneIdentifier: "America/Los_Angeles"
        )
        let pdx = Airport(
            code: "PDX",
            name: "Portland International",
            city: "Portland",
            coordinate: .init(latitude: 45.5898, longitude: -122.5951),
            countryCode: "US",
            timeZoneIdentifier: "America/Los_Angeles"
        )
        let jfk = Airport(
            code: "JFK",
            name: "John F. Kennedy International",
            city: "New York",
            coordinate: .init(latitude: 40.6413, longitude: -73.7781),
            countryCode: "US",
            timeZoneIdentifier: "America/New_York"
        )
        let lhr = Airport(
            code: "LHR",
            name: "London Heathrow",
            city: "London",
            coordinate: .init(latitude: 51.4700, longitude: -0.4543),
            countryCode: "GB",
            timeZoneIdentifier: "Europe/London"
        )
        let lax = Airport(
            code: "LAX",
            name: "Los Angeles International",
            city: "Los Angeles",
            coordinate: .init(latitude: 33.9416, longitude: -118.4085),
            countryCode: "US",
            timeZoneIdentifier: "America/Los_Angeles"
        )
        let syd = Airport(
            code: "SYD",
            name: "Sydney Kingsford Smith",
            city: "Sydney",
            coordinate: .init(latitude: -33.9399, longitude: 151.1753),
            countryCode: "AU",
            timeZoneIdentifier: "Australia/Sydney"
        )
        let cases = [
            ("SJC-SAN", sjc, san, 0.5),
            ("PDX-SAN", pdx, san, 0.76),
            ("SFO-JFK", Airport.sfo, jfk, 0.5),
            ("SFO-LHR", Airport.sfo, lhr, 0.5),
            ("ICN-SFO", Airport.icn, Airport.sfo, 0.5),
            ("LAX-SYD", lax, syd, 0.5),
        ]
        let outputDirectory = URL(
            fileURLWithPath: "/private/tmp/airspace-route-frame-cases",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )

        for (name, origin, destination, progress) in cases {
            let flight = TrackedFlight(
                airlineName: "Preview Air",
                marketingNumber: name.replacingOccurrences(of: "-", with: ""),
                operationalCallsign: "PREVIEW",
                aircraftType: "Boeing 787-9",
                aircraftTypeCode: "B789",
                origin: origin,
                destination: destination,
                scheduleSummary: "",
                durationSummary: "-",
                distanceSummary: "-",
                liveStatusSummary: "Preview",
                dataSourceSummary: "Test",
                routeProgress: progress,
                lastPosition: nil,
                lastUpdated: nil
            )
            let image = try await RouteFrameMapRenderer().renderMap(
                for: flight,
                progress: progress,
                size: CGSize(width: 466, height: 466)
            )
            guard
                let data = Self.pngData(image),
                let bitmap = NSBitmapImageRep(data: data)
            else {
                return XCTFail("Could not render \(name)")
            }
            try data.write(
                to: outputDirectory.appendingPathComponent("\(name).png")
            )

            if let globePreview = NativeInstrumentPreview.snapshot(
                page: .flightGlobe,
                flight: flight,
                localRadarMapImage: nil,
                geodesicMapImage: nil,
                routeFrameMapImage: image,
                airlineLogoImage: nil,
                routeProgress: progress,
                locationName: "San Jose",
                accent: .green,
                launch: .preview
            ), let previewData = Self.pngData(globePreview) {
                try previewData.write(
                    to: outputDirectory.appendingPathComponent(
                        "\(name)-globe-preview.png"
                    )
                )
            }
            if let framePreview = NativeInstrumentPreview.snapshot(
                page: .flightFrame,
                flight: flight,
                localRadarMapImage: nil,
                geodesicMapImage: nil,
                routeFrameMapImage: image,
                airlineLogoImage: nil,
                routeProgress: progress,
                locationName: "San Jose",
                accent: .green,
                launch: .preview
            ), let previewData = Self.pngData(framePreview) {
                try previewData.write(
                    to: outputDirectory.appendingPathComponent(
                        "\(name)-frame-preview.png"
                    )
                )
            }

            var visibleMapPixels = 0
            for y in stride(from: 0, to: 466, by: 3) {
                for x in stride(from: 0, to: 466, by: 3) {
                    guard let color = bitmap.colorAt(x: x, y: y)?
                        .usingColorSpace(.deviceRGB)
                    else { continue }
                    if color.greenComponent > 0.075 ||
                        color.blueComponent > 0.09 {
                        visibleMapPixels += 1
                    }
                }
            }
            XCTAssertGreaterThan(
                visibleMapPixels,
                1_200,
                "\(name) rendered as an effectively empty layer"
            )
        }
    }

    func testCompanionLoadsExistingMapAssetsOnStartup() throws {
        let pictures = FileManager.default.urls(
            for: .picturesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.homeDirectoryForCurrentUser
        let cache = pictures.appendingPathComponent(
            "AirspaceInstrument",
            isDirectory: true
        )
        guard FileManager.default.fileExists(
            atPath: cache
                .appendingPathComponent(
                    "YP111_geodesic_map_466.png"
                )
                .path
        ) else {
            throw XCTSkip("No generated map cache on this Mac")
        }

        let model = CompanionModel(resumeSavedMonitoring: false)
        XCTAssertNotNil(model.latestLocalRadarMapImage)
        XCTAssertNotNil(model.latestGeodesicMapImage)
        XCTAssertNotNil(model.latestRouteFrameMapImage)
    }

    func testNativeViewPresentsFramebufferWithoutMirroring() throws {
        let width = 3
        let height = 2
        let bytes: [UInt8] = [
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            255, 255, 0, 255,
            0, 255, 255, 255,
            255, 0, 255, 255,
        ]
        let data = Data(bytes)
        guard
            let provider = CGDataProvider(data: data as CFData),
            let source = CGImage(
                width: width,
                height: height,
                bitsPerComponent: 8,
                bitsPerPixel: 32,
                bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo(
                    rawValue:
                        CGImageAlphaInfo.noneSkipLast.rawValue
                ),
                provider: provider,
                decode: nil,
                shouldInterpolate: false,
                intent: .defaultIntent
            )
        else {
            return XCTFail("Could not make orientation fixture")
        }

        let view = NativeInstrumentView(
            frame: NSRect(
                x: 0,
                y: 0,
                width: width,
                height: height
            )
        )
        view.display(source)
        guard let rendered =
            view.bitmapImageRepForCachingDisplay(in: view.bounds)
        else {
            return XCTFail("Could not allocate hosted-view capture")
        }
        view.cacheDisplay(in: view.bounds, to: rendered)

        let expected = NSBitmapImageRep(cgImage: source)
        var uprightError = 0.0
        var mirroredError = 0.0
        for y in 0..<height {
            for x in 0..<width {
                guard
                    let actualColor = rendered
                        .colorAt(x: x, y: y)?
                        .usingColorSpace(.deviceRGB),
                    let uprightColor = expected
                        .colorAt(x: x, y: y)?
                        .usingColorSpace(.deviceRGB),
                    let mirroredColor = expected
                        .colorAt(
                            x: x,
                            y: height - 1 - y
                        )?
                        .usingColorSpace(.deviceRGB)
                else {
                    return XCTFail(
                        "Could not read orientation pixel \(x),\(y)"
                    )
                }
                uprightError += Self.colorDistance(
                    actualColor,
                    uprightColor
                )
                mirroredError += Self.colorDistance(
                    actualColor,
                    mirroredColor
                )
            }
        }
        XCTAssertLessThan(
            uprightError,
            mirroredError * 0.25,
            "Hosted framebuffer was reflected vertically"
        )
    }

    func testEveryDevicePreviewPageRendersAtInstrumentResolution() throws {
        let outputDirectory = URL(
            fileURLWithPath: ProcessInfo.processInfo.environment[
                "AIRSPACE_GUIDE_OUTPUT_DIR"
            ] ?? "/private/tmp/airspace-device-previews",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )
        let model = CompanionModel(resumeSavedMonitoring: false)
        let now = Date()
        var guideFlight = TrackedFlight.airPremiaYP111
        guideFlight.actualDeparture = now.addingTimeInterval(-3 * 3600)
        guideFlight.estimatedArrival = now.addingTimeInterval(8 * 3600)
        guideFlight.elapsedSummary = "3h 00m"
        guideFlight.remainingSummary = "8h 00m"
        guideFlight.routeDistanceKilometers = 9_085
        guideFlight.altitudeMeters = 11_278
        guideFlight.groundSpeedKPH = 903
        guideFlight.headingDegrees = 94
        guideFlight.verticalPhase = "cruise"

        for page in DevicePreviewPage.allCases {
            var pageFlight = guideFlight
            if page == .flightDetail {
                pageFlight.originTerminal = "1"
                pageFlight.originGate = "55"
                pageFlight.destinationTerminal = "2"
                pageFlight.destinationGate = "G12"
            }
            let nativeImage = NativeInstrumentPreview.snapshot(
                page: page,
                flight: pageFlight,
                localRadarMapImage: model.latestLocalRadarMapImage,
                geodesicMapImage: model.latestGeodesicMapImage,
                routeFrameMapImage: model.latestRouteFrameMapImage,
                airlineLogoImage: model.latestAirlineLogoImage,
                routeProgress: 0.56,
                locationName: "San Jose",
                accent: .green,
                launch: .preview
            )
            let image: NSImage?
            if let nativeImage {
                image = nativeImage
            } else {
                let preview = InstrumentPreview(
                    page: page,
                    flight: pageFlight,
                    localRadarMapImage: model.latestLocalRadarMapImage,
                    geodesicMapImage: model.latestGeodesicMapImage,
                    routeFrameMapImage: model.latestRouteFrameMapImage,
                    airlineLogoImage: model.latestAirlineLogoImage,
                    routeProgress: 0.56,
                    locationName: "San Jose",
                    accent: .green,
                    clockCityKeys: [
                        "ICN", "MOW", "DUS", "JFK", "SJC",
                    ],
                    launch: .preview
                )
                .frame(width: 466, height: 466)
                let renderer = ImageRenderer(content: preview)
                renderer.proposedSize = ProposedViewSize(
                    width: 466,
                    height: 466
                )
                renderer.scale = 1
                image = renderer.nsImage
            }

            guard
                let image,
                let data = image.tiffRepresentation,
                let bitmap = NSBitmapImageRep(data: data),
                let png = bitmap.representation(using: .png, properties: [:])
            else {
                XCTFail("Could not render \(page.rawValue)")
                continue
            }

            XCTAssertEqual(bitmap.pixelsWide, 466)
            XCTAssertEqual(bitmap.pixelsHigh, 466)
            try png.write(to: outputDirectory.appendingPathComponent("\(page.rawValue).png"))
        }

        let jfk = Airport(
            code: "JFK",
            name: "John F. Kennedy International",
            city: "New York",
            coordinate: .init(latitude: 40.6413, longitude: -73.7781),
            countryCode: "US",
            timeZoneIdentifier: "America/New_York"
        )
        var countdownFlight = TrackedFlight(
            airlineName: "American Airlines",
            marketingNumber: "AA276",
            operationalCallsign: "AAL276",
            aircraftType: "Airbus A321",
            aircraftTypeCode: "A321",
            origin: .sfo,
            destination: jfk,
            scheduleSummary: "SFO → JFK · Aug 9",
            durationSummary: "5h 34m",
            distanceSummary: "4,149 km",
            liveStatusSummary: "Scheduled",
            dataSourceSummary: "Guide fixture",
            routeProgress: 0,
            lastPosition: nil,
            lastUpdated: now
        )
        countdownFlight.estimatedDeparture = now.addingTimeInterval(
            (30 * 60 * 60) + (48 * 60)
        )
        countdownFlight.estimatedArrival = now.addingTimeInterval(
            (36 * 60 * 60) + (22 * 60)
        )
        countdownFlight.routeDistanceKilometers = 4_149
        countdownFlight.originTerminal = "B"
        countdownFlight.originGate = "55"
        let countdownLogo = NSImage(
            contentsOf: ProjectAssetCatalog.assetRoot
                .appendingPathComponent("airline_logos")
                .appendingPathComponent("AA.png")
        )
        XCTAssertNotNil(countdownLogo, "AA guide logo asset is missing")
        guard let countdownImage = NativeInstrumentPreview.snapshot(
            page: .flightGlobe,
            flight: countdownFlight,
            localRadarMapImage: model.latestLocalRadarMapImage,
            geodesicMapImage: model.latestGeodesicMapImage,
            routeFrameMapImage: model.latestRouteFrameMapImage,
            airlineLogoImage: countdownLogo,
            routeProgress: 0,
            locationName: "San Jose",
            accent: .green,
            launch: .preview
        ), let countdownData = Self.pngData(countdownImage) else {
            return XCTFail("Could not render the tracked-flight countdown")
        }
        try countdownData.write(
            to: outputDirectory.appendingPathComponent("flightCountdown.png")
        )

        countdownFlight.estimatedDeparture = now.addingTimeInterval(75 * 60)
        countdownFlight.estimatedArrival = now.addingTimeInterval(6.8 * 3600)
        guard let countdownHoursImage = NativeInstrumentPreview.snapshot(
            page: .flightGlobe,
            flight: countdownFlight,
            localRadarMapImage: model.latestLocalRadarMapImage,
            geodesicMapImage: model.latestGeodesicMapImage,
            routeFrameMapImage: model.latestRouteFrameMapImage,
            airlineLogoImage: countdownLogo,
            routeProgress: 0,
            locationName: "San Jose",
            accent: .green,
            launch: .preview
        ), let countdownHoursData = Self.pngData(countdownHoursImage) else {
            return XCTFail("Could not render the under-24-hour countdown")
        }
        try countdownHoursData.write(
            to: outputDirectory.appendingPathComponent(
                "flightCountdownHours.png"
            )
        )
    }

    func testRadarLocationPreviewRendersForGuide() throws {
        let outputDirectory = URL(
            fileURLWithPath: ProcessInfo.processInfo.environment[
                "AIRSPACE_GUIDE_OUTPUT_DIR"
            ] ?? "/private/tmp/airspace-device-previews",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )
        let preview = ZStack {
            Circle().fill(.black)
            RadarLocationSettingsTwin(
                accent: .green,
                batteryPercent: 85,
                charging: true
            )
                .frame(width: 466, height: 466)
                .clipShape(Circle())
        }
        .frame(width: 466, height: 466)
        let hostingView = NSHostingView(rootView: preview)
        hostingView.frame = CGRect(x: 0, y: 0, width: 466, height: 466)
        hostingView.appearance = NSAppearance(named: .darkAqua)
        hostingView.layoutSubtreeIfNeeded()
        RunLoop.current.run(until: Date().addingTimeInterval(0.12))
        hostingView.layoutSubtreeIfNeeded()
        guard
            let bitmap = hostingView.bitmapImageRepForCachingDisplay(
                in: hostingView.bounds
            )
        else {
            return XCTFail("Could not render Radar Locations guide page")
        }
        hostingView.cacheDisplay(in: hostingView.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            return XCTFail("Could not encode Radar Locations guide page")
        }
        try png.write(
            to: outputDirectory.appendingPathComponent("radarLocations.png")
        )
    }

    func testExpectedLaunchPreviewUsesTargetDateInsteadOfCountdown() throws {
        let expectedLaunch = TrackedLaunch(
            id: "preview-artemis-expected",
            missionName: "ARTEMIS III",
            fullName: "SLS Block 1 | Artemis III",
            agency: .nasa,
            vehicle: .sls,
            site: .florida,
            padName: "Launch Complex 39B",
            locationName: "Kennedy Space Center",
            net: Date(timeIntervalSince1970: 1_814_313_600),
            precisionName: "Year Half 1",
            precisionAbbreviation: "H1",
            statusName: "To Be Determined",
            statusAbbreviation: "TBD",
            watchURL: URL(string: "https://www.nasa.gov/nasatv/"),
            lastUpdated: Date(),
            currentUTCOffsetSeconds: -14_400,
            weatherProbabilityPercent: nil
        )
        let model = CompanionModel(
            resumeSavedMonitoring: false
        )
        guard
            let image = NativeInstrumentPreview.snapshot(
                page: .launchMain,
                flight: .airPremiaYP111,
                localRadarMapImage:
                    model.latestLocalRadarMapImage,
                geodesicMapImage:
                    model.latestGeodesicMapImage,
                routeFrameMapImage:
                    model.latestRouteFrameMapImage,
                airlineLogoImage:
                    model.latestAirlineLogoImage,
                routeProgress: 0.56,
                locationName: "San Jose",
                accent: .green,
                launch: expectedLaunch
            ),
            let png = Self.pngData(image)
        else {
            return XCTFail(
                "Could not render expected launch state"
            )
        }
        let output = URL(
            fileURLWithPath:
                "/private/tmp/airspace-device-previews/" +
                "launchMainExpected.png"
        )
        try png.write(to: output)
    }

    func testAgencyLaunchPreviewsRenderForGuide() throws {
        let outputDirectory = URL(
            fileURLWithPath: "/private/tmp/airspace-device-previews",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )
        let model = CompanionModel(resumeSavedMonitoring: false)
        let now = Date()
        let launches: [(String, TrackedLaunch, DevicePreviewPage)] = [
            (
                "launchMain",
                .preview,
                .launchMain
            ),
            (
                "launchMap",
                .preview,
                .launchMap
            ),
            (
                "launchQR",
                .preview,
                .launchQR
            ),
            (
                "launchRoscosmos",
                TrackedLaunch(
                    id: "preview-soyuz",
                    missionName: "SOYUZ MS-33",
                    fullName: "Soyuz MS-33 Crewed Mission",
                    agency: .roscosmos,
                    vehicle: .soyuz,
                    site: .baikonur,
                    padName: "SITE 31/6",
                    locationName: "Baikonur Cosmodrome",
                    net: now.addingTimeInterval(38 * 86_400 + 5_712),
                    precisionName: "Hour",
                    precisionAbbreviation: "H",
                    statusName: "Scheduled",
                    statusAbbreviation: "GO",
                    watchURL: URL(string: "https://www.roscosmos.ru/"),
                    lastUpdated: now,
                    currentUTCOffsetSeconds: 18_000,
                    weatherProbabilityPercent: 91
                ),
                .launchMain
            ),
            (
                "launchSpaceX",
                TrackedLaunch(
                    id: "preview-starship",
                    missionName: "STARSHIP FLIGHT 12",
                    fullName: "Starship Integrated Flight Test 12",
                    agency: .spacex,
                    vehicle: .starship,
                    site: .starbase,
                    padName: "OLM-A",
                    locationName: "Starbase, Texas",
                    net: now.addingTimeInterval(22 * 86_400 + 3_245),
                    precisionName: "Hour",
                    precisionAbbreviation: "H",
                    statusName: "Go for Launch",
                    statusAbbreviation: "GO",
                    watchURL: URL(string: "https://www.spacex.com/launches/"),
                    lastUpdated: now,
                    currentUTCOffsetSeconds: -18_000,
                    weatherProbabilityPercent: 86
                ),
                .launchMain
            ),
        ]

        for (filename, launch, page) in launches {
            guard let image = NativeInstrumentPreview.snapshot(
                page: page,
                flight: .airPremiaYP111,
                localRadarMapImage: model.latestLocalRadarMapImage,
                geodesicMapImage: model.latestGeodesicMapImage,
                routeFrameMapImage: model.latestRouteFrameMapImage,
                airlineLogoImage: model.latestAirlineLogoImage,
                routeProgress: 0.56,
                locationName: "San Jose",
                accent: .green,
                launch: launch
            ),
            let data = image.tiffRepresentation,
            let bitmap = NSBitmapImageRep(data: data),
            let png = bitmap.representation(using: .png, properties: [:])
            else {
                return XCTFail("Could not render \(filename)")
            }
            try png.write(
                to: outputDirectory.appendingPathComponent("\(filename).png")
            )
        }

        guard let weatherRadar = NativeInstrumentPreview.snapshot(
            page: .weatherRadar,
            flight: .airPremiaYP111,
            localRadarMapImage: model.latestLocalRadarMapImage,
            geodesicMapImage: model.latestGeodesicMapImage,
            routeFrameMapImage: model.latestRouteFrameMapImage,
            airlineLogoImage: model.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            accent: .green,
            launch: .preview
        ),
        let weatherData = weatherRadar.tiffRepresentation,
        let weatherBitmap = NSBitmapImageRep(data: weatherData),
        let weatherPNG = weatherBitmap.representation(
            using: .png,
            properties: [:]
        ) else {
            return XCTFail("Could not render Weather Radar")
        }
        try weatherPNG.write(
            to: outputDirectory.appendingPathComponent("weatherRadar.png")
        )
    }

    func testTrackedFlightTimingPagesRenderForEveryJourneyState() throws {
        let now = Date()
        var preflight = TrackedFlight.airPremiaYP111
        preflight.estimatedDeparture =
            now.addingTimeInterval(75 * 60)
        preflight.estimatedArrival =
            now.addingTimeInterval(12 * 3600)
        preflight.routeDistanceKilometers = 9_085
        preflight.originTerminal = "B"
        preflight.originGate = "55"

        var enroute = TrackedFlight.airPremiaYP111
        enroute.actualDeparture =
            now.addingTimeInterval(-3 * 3600)
        enroute.estimatedArrival =
            now.addingTimeInterval(8 * 3600)
        enroute.elapsedSummary = "3h 00m"
        enroute.remainingSummary = "8h 00m"
        enroute.routeDistanceKilometers = 9_085

        var arrived = enroute
        arrived.actualArrival =
            now.addingTimeInterval(-20 * 60)
        arrived.elapsedSummary = "10h 42m"
        arrived.remainingSummary = "0m"

        let model = CompanionModel(
            resumeSavedMonitoring: false
        )
        let outputDirectory = URL(
            fileURLWithPath:
                "/private/tmp/airspace-flight-timing-previews",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: outputDirectory,
            withIntermediateDirectories: true
        )

        for (state, flight) in [
            ("preflight", preflight),
            ("enroute", enroute),
            ("arrived", arrived),
        ] {
            for page in 0...1 {
                guard let image =
                    NativeInstrumentPreview.snapshot(
                        page: .flightGlobe,
                        flight: flight,
                        localRadarMapImage:
                            model.latestLocalRadarMapImage,
                        geodesicMapImage:
                            model.latestGeodesicMapImage,
                        routeFrameMapImage:
                            model.latestRouteFrameMapImage,
                        airlineLogoImage:
                            model.latestAirlineLogoImage,
                        routeProgress:
                            state == "preflight" ? 0 : 0.56,
                        locationName: "San Jose",
                        accent: .green,
                        launch: .preview,
                        trackedTimePage: page
                    ),
                    let tiff = image.tiffRepresentation,
                    let bitmap = NSBitmapImageRep(data: tiff),
                    let png = bitmap.representation(
                        using: .png,
                        properties: [:]
                    )
                else {
                    return XCTFail(
                        "Could not render \(state) page \(page)"
                    )
                }
                XCTAssertEqual(bitmap.pixelsWide, 466)
                XCTAssertEqual(bitmap.pixelsHigh, 466)
                try png.write(
                    to: outputDirectory.appendingPathComponent(
                        "\(state)-\(page).png"
                    )
                )
            }
        }
    }

    func testDeviceEstimatedPositionRendersOnEveryTrackerPage() throws {
        let snapshot = try JSONDecoder().decode(DeviceTrackerSnapshot.self, from: Data(
            #"{"deviceTrackingSupported":true,"preparedFlightCount":1,"status":"En route (estimated)","journeyPhase":"enroute","estimatedPosition":true,"routeProgress":0.52}"#.utf8
        ))
        var flight = snapshot.updating(.airPremiaYP111)
        flight.actualDeparture = Date().addingTimeInterval(-6 * 3600)
        flight.estimatedArrival = Date().addingTimeInterval(6 * 3600)
        let model = CompanionModel(resumeSavedMonitoring: false)
        let directory = URL(fileURLWithPath: "/private/tmp/airspace-estimated-position-previews", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        for page in [DevicePreviewPage.flightGlobe, .flightFrame, .flightDetail] {
            let image = try XCTUnwrap(NativeInstrumentPreview.snapshot(
                page: page, flight: flight,
                localRadarMapImage: model.latestLocalRadarMapImage,
                geodesicMapImage: model.latestGeodesicMapImage,
                routeFrameMapImage: model.latestRouteFrameMapImage,
                airlineLogoImage: model.latestAirlineLogoImage,
                routeProgress: 0.52, locationName: "San Jose", accent: .green, launch: .preview
            ))
            let bitmap = try XCTUnwrap(NSBitmapImageRep(data: XCTUnwrap(image.tiffRepresentation)))
            XCTAssertEqual(bitmap.pixelsWide, 466)
            XCTAssertEqual(bitmap.pixelsHigh, 466)
            let png = try XCTUnwrap(bitmap.representation(using: .png, properties: [:]))
            try png.write(to: directory.appendingPathComponent("\(page.rawValue).png"))
        }
    }

    func testTrackedFlightTimingAutomaticallyRotatesAfterFiveSeconds() throws {
        let now = Date()
        var flight = TrackedFlight.airPremiaYP111
        flight.actualDeparture =
            now.addingTimeInterval(-3 * 3600)
        flight.estimatedArrival =
            now.addingTimeInterval(8 * 3600)
        flight.elapsedSummary = "3h 00m"
        flight.remainingSummary = "8h 00m"
        flight.routeDistanceKilometers = 9_085
        let model = CompanionModel(
            resumeSavedMonitoring: false
        )

        let before = NativeInstrumentPreview.snapshot(
            page: .flightGlobe,
            flight: flight,
            localRadarMapImage:
                model.latestLocalRadarMapImage,
            geodesicMapImage:
                model.latestGeodesicMapImage,
            routeFrameMapImage:
                model.latestRouteFrameMapImage,
            airlineLogoImage:
                model.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            accent: .green,
            launch: .preview,
            trackedTimePage: 0
        )
        Thread.sleep(forTimeInterval: 5.1)
        _ = NativeInstrumentPreview.snapshot(
            page: .flightGlobe,
            flight: flight,
            localRadarMapImage:
                model.latestLocalRadarMapImage,
            geodesicMapImage:
                model.latestGeodesicMapImage,
            routeFrameMapImage:
                model.latestRouteFrameMapImage,
            airlineLogoImage:
                model.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            accent: .green,
            launch: .preview
        )
        Thread.sleep(forTimeInterval: 0.55)
        let after = NativeInstrumentPreview.snapshot(
            page: .flightGlobe,
            flight: flight,
            localRadarMapImage:
                model.latestLocalRadarMapImage,
            geodesicMapImage:
                model.latestGeodesicMapImage,
            routeFrameMapImage:
                model.latestRouteFrameMapImage,
            airlineLogoImage:
                model.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            accent: .green,
            launch: .preview
        )

        guard
            let beforeData = Self.pngData(before),
            let afterData = Self.pngData(after)
        else {
            return XCTFail(
                "Could not capture automatic timing rotation"
            )
        }
        XCTAssertNotEqual(
            beforeData,
            afterData,
            "Tracked timing page did not rotate after five seconds"
        )
    }

    func testWeatherPreviewChangesWithSelectedCity() throws {
        let sanJose = Self.weatherSnapshot(
            locationKey: "SJC",
            temperature: 22,
            summary: "Clear"
        )
        let moscow = Self.weatherSnapshot(
            locationKey: "MOW",
            temperature: -8,
            summary: "Snow"
        )
        let common = CompanionModel(resumeSavedMonitoring: false)
        let first = NativeInstrumentPreview.snapshot(
            page: .weatherCurrent,
            flight: .airPremiaYP111,
            localRadarMapImage: common.latestLocalRadarMapImage,
            geodesicMapImage: common.latestGeodesicMapImage,
            routeFrameMapImage: common.latestRouteFrameMapImage,
            airlineLogoImage: common.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            locationKey: "SJC",
            accent: .green,
            launch: .preview,
            liveWeather: sanJose
        )
        let second = NativeInstrumentPreview.snapshot(
            page: .weatherCurrent,
            flight: .airPremiaYP111,
            localRadarMapImage: common.latestLocalRadarMapImage,
            geodesicMapImage: common.latestGeodesicMapImage,
            routeFrameMapImage: common.latestRouteFrameMapImage,
            airlineLogoImage: common.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "Moscow",
            locationKey: "MOW",
            accent: .green,
            launch: .preview,
            liveWeather: moscow
        )

        XCTAssertNotEqual(
            Self.pngData(first),
            Self.pngData(second),
            "Weather preview retained the previous selected city"
        )
    }

    func testWorldClockPreviewInvalidatesWhenCityOrderChanges() throws {
        let common = CompanionModel(resumeSavedMonitoring: false)
        let first = NativeInstrumentPreview.snapshot(
            page: .clockWorld,
            flight: .airPremiaYP111,
            localRadarMapImage: common.latestLocalRadarMapImage,
            geodesicMapImage: common.latestGeodesicMapImage,
            routeFrameMapImage: common.latestRouteFrameMapImage,
            airlineLogoImage: common.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            locationKey: "SJC",
            accent: .green,
            clockCityKeys: ["SJC", "ICN", "MOW", "DUS", "JFK"],
            launch: .preview
        )
        let second = NativeInstrumentPreview.snapshot(
            page: .clockWorld,
            flight: .airPremiaYP111,
            localRadarMapImage: common.latestLocalRadarMapImage,
            geodesicMapImage: common.latestGeodesicMapImage,
            routeFrameMapImage: common.latestRouteFrameMapImage,
            airlineLogoImage: common.latestAirlineLogoImage,
            routeProgress: 0.56,
            locationName: "San Jose",
            locationKey: "SJC",
            accent: .green,
            clockCityKeys: ["SJC", "JFK", "DUS", "MOW", "ICN"],
            launch: .preview
        )

        XCTAssertNotEqual(
            Self.pngData(first),
            Self.pngData(second),
            "World Clock preview reused the old city order"
        )
    }

    private static func colorDistance(
        _ lhs: NSColor,
        _ rhs: NSColor
    ) -> Double {
        let red =
            lhs.redComponent - rhs.redComponent
        let green =
            lhs.greenComponent - rhs.greenComponent
        let blue =
            lhs.blueComponent - rhs.blueComponent
        return red * red + green * green + blue * blue
    }

    private static func pngData(_ image: NSImage?) -> Data? {
        guard
            let image,
            let tiff = image.tiffRepresentation,
            let bitmap = NSBitmapImageRep(data: tiff)
        else {
            return nil
        }
        return bitmap.representation(
            using: .png,
            properties: [:]
        )
    }

    private static func weatherSnapshot(
        locationKey: String,
        temperature: Int,
        summary: String
    ) -> DeviceClient.WeatherSnapshot {
        DeviceClient.WeatherSnapshot(
            ok: true,
            weatherValid: true,
            airQualityValid: true,
            locationKey: locationKey,
            fetchedUnix: Int(Date().timeIntervalSince1970),
            temperature: temperature,
            apparentTemperature: temperature,
            code: summary == "Snow" ? 71 : 0,
            humidityPercent: summary == "Snow" ? 91 : 42,
            pressureHPA: 1_015,
            precipitationTenthsMM: summary == "Snow" ? 22 : 0,
            uvTenths: summary == "Snow" ? 0 : 56,
            aqi: 18,
            windKT: 7,
            windDegrees: 280,
            unit: "C",
            summary: summary,
            wind: "W 7 kt",
            sunrise: "06:12",
            sunset: "20:04",
            forecast: [
                .init(
                    day: "THU",
                    code: summary == "Snow" ? 71 : 0,
                    high: temperature + 3,
                    low: temperature - 5,
                    precipitationPercent: summary == "Snow" ? 80 : 0
                ),
            ]
        )
    }
}
