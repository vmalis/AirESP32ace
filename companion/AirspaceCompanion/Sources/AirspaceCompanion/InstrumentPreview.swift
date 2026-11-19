// AirESP32ace
// Created by VM, August 2026

import AppKit
import SwiftUI

enum DevicePreviewPage: String, CaseIterable, Identifiable {
    case radarMain
    case radarLocations
    case radarConnectivity
    case radarBrightness
    case radarAPIs
    case aircraftDetail
    case flightGlobe
    case flightFrame
    case flightDetail
    case weatherCurrent
    case weatherForecast
    case weatherRadar
    case spaceSolar
    case spaceMoon
    case spaceISS
    case clockGMT
    case clockWorld
    case launchMain
    case launchMap
    case launchQR
    case powerMenu
    case dockModeOn
    case dockModeOff
    case spaceEclipse

    var id: String { rawValue }

    var isTrackedFlight: Bool {
        self == .flightGlobe || self == .flightFrame || self == .flightDetail
    }

    static func trackerPages(for flight: TrackedFlight?, at now: Date) -> [Self] {
        guard let flight, !flight.isExpiredAfterArrival(at: now),
              !flight.showsScheduledCountdown(at: now) else { return [.flightGlobe] }
        return [.flightGlobe, .flightFrame, .flightDetail]
    }

    var title: String {
        switch self {
        case .radarMain: "Nearby Radar"
        case .radarLocations: "Radar Locations"
        case .radarConnectivity: "Radar Settings"
        case .radarBrightness: "Display Brightness"
        case .radarAPIs: "API Status"
        case .aircraftDetail: "Aircraft Detail"
        case .flightGlobe: "Live Route"
        case .flightFrame: "Straight-line Route"
        case .flightDetail: "Tracked Flight Detail"
        case .weatherCurrent: "Current Weather"
        case .weatherForecast: "Five-Day Forecast"
        case .weatherRadar: "Weather Radar"
        case .spaceSolar: "Solar System"
        case .spaceMoon: "Earth and Moon"
        case .spaceISS: "ISS Tracker"
        case .clockGMT: "GMT Clock"
        case .clockWorld: "World Clock"
        case .launchMain: "Next Launch"
        case .launchMap: "Launch Site"
        case .launchQR: "Watch Live"
        case .powerMenu: "Power Menu"
        case .dockModeOn: "Dock Mode On"
        case .dockModeOff: "Dock Mode Off"
        case .spaceEclipse: "Next Total Eclipse"
        }
    }

    var gestureHint: String {
        switch self {
        case .radarMain: "MAIN · SWIPE FROM TOP FOR SETTINGS"
        case .radarLocations: "SETTINGS · PAGE 1"
        case .radarConnectivity: "SETTINGS · PAGE 2"
        case .radarBrightness: "SETTINGS · PAGE 3"
        case .radarAPIs: "SETTINGS · PAGE 4"
        case .aircraftDetail: "TAP AIRCRAFT · BUTTON TO CLOSE"
        case .flightGlobe: "MAIN · SWIPE DOWN FOR STRAIGHT-LINE ROUTE"
        case .flightFrame: "SWIPE UP FOR MAIN · DOWN FOR DETAIL"
        case .flightDetail: "SWIPE DOWN TO RETURN"
        case .weatherCurrent: "MAIN · SWIPE UP OR DOWN"
        case .weatherForecast: "SWIPE DOWN FOR CURRENT"
        case .weatherRadar: "TAP CENTER TO PLAY"
        case .spaceSolar: "SWIPE UP FOR ECLIPSE · DOWN FOR MOON"
        case .spaceMoon: "SWIPE UP FOR SOLAR SYSTEM"
        case .spaceISS: "BUTTON FOR ECLIPSE · SWIPE DOWN FOR SOLAR"
        case .clockGMT: "MAIN · SWIPE DOWN FOR WORLD TIME"
        case .clockWorld: "TAP A CITY TO SELECT"
        case .launchMain: "MAIN · SWIPE UP OR DOWN"
        case .launchMap: "SWIPE DOWN FOR MAIN"
        case .launchQR: "SWIPE UP FOR MAIN"
        case .powerMenu: "HOLD TOP BUTTON"
        case .dockModeOn: "PRESS BOTH BUTTONS"
        case .dockModeOff: "PRESS BOTH BUTTONS"
        case .spaceEclipse: "BUTTON FOR ISS · SWIPE DOWN FOR SOLAR"
        }
    }

    var glowColor: Color {
        switch self {
        case .weatherCurrent, .weatherForecast, .weatherRadar:
            .cyan
        case .spaceSolar, .spaceMoon, .spaceISS, .spaceEclipse:
            .indigo
        case .clockGMT, .clockWorld:
            .red
        case .launchMain, .launchMap, .launchQR:
            .yellow
        case .powerMenu, .dockModeOn, .dockModeOff:
            .green
        default:
            .green
        }
    }
}

struct InstrumentPreview: View {
    private static let deviceCanvasSize: CGFloat = 466

    let page: DevicePreviewPage
    let flight: TrackedFlight
    let localRadarMapImage: NSImage?
    let geodesicMapImage: NSImage?
    let routeFrameMapImage: NSImage?
    let airlineLogoImage: NSImage?
    let routeProgress: Double
    let locationName: String
    let accent: DeviceAccentColor
    let clockCityKeys: [String]
    let launch: TrackedLaunch
    var flightAwareAPIState: APICredentialState = .valid
    var openSkyAPIState: APICredentialState = .valid
    var airLabsAPIState: APICredentialState = .valid
    var radarLocations: [RadarLocationPreset] = RadarLocationPreset.all
    var selectedRadarLocationKey: String = "SJC"
    var deviceBatteryPercent: Int?
    var deviceBatteryCharging = false
    var liveAircraft: DeviceClient.AircraftSnapshot?
    var liveWeather: DeviceClient.WeatherSnapshot?
    var weatherUnits: WeatherDisplayUnits = .celsius

    var body: some View {
        GeometryReader { proxy in
            let scale = min(
                proxy.size.width / Self.deviceCanvasSize,
                proxy.size.height / Self.deviceCanvasSize
            )

            previewSurface
                .frame(
                    width: Self.deviceCanvasSize,
                    height: Self.deviceCanvasSize
                )
                .scaleEffect(scale)
                .frame(
                    width: proxy.size.width,
                    height: proxy.size.height
                )
        }
        .aspectRatio(1, contentMode: .fit)
    }

    @ViewBuilder
    private var previewSurface: some View {
        if NativeInstrumentPreview.isAvailable &&
            page != .radarAPIs &&
            page != .radarLocations {
            NativeInstrumentPreview(
                page: page,
                flight: flight,
                localRadarMapImage: localRadarMapImage,
                geodesicMapImage: geodesicMapImage,
                routeFrameMapImage: routeFrameMapImage,
                airlineLogoImage: airlineLogoImage,
                routeProgress: routeProgress,
                locationName: locationName,
                locationKey: selectedRadarLocationKey,
                accent: accent,
                clockCityKeys: clockCityKeys,
                weatherUnits: weatherUnits,
                launch: launch,
                liveAircraft: liveAircraft,
                liveWeather: liveWeather
            )
            .background(.black)
            .clipShape(Circle())
        } else if page.isTrackedFlight && flight.showsScheduledCountdown() {
            ScheduledFlightCountdownTwin(flight: flight, accent: accent.previewBrightColor)
                .background(.black)
                .clipShape(Circle())
        } else {
            instrumentSurface
        }
    }

    private var instrumentSurface: some View {
        ZStack {
            Circle()
                .fill(.black)
            Circle()
                .fill(backgroundGradient)
                .overlay(
                    pageContent
                        .frame(
                            width: Self.deviceCanvasSize,
                            height: Self.deviceCanvasSize
                        )
                        .clipShape(Circle())
                )
                .overlay(outerGlass)
                .shadow(color: page.glowColor.opacity(0.22), radius: 26)
        }
        .clipShape(Circle())
    }

    private var backgroundGradient: LinearGradient {
        switch page {
        case .weatherCurrent, .weatherForecast, .weatherRadar:
            LinearGradient(colors: [.black, Color(red: 0.015, green: 0.04, blue: 0.055)], startPoint: .top, endPoint: .bottom)
        case .spaceSolar, .spaceMoon, .spaceISS, .spaceEclipse:
            LinearGradient(colors: [.black, Color(red: 0.012, green: 0.014, blue: 0.035)], startPoint: .top, endPoint: .bottom)
        case .launchMain, .launchMap, .launchQR:
            LinearGradient(
                colors: [
                    .black,
                    Color(
                        red: 0.035,
                        green: 0.028,
                        blue: 0.012
                    ),
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        default:
            LinearGradient(colors: [.black, Color(red: 0.008, green: 0.018, blue: 0.014)], startPoint: .topLeading, endPoint: .bottomTrailing)
        }
    }

    @ViewBuilder
    private var pageContent: some View {
        switch page {
        case .radarMain:
            RadarScreenTwin(
                mapImage: localRadarMapImage,
                accent: accent.previewColor
            )
        case .radarLocations:
            RadarLocationSettingsTwin(
                accent: accent.previewColor,
                locations: radarLocations,
                selectedKey: selectedRadarLocationKey,
                batteryPercent: deviceBatteryPercent,
                charging: deviceBatteryCharging
            )
        case .radarConnectivity:
            RadarConnectivityTwin(
                accent: accent.previewColor,
                weatherUnits: weatherUnits
            )
        case .radarBrightness:
            RadarBrightnessTwin(accent: accent.previewColor)
        case .radarAPIs:
            RadarAPIsTwin(
                accent: accent.previewColor,
                statuses: [
                    flightAwareAPIState,
                    openSkyAPIState,
                    airLabsAPIState,
                ]
            )
        case .aircraftDetail:
            FlightDetailTwin(
                flight: flight,
                accent: accent.previewBrightColor,
                source: "OPENSKY",
                tracksDesignatedFlight: false,
                units: weatherUnits
            )
        case .flightGlobe:
            FlightMapTwin(
                flight: flight,
                mapImage: geodesicMapImage,
                progress: routeProgress,
                vertical: false,
                accent: accent.previewBrightColor,
                units: weatherUnits
            )
        case .flightFrame:
            FlightMapTwin(
                flight: flight,
                mapImage: routeFrameMapImage,
                progress: routeProgress,
                vertical: true,
                accent: accent.previewBrightColor,
                units: weatherUnits
            )
        case .flightDetail:
            FlightDetailTwin(
                flight: flight,
                accent: accent.previewBrightColor,
                source: "FLIGHTAWARE",
                tracksDesignatedFlight: true,
                units: weatherUnits
            )
        case .weatherCurrent:
            WeatherCurrentTwin(
                location: locationName,
                units: weatherUnits
            )
        case .weatherForecast:
            WeatherForecastTwin(
                location: locationName,
                units: weatherUnits
            )
        case .weatherRadar:
            WeatherRadarTwin(
                location: locationName,
                mapImage: localRadarMapImage,
                accent: accent.previewColor,
                units: weatherUnits
            )
        case .spaceSolar:
            SolarSystemTwin(accent: accent.previewBrightColor)
        case .spaceMoon:
            MoonSystemTwin(accent: accent.previewBrightColor)
        case .spaceISS:
            ISSTwin(accent: accent.previewBrightColor)
        case .spaceEclipse:
            ISSTwin(accent: accent.previewBrightColor)
        case .clockGMT, .clockWorld:
            ClockScreenTwin(
                worldMode: page == .clockWorld,
                cityKeys: clockCityKeys,
                locationName: locationName
            )
        case .launchMain:
            LaunchFallbackTwin(
                launch: launch,
                view: .main
            )
        case .launchMap:
            LaunchFallbackTwin(
                launch: launch,
                view: .map
            )
        case .launchQR:
            LaunchFallbackTwin(
                launch: launch,
                view: .qr
            )
        case .powerMenu:
            SystemOverlayTwin(kind: .power)
        case .dockModeOn:
            SystemOverlayTwin(kind: .dockOn)
        case .dockModeOff:
            SystemOverlayTwin(kind: .dockOff)
        }
    }

    private var outerGlass: some View {
        Circle()
            .strokeBorder(
                LinearGradient(
                    colors: [.white.opacity(0.42), page.glowColor.opacity(0.28), .white.opacity(0.08)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                lineWidth: 2
            )
            .overlay(
                Circle()
                    .stroke(.white.opacity(0.08), lineWidth: 22)
                    .blur(radius: 11)
                    .padding(10)
            )
    }
}

private enum SystemOverlayKind: Equatable {
    case power
    case dockOn
    case dockOff
}

private struct SystemOverlayTwin: View {
    let kind: SystemOverlayKind

    var body: some View {
        ZStack {
            Color.black
            if kind == .power {
                VStack(spacing: 18) {
                    Text("POWER")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(.secondary)
                    Text("Choose an action")
                        .font(.system(size: 24, weight: .semibold))
                    systemButton("Sleep", color: .green)
                    systemButton("Power Off", color: .red)
                }
                .padding(54)
            } else {
                VStack(spacing: 12) {
                    Circle()
                        .fill(kind == .dockOn ? Color.green : Color.gray)
                        .frame(width: 18, height: 18)
                    Text("DOCK MODE")
                        .font(.system(size: 20, weight: .medium))
                        .foregroundStyle(.secondary)
                    Text(kind == .dockOn ? "ON" : "OFF")
                        .font(.system(size: 48, weight: .semibold))
                }
            }
        }
        .foregroundStyle(.white)
    }

    private func systemButton(
        _ title: String,
        color: Color
    ) -> some View {
        HStack(spacing: 20) {
            Circle()
                .fill(color)
                .frame(width: 14, height: 14)
            Text(title)
                .font(.system(size: 22, weight: .semibold))
            Spacer()
        }
        .padding(.horizontal, 28)
        .frame(height: 78)
        .background(
            RoundedRectangle(cornerRadius: 28)
                .fill(color.opacity(0.08))
                .overlay(
                    RoundedRectangle(cornerRadius: 28)
                        .stroke(color.opacity(0.45), lineWidth: 1)
                )
        )
    }
}

private enum LaunchFallbackView {
    case main
    case map
    case qr
}

private struct LaunchFallbackTwin: View {
    let launch: TrackedLaunch
    let view: LaunchFallbackView

    private var mapImage: NSImage? {
        NSImage(
            contentsOf:
                ProjectAssetCatalog.assetRoot
                    .appendingPathComponent(
                        "space/" +
                        launch.site.assetName +
                        ".png"
                    )
        )
    }

    var body: some View {
        ZStack {
            Color.black
            switch view {
            case .main:
                main
            case .map:
                map
            case .qr:
                qr
            }
        }
    }

    private var header: some View {
        VStack(spacing: 5) {
            Text(launch.agency.displayName)
                .font(
                    .system(
                        size: 14,
                        weight: .bold,
                        design: .monospaced
                    )
                )
                .tracking(2)
                .foregroundStyle(.white.opacity(0.88))
            Text(launch.missionName)
                .font(
                    .system(
                        size: 22,
                        weight: .semibold,
                        design: .rounded
                    )
                )
                .foregroundStyle(.white)
                .lineLimit(1)
        }
    }

    private var main: some View {
        VStack(spacing: 0) {
            header.padding(.top, 26)
            Spacer()
            Image(systemName: "rocket.fill")
                .font(.system(size: 160, weight: .ultraLight))
                .foregroundStyle(
                    LinearGradient(
                        colors: [
                            .white,
                            .white.opacity(0.45),
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
                .shadow(
                    color: .yellow.opacity(0.4),
                    radius: 24,
                    y: 20
                )
            Spacer()
            Text(
                launch.net.formatted(
                    date: .abbreviated,
                    time: .shortened
                )
            )
            .font(
                .system(
                    size: 31,
                    weight: .medium,
                    design: .monospaced
                )
            )
            .foregroundStyle(.white)
            Text(launch.padName)
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.bottom, 28)
        }
        .padding(.horizontal, 34)
    }

    private var map: some View {
        ZStack {
            if let mapImage {
                Image(nsImage: mapImage)
                    .resizable()
                    .scaledToFill()
            }
            Canvas { context, _ in
                var path = Path()
                path.move(to: CGPoint(x: 208, y: 378))
                path.addCurve(
                    to: CGPoint(x: 356, y: 78),
                    control1: CGPoint(x: 202, y: 322),
                    control2: CGPoint(x: 295, y: 206)
                )
                context.stroke(
                    path,
                    with: .linearGradient(
                        Gradient(
                            colors: [.white, .yellow]
                        ),
                        startPoint:
                            CGPoint(x: 208, y: 378),
                        endPoint:
                            CGPoint(x: 356, y: 78)
                    ),
                    style: StrokeStyle(
                        lineWidth: 3,
                        lineCap: .round
                    )
                )
            }
            VStack {
                header.padding(.top, 26)
                Spacer()
                Text(launch.site.displayName)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.white)
                    .padding(.bottom, 28)
            }
        }
    }

    private var qr: some View {
        VStack(spacing: 20) {
            header.padding(.top, 28)
            Spacer()
            Image(systemName: "qrcode")
                .font(.system(size: 230, weight: .thin))
                .foregroundStyle(.white)
            Text(
                launch.watchURL == nil
                    ? "STREAM NOT AVAILABLE"
                    : "SCAN TO WATCH"
            )
            .font(
                .system(
                    size: 13,
                    weight: .semibold,
                    design: .monospaced
                )
            )
            .tracking(1.2)
            .foregroundStyle(.white.opacity(0.82))
            Spacer()
        }
    }
}

private extension DeviceAccentColor {
    var previewColor: Color {
        switch self {
        case .green: Color(red: 0.0, green: 0.48, blue: 0.12)
        case .cyan: Color(red: 0.0, green: 0.64, blue: 0.75)
        case .orange: Color(red: 0.83, green: 0.48, blue: 0.09)
        case .red: Color(red: 0.74, green: 0.15, blue: 0.18)
        }
    }

    var previewBrightColor: Color {
        switch self {
        case .green: Color(red: 0.14, green: 0.82, blue: 0.23)
        case .cyan: Color(red: 0.21, green: 0.85, blue: 0.92)
        case .orange: Color(red: 1.0, green: 0.69, blue: 0.31)
        case .red: Color(red: 1.0, green: 0.41, blue: 0.43)
        }
    }
}

private struct RadarScreenTwin: View {
    let mapImage: NSImage?
    let accent: Color

    var body: some View {
        ZStack {
            if let mapImage {
                Image(nsImage: mapImage)
                    .resizable()
                    .scaledToFill()
                    .saturation(0)
                    .brightness(-0.08)
                    .contrast(1.08)
            }

            Canvas { context, size in
                if mapImage == nil {
                    context.fill(
                        Path(CGRect(origin: .zero, size: size)),
                        with: .color(.black)
                    )
                    drawGraphiteRoads(context: &context, size: size)
                } else {
                    context.fill(
                        Path(CGRect(origin: .zero, size: size)),
                        with: .color(.black.opacity(0.18))
                    )
                }

                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                for fraction in [0.24, 0.48, 0.72, 0.96] {
                    let radius = size.width * fraction / 2
                    context.stroke(
                        Path(
                            ellipseIn: CGRect(
                                x: center.x - radius,
                                y: center.y - radius,
                                width: radius * 2,
                                height: radius * 2
                            )
                        ),
                        with: .color(accent.opacity(fraction == 0.48 ? 0.31 : 0.18)),
                        lineWidth: 0.8
                    )
                }

                var sweep = Path()
                sweep.move(to: center)
                sweep.addLine(
                    to: CGPoint(
                        x: center.x + size.width * 0.50,
                        y: center.y - size.height * 0.12
                    )
                )
                sweep.addLine(
                    to: CGPoint(
                        x: center.x + size.width * 0.49,
                        y: center.y - size.height * 0.20
                    )
                )
                sweep.closeSubpath()
                context.fill(sweep, with: .color(accent.opacity(0.28)))

                drawPlaneGlyph(
                    context: &context,
                    center: CGPoint(x: size.width * 0.69, y: size.height * 0.34),
                    angle: -0.7,
                    color: accent.opacity(0.95)
                )
                drawPlaneGlyph(
                    context: &context,
                    center: CGPoint(x: size.width * 0.31, y: size.height * 0.58),
                    angle: 0.35,
                    color: accent.opacity(0.78)
                )
                drawPlaneGlyph(
                    context: &context,
                    center: CGPoint(x: size.width * 0.52, y: size.height * 0.77),
                    angle: -1.15,
                    color: accent.opacity(0.90)
                )
            }
        }
    }
}

struct RadarLocationSettingsTwin: View {
    let accent: Color
    let locations: [RadarLocationPreset]
    let selectedKey: String
    let batteryPercent: Int?
    let charging: Bool

    init(
        accent: Color,
        locations: [RadarLocationPreset] = RadarLocationPreset.all,
        selectedKey: String = "SJC",
        batteryPercent: Int? = nil,
        charging: Bool = false
    ) {
        self.accent = accent
        self.locations = locations
        self.selectedKey = selectedKey
        self.batteryPercent = batteryPercent
        self.charging = charging
    }

    var body: some View {
        ZStack {
            SettingsPageDots(selected: 0, accent: accent)
                .position(x: 233, y: 28)

            HStack(spacing: 6) {
                if charging {
                    Image(systemName: "bolt.fill")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(batteryColor)
                }

                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3, style: .continuous)
                        .stroke(.white.opacity(0.52), lineWidth: 1)
                    RoundedRectangle(cornerRadius: 2, style: .continuous)
                        .fill(batteryColor)
                        .frame(width: batteryFillWidth, height: 6)
                        .padding(.leading, 2)
                }
                .frame(width: 25, height: 11)
                .overlay(alignment: .trailing) {
                    Capsule()
                        .fill(.white.opacity(0.52))
                        .frame(width: 2, height: 6)
                        .offset(x: 4)
                }

                Text(batteryPercent.map { "\($0)%" } ?? "—")
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundStyle(.white.opacity(0.68))
            }
            .position(x: 233, y: 55)

            VStack(spacing: 2) {
                HStack(spacing: 6) {
                    ForEach(0..<4, id: \.self) { slot in
                        cityCell(slot: slot)
                    }
                }
                HStack(spacing: 6) {
                    ForEach(4..<8, id: \.self) { slot in
                        cityCell(slot: slot)
                    }
                }
            }
            .position(x: 233, y: 180)

            HStack(spacing: 14) {
                ForEach([10, 20, 50], id: \.self) { radius in
                    Text("\(radius)")
                        .font(.system(size: 11, weight: .semibold, design: .rounded))
                        .foregroundStyle(radius == 20 ? .black : .white)
                        .frame(width: 38, height: 38)
                        .background(
                            radius == 20
                                ? accent
                                : Color.white.opacity(0.055),
                            in: Circle()
                        )
                        .overlay(
                            Circle().stroke(.white.opacity(0.13), lineWidth: 0.7)
                        )
                }
            }
            .frame(width: 316, height: 67)
            .background(.white.opacity(0.022), in: Capsule())
            .position(x: 233, y: 338)

            CloseSettingsGlyph()
                .position(x: 233, y: 415)
        }
    }

    @ViewBuilder
    private func cityCell(slot: Int) -> some View {
        if locations.indices.contains(slot) {
            let location = locations[slot]
            let selected = selectedKey.caseInsensitiveCompare(location.key) == .orderedSame
            VStack(spacing: 0) {
                ZStack {
                    Circle()
                        .fill(.white.opacity(0.025))
                    if let cityImage = ProjectAssetCatalog.cityImage(
                        for: location.key
                    ), let cityCGImage = cityImage.cgImage(
                        forProposedRect: nil,
                        context: nil,
                        hints: nil
                    ) {
                        Image(decorative: cityCGImage, scale: 1)
                            .resizable()
                            .scaledToFit()
                            .padding(5)
                    } else {
                        Image(systemName: "mappin.and.ellipse")
                            .font(.system(size: 13, weight: .medium))
                            .foregroundStyle(
                                selected ? accent : Color.white.opacity(0.68)
                            )
                    }
                }
                .overlay(
                    Circle().stroke(
                        selected ? accent.opacity(0.95) : Color.clear,
                        lineWidth: 2
                    )
                )
                .frame(width: 64, height: 64)

                Text(location.key)
                    .font(.system(size: 11, weight: .semibold, design: .monospaced))
                    .tracking(0.6)
                    .foregroundStyle(selected ? accent : .white.opacity(0.82))
                    .frame(height: 18)
                Text(location.displayCity)
                    .font(.system(size: 8, weight: .regular, design: .rounded))
                    .foregroundStyle(.white.opacity(0.64))
                    .lineLimit(1)
                    .frame(width: 90, height: 18)
            }
            .frame(width: 90, height: 100)
        } else {
            Color.clear.frame(width: 90, height: 100)
        }
    }

    private var batteryColor: Color {
        guard let batteryPercent else { return .white.opacity(0.28) }
        if batteryPercent < 25 { return .red }
        if batteryPercent < 50 { return .yellow }
        return .green
    }

    private var batteryFillWidth: CGFloat {
        guard let batteryPercent else { return 0 }
        return 20 * CGFloat(min(max(batteryPercent, 0), 100)) / 100
    }
}

private struct RadarConnectivityTwin: View {
    let accent: Color
    let weatherUnits: WeatherDisplayUnits

    var body: some View {
        ZStack {
            SettingsPageDots(selected: 1, accent: accent)
                .position(x: 233, y: 28)

            Text("Studio Wi-Fi")
                .font(.system(size: 12, weight: .medium, design: .rounded))
                .foregroundStyle(.white)
                .position(x: 233, y: 78)

            HStack(spacing: 0) {
                Image(systemName: "wifi")
                    .foregroundStyle(accent)
                    .font(.system(size: 24))
                    .frame(width: 82, height: 62)
                    .background(accent.opacity(0.10))
                Image(systemName: "personalhotspot")
                    .foregroundStyle(.white.opacity(0.38))
                    .font(.system(size: 21))
                    .frame(width: 82, height: 62)
            }
            .background(.white.opacity(0.04), in: Capsule())
            .clipShape(Capsule())
            .overlay(Capsule().stroke(.white.opacity(0.12), lineWidth: 1))
            .position(x: 233, y: 147)

            SettingsToggleTwin(title: "CALLSIGN", enabled: true, accent: accent)
                .frame(width: 112, height: 84)
                .position(x: 105, y: 251)
            SettingsToggleTwin(title: "WEDGE", enabled: true, accent: accent)
                .frame(width: 112, height: 84)
                .position(x: 233, y: 251)
            SettingsWeatherTwin(units: weatherUnits, accent: accent)
                .frame(width: 112, height: 84)
                .position(x: 361, y: 251)

            ForEach(
                Array([Color.green, Color.orange, Color.cyan, Color.red].enumerated()),
                id: \.offset
            ) { item in
                ZStack {
                    Circle()
                        .fill(.white.opacity(0.025))
                    Circle()
                        .fill(item.element)
                        .frame(width: 27, height: 27)
                    if item.offset == 0 {
                        Circle()
                            .fill(.white)
                            .frame(width: 9, height: 9)
                    }
                }
                .frame(width: 54, height: 54)
                .position(x: CGFloat(149 + item.offset * 56), y: 339)
            }

            CloseSettingsGlyph()
                .position(x: 233, y: 415)
        }
    }
}

private struct RadarBrightnessTwin: View {
    let accent: Color

    var body: some View {
        ZStack {
            SettingsPageDots(selected: 2, accent: accent)
                .position(x: 233, y: 28)

            Circle()
                .stroke(.white.opacity(0.08), lineWidth: 15)
                .frame(width: 246, height: 246)
                .position(x: 233, y: 216)
            Circle()
                .trim(from: 0, to: 0.78)
                .stroke(
                    accent,
                    style: StrokeStyle(
                        lineWidth: 16,
                        lineCap: .round
                    )
                )
                .rotationEffect(.degrees(-90))
                .frame(width: 246, height: 246)
                .position(x: 233, y: 216)

            Image(systemName: "chevron.up")
                .font(.system(size: 24, weight: .medium))
                .foregroundStyle(accent)
                .position(x: 233, y: 126)
            Text("BRIGHTNESS")
                .font(.system(size: 16, weight: .medium, design: .rounded))
                .foregroundStyle(.white.opacity(0.72))
                .position(x: 233, y: 194)
            Text("80%")
                .font(.system(size: 40, weight: .medium, design: .rounded))
                .foregroundStyle(.white)
                .position(x: 233, y: 236)
            Image(systemName: "chevron.down")
                .font(.system(size: 24, weight: .medium))
                .foregroundStyle(accent)
                .position(x: 233, y: 294)
            Text("SWIPE TO ADJUST BRIGHTNESS")
                .font(.system(size: 12, weight: .medium, design: .rounded))
                .foregroundStyle(.white.opacity(0.44))
                .position(x: 233, y: 358)

            CloseSettingsGlyph()
                .position(x: 233, y: 415)
        }
    }
}

private struct RadarAPIsTwin: View {
    let accent: Color
    let statuses: [APICredentialState]

    var body: some View {
        ZStack {
            SettingsPageDots(selected: 3, accent: accent)
                .position(x: 233, y: 28)
            Text("APIs")
                .font(.system(size: 20, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
                .position(x: 233, y: 62)

            ForEach(Array(apiProviders.enumerated()), id: \.offset) { item in
                let rowY = [112.0, 212.0, 313.0][item.offset]
                let provider = item.element
                APIProviderLogo(
                    provider: provider,
                    width: provider.deviceSize.width,
                    height: provider.deviceSize.height
                )
                .position(
                    x: provider == .flightAware ? 157 : 192,
                    y: rowY
                )

                if provider == .flightAware {
                    Text(provider.displayName)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(Color(red: 0.38, green: 0.78, blue: 1.0))
                        .frame(width: 104, alignment: .leading)
                        .position(x: 257, y: rowY)
                }

                ZStack {
                    switch statuses[item.offset] {
                    case .checking:
                        ProgressView()
                            .controlSize(.regular)
                            .tint(.orange)
                    case .valid:
                        Circle()
                            .fill(accent.opacity(0.10))
                        Circle()
                            .stroke(accent.opacity(0.40), lineWidth: 1)
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(accent)
                    case .invalid:
                        Circle()
                            .fill(Color.red.opacity(0.12))
                        Circle()
                            .stroke(Color.red.opacity(0.55), lineWidth: 1)
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.red)
                    }
                }
                .frame(width: 36, height: 36)
                .position(x: 351, y: rowY)

                if item.offset < 2 {
                    LinearGradient(
                        colors: [.clear, accent.opacity(0.65), .clear],
                        startPoint: .leading,
                        endPoint: .trailing
                    )
                    .frame(width: 300, height: 1)
                    .position(x: 232, y: item.offset == 0 ? 158 : 260)
                }
            }

            CloseSettingsGlyph()
                .position(x: 233, y: 415)
        }
    }

    private var apiProviders: [APIProviderArtwork] {
        [.flightAware, .openSky, .airLabs]
    }
}

private struct SettingsPageDots: View {
    let selected: Int
    let accent: Color

    var body: some View {
        HStack(spacing: 7) {
            ForEach(0..<4, id: \.self) { index in
                Circle()
                    .fill(index == selected ? accent : .white.opacity(0.24))
                    .frame(width: index == selected ? 7 : 5, height: index == selected ? 7 : 5)
            }
        }
    }
}

private struct SettingsToggleTwin: View {
    let title: String
    let enabled: Bool
    let accent: Color

    var body: some View {
        VStack(spacing: 7) {
            Text(title)
                .font(.system(size: 12, weight: .semibold, design: .monospaced))
                .foregroundStyle(.white.opacity(0.74))
            Circle()
                .fill(enabled ? accent : .white.opacity(0.34))
                .frame(width: 15, height: 15)
                .shadow(color: enabled ? accent : .clear, radius: 4)
        }
        .frame(width: 122, height: 84)
        .background(.white.opacity(0.035), in: Capsule())
        .overlay(Capsule().stroke(.white.opacity(0.13), lineWidth: 1))
    }
}

private struct SettingsWeatherTwin: View {
    let units: WeatherDisplayUnits
    let accent: Color

    var body: some View {
        VStack(spacing: 7) {
            Text("UNITS")
                .font(.system(size: 12, weight: .semibold, design: .monospaced))
                .foregroundStyle(.white.opacity(0.74))
            HStack(spacing: 9) {
                ForEach(WeatherDisplayUnits.allCases) { candidate in
                    Text(candidate.rawValue)
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(
                            candidate == units ? accent : .white.opacity(0.34)
                        )
                }
            }
        }
        .frame(width: 122, height: 84)
        .background(.white.opacity(0.035), in: Capsule())
        .overlay(Capsule().stroke(.white.opacity(0.13), lineWidth: 1))
    }
}

private struct CloseSettingsGlyph: View {
    var body: some View {
        Image(systemName: "xmark")
            .font(.system(size: 20, weight: .medium))
            .foregroundStyle(.white.opacity(0.72))
            .frame(width: 58, height: 58)
            .background(.white.opacity(0.055), in: Circle())
            .overlay(Circle().stroke(.white.opacity(0.14), lineWidth: 1))
    }
}

/// Keep the no-native-renderer fallback honest too; never substitute an
/// airborne mockup while the real instrument is still counting down.
private struct ScheduledFlightCountdownTwin: View {
    let flight: TrackedFlight
    let accent: Color

    var body: some View {
        TimelineView(.periodic(from: .now, by: 30)) { context in
            let departure = flight.estimatedDeparture ?? flight.scheduledDeparture ?? context.date
            let minutes = max(0, Int(ceil(departure.timeIntervalSince(context.date) / 60)))
            VStack(spacing: 20) {
                Text(flight.marketingNumber).font(.system(size: 18))
                HStack(spacing: 40) {
                    endpoint(flight.origin)
                    Text("> ").foregroundStyle(.secondary)
                    endpoint(flight.destination)
                }
                Text(flight.isDepartureDelayed ? "DELAYED - DEPARTS IN" : "DEPARTS IN")
                    .font(.system(size: 12))
                    .foregroundStyle(flight.isDepartureDelayed ? .yellow : .secondary)
                if minutes >= 1440 {
                    Text("\(minutes / 1440)").font(.system(size: 48)).foregroundStyle(accent)
                    Text("DAYS").font(.system(size: 14)).foregroundStyle(.secondary)
                }
                Text(String(format: "%02d:%02d", (minutes % 1440) / 60, minutes % 60))
                    .font(.system(size: minutes >= 1440 ? 24 : 48)).foregroundStyle(accent)
                Text("HOURS : MINUTES").font(.system(size: 12)).foregroundStyle(.secondary)
            }
            .foregroundStyle(.white)
            .frame(width: 466, height: 466)
        }
    }

    private func endpoint(_ airport: Airport) -> some View {
        VStack(spacing: 4) {
            Text(airport.code).font(.system(size: 28))
            Text(airport.displayCity).font(.system(size: 14)).foregroundStyle(.secondary)
        }
    }
}

private struct FlightMapTwin: View {
    let flight: TrackedFlight
    let mapImage: NSImage?
    let progress: Double
    let vertical: Bool
    let accent: Color
    let units: WeatherDisplayUnits

    private var clampedProgress: CGFloat {
        min(0.98, max(0.02, progress))
    }

    var body: some View {
        GeometryReader { proxy in
            let routePoints = routePoints(in: proxy.size)
            let planePoint = point(
                along: routePoints,
                progress: clampedProgress
            )
            ZStack {
                if let mapImage {
                    Image(nsImage: mapImage)
                        .resizable()
                        .scaledToFill()
                } else {
                    GraphiteWorldTwin(vertical: vertical, accent: accent)
                }

                FlightFallbackRouteTwin(
                    points: routePoints,
                    accent: accent
                )

                Image(systemName: "airplane")
                    .font(.system(size: 23, weight: .semibold))
                    .foregroundStyle(accent)
                    .rotationEffect(.degrees(vertical ? -90 : 0))
                    .shadow(color: accent.opacity(0.75), radius: 6)
                    .position(planePoint)

                if vertical {
                    FlightFramePanelsTwin(
                        flight: flight,
                        accent: accent,
                        units: units
                    )
                    EndpointTwin(
                        code: flight.destination.code,
                        city: flight.destination.displayCity,
                        alignment: .center
                    )
                    .frame(width: proxy.size.width * 0.34)
                    .position(
                        frameEndpointLabelPoint(
                            points: routePoints,
                            planePoint: planePoint,
                            destination: true,
                            size: proxy.size
                        )
                    )
                    EndpointTwin(
                        code: flight.origin.code,
                        city: flight.origin.displayCity,
                        alignment: .center
                    )
                    .frame(width: proxy.size.width * 0.34)
                    .position(
                        frameEndpointLabelPoint(
                            points: routePoints,
                            planePoint: planePoint,
                            destination: false,
                            size: proxy.size
                        )
                    )
                } else {
                    FlightGlobePanelsTwin(flight: flight, accent: accent)
                    EndpointTwin(
                        code: flight.origin.code,
                        city: flight.origin.displayCity,
                        alignment: .center
                    )
                    .frame(width: proxy.size.width * (126.0 / 466.0))
                    .position(
                        endpointLabelPoint(
                            points: routePoints,
                            planePoint: planePoint,
                            destination: false,
                            size: proxy.size
                        )
                    )
                    EndpointTwin(
                        code: flight.destination.code,
                        city: flight.destination.displayCity,
                        alignment: .center
                    )
                    .frame(width: proxy.size.width * (126.0 / 466.0))
                    .position(
                        endpointLabelPoint(
                            points: routePoints,
                            planePoint: planePoint,
                            destination: true,
                            size: proxy.size
                        )
                    )
                }
            }
        }
    }

    private func routePoints(in size: CGSize) -> [CGPoint] {
        if vertical {
            return [
                CGPoint(x: size.width * 0.50, y: size.height * 0.794),
                CGPoint(x: size.width * 0.50, y: size.height * 0.206)
            ]
        }
        return MapKitFlightMapRenderer.routeScreenPoints(
            for: flight,
            size: size
        )
    }

    private func point(
        along points: [CGPoint],
        progress: CGFloat
    ) -> CGPoint {
        guard let first = points.first else { return .zero }
        guard points.count > 1 else { return first }
        let scaled = progress * CGFloat(points.count - 1)
        let lower = min(points.count - 2, max(0, Int(scaled)))
        let fraction = scaled - CGFloat(lower)
        let start = points[lower]
        let end = points[lower + 1]
        return CGPoint(
            x: start.x + (end.x - start.x) * fraction,
            y: start.y + (end.y - start.y) * fraction
        )
    }

    private func endpointLabelPoint(
        points: [CGPoint],
        planePoint: CGPoint,
        destination: Bool,
        size: CGSize
    ) -> CGPoint {
        guard points.count > 1 else {
            return CGPoint(x: size.width / 2, y: size.height / 2)
        }
        let endpoint = destination ? points[points.count - 1] : points[0]
        let adjacent = destination ? points[points.count - 2] : points[1]
        var routeX = adjacent.x - endpoint.x
        var routeY = adjacent.y - endpoint.y
        var length = hypot(routeX, routeY)
        if length < 0.5 {
            routeX = size.width / 2 - endpoint.x
            routeY = size.height / 2 - endpoint.y
            length = hypot(routeX, routeY)
        }
        if length < 0.5 {
            routeX = destination ? -1 : 1
            routeY = 0
            length = 1
        }
        routeX /= length
        routeY /= length
        let outsideX = -routeX
        let outsideY = -routeY
        let normalX = -routeY
        let normalY = routeX
        let proximity = destination
            ? max(0, min(1, (clampedProgress - 0.84) / 0.16))
            : max(0, min(1, (0.16 - clampedProgress) / 0.16))
        let smooth = proximity * proximity * (3 - 2 * proximity)
        let scale = min(size.width, size.height) / 466.0
        let halfWidth = 63.0 * scale
        let halfHeight = 21.0 * scale
        let minimumCenterX = halfWidth + 8.0 * scale
        let maximumCenterX = size.width - halfWidth - 8.0 * scale
        let minimumCenterY = (102.0 + 21.0) * scale
        let maximumCenterY = (310.0 + 21.0) * scale
        let directions: [(CGFloat, CGFloat)] = [
            (normalX, normalY),
            (-normalX, -normalY),
            (outsideX + normalX, outsideY + normalY),
            (outsideX - normalX, outsideY - normalY),
            (-1, 0),
            (1, 0),
            (outsideX, outsideY),
        ]
        var bestPoint = CGPoint(x: size.width / 2, y: size.height / 2)
        var bestScore = CGFloat.greatestFiniteMagnitude
        for (index, direction) in directions.enumerated() {
            let directionLength = hypot(direction.0, direction.1)
            guard directionLength >= 0.5 else { continue }
            let directionX = direction.0 / directionLength
            let directionY = direction.1 / directionLength
            let projectedHalfExtent =
                abs(directionX) * halfWidth +
                abs(directionY) * halfHeight
            let offset = projectedHalfExtent +
                (14.0 + 18.0 * smooth) * scale
            let rawX = endpoint.x + directionX * offset
            let rawY = endpoint.y + directionY * offset
            let centerX = max(minimumCenterX, min(maximumCenterX, rawX))
            let centerY = max(minimumCenterY, min(maximumCenterY, rawY))

            var score = hypot(centerX - rawX, centerY - rawY) * 16
            if index >= 2 {
                score += (index >= 6 ? 180 : 36) * scale
            }

            let routeNormalX = -routeY
            let routeNormalY = routeX
            let routeDistance = abs(
                (centerX - endpoint.x) * routeNormalX +
                (centerY - endpoint.y) * routeNormalY
            )
            let routeHalfExtent =
                abs(routeNormalX) * halfWidth +
                abs(routeNormalY) * halfHeight
            let routeClearance = routeDistance - routeHalfExtent
            if routeClearance < 12 * scale {
                score += (12 * scale - routeClearance) * 900
            }

            // A short or strongly curved route can bend back through a label
            // even when the local endpoint tangent is clear. Mirror the
            // firmware's full-polyline collision check for every route.
            if points.count > 1 {
                for segmentIndex in 0..<(points.count - 1) {
                    let start = points[segmentIndex]
                    let end = points[segmentIndex + 1]
                    let segmentX = end.x - start.x
                    let segmentY = end.y - start.y
                    let segmentLengthSquared =
                        segmentX * segmentX + segmentY * segmentY
                    var nearest: CGFloat = 0
                    if segmentLengthSquared > 0.25 {
                        nearest = (
                            (centerX - start.x) * segmentX +
                            (centerY - start.y) * segmentY
                        ) / segmentLengthSquared
                        nearest = max(0, min(1, nearest))
                    }
                    let nearestX = start.x + nearest * segmentX
                    let nearestY = start.y + nearest * segmentY
                    let boxDX = max(
                        0,
                        abs(centerX - nearestX) - halfWidth
                    )
                    let boxDY = max(
                        0,
                        abs(centerY - nearestY) - halfHeight
                    )
                    let segmentClearance = hypot(boxDX, boxDY)
                    if segmentClearance < 12 * scale {
                        score +=
                            (12 * scale - segmentClearance) * 1_100
                    }
                }
            }

            let aircraftDX = max(
                0,
                abs(centerX - planePoint.x) - halfWidth
            )
            let aircraftDY = max(
                0,
                abs(centerY - planePoint.y) - halfHeight
            )
            let aircraftClearance =
                hypot(aircraftDX, aircraftDY) - 29 * scale
            if aircraftClearance < 14 * scale {
                score += (14 * scale - aircraftClearance) * 1_200
            }

            if score < bestScore {
                bestScore = score
                bestPoint = CGPoint(x: centerX, y: centerY)
            }
        }
        return bestPoint
    }

    private func frameEndpointLabelPoint(
        points: [CGPoint],
        planePoint: CGPoint,
        destination: Bool,
        size: CGSize
    ) -> CGPoint {
        guard let origin = points.first, let destinationPoint = points.last else {
            return CGPoint(x: size.width / 2, y: size.height / 2)
        }
        let endpoint = destination ? destinationPoint : origin
        let rawProximity = destination
            ? (clampedProgress - 0.82) / 0.18
            : (0.18 - clampedProgress) / 0.18
        let proximity = max(0, min(1, rawProximity))
        let smooth = proximity * proximity * (3 - 2 * proximity)
        let labelHeight = size.height * (42.0 / 466.0)
        let labelHalfHeight = labelHeight / 2
        let clearance = size.height * ((18.0 + 24.0 * smooth) / 466.0)
        var centerY = destination
            ? endpoint.y - clearance - labelHalfHeight
            : endpoint.y + clearance + labelHalfHeight

        if abs(planePoint.y - endpoint.y) < size.height * (72.0 / 466.0) {
            let planeHalfHeight = size.height * (25.0 / 466.0)
            let extraGap = size.height * (8.0 / 466.0)
            let safeCenter = destination
                ? planePoint.y - planeHalfHeight - extraGap - labelHalfHeight
                : planePoint.y + planeHalfHeight + extraGap + labelHalfHeight
            centerY = destination
                ? min(centerY, safeCenter)
                : max(centerY, safeCenter)
        }

        let edgeInset = size.height * (6.0 / 466.0)
        centerY = max(
            edgeInset + labelHalfHeight,
            min(size.height - edgeInset - labelHalfHeight, centerY)
        )
        return CGPoint(x: size.width / 2, y: centerY)
    }
}

private struct FlightGlobePanelsTwin: View {
    let flight: TrackedFlight
    let accent: Color

    var body: some View {
        ZStack {
            VStack {
                VStack(spacing: 2) {
                    Text(flight.airlineName.uppercased())
                        .font(.system(size: 7, weight: .semibold, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.62))
                    Text(flight.marketingNumber)
                        .font(.system(size: 16, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 28)
                .padding(.bottom, 34)
                .background(
                    LinearGradient(
                        colors: [.black.opacity(0.78), .clear],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )

                Spacer()

                HStack(spacing: 32) {
                    FlightBottomValue(
                        title: "TRAVELED",
                        value: flight.elapsedSummary ?? "—"
                    )
                    FlightBottomValue(
                        title: "REMAINING",
                        value: flight.remainingSummary ?? "—"
                    )
                    FlightBottomValue(
                        title: "ETA",
                        value: flight.etaSummary ?? "—",
                        color: accent
                    )
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 45)
                .padding(.bottom, 32)
                .background(
                    LinearGradient(
                        colors: [.clear, .black.opacity(0.86)],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
            }

        }
    }
}

private struct FlightBottomValue: View {
    let title: String
    let value: String
    var color: Color = .white

    var body: some View {
        VStack(spacing: 3) {
            Text(title)
                .font(.system(size: 6, weight: .bold, design: .monospaced))
                .foregroundStyle(.white.opacity(0.40))
            Text(value)
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .foregroundStyle(color.opacity(0.90))
        }
        .frame(width: 70)
    }
}

private struct FlightFramePanelsTwin: View {
    let flight: TrackedFlight
    let accent: Color
    let units: WeatherDisplayUnits

    var body: some View {
        ZStack {
            HStack {
                VStack(spacing: 13) {
                    PreviewMetric(symbol: "arrow.up.to.line", value: metricAltitude)
                    PreviewMetric(symbol: "speedometer", value: metricSpeed)
                    PreviewMetric(symbol: "location.north.fill", value: metricHeading)
                }
                .padding(.leading, 23)
                Spacer()
                VStack(spacing: 18) {
                    Text(flight.marketingNumber)
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                    Text(flight.remainingSummary ?? "—")
                        .font(.system(size: 11, weight: .medium, design: .rounded))
                        .foregroundStyle(accent)
                    Text(flight.etaSummary ?? "—")
                        .font(.system(size: 11, weight: .medium, design: .rounded))
                        .foregroundStyle(.white.opacity(0.78))
                }
                .padding(.trailing, 23)
            }
        }
    }

    private var metricAltitude: String {
        units.altitudeLabel(meters: flight.altitudeMeters)
    }

    private var metricSpeed: String {
        units.speedLabel(kilometersPerHour: flight.groundSpeedKPH)
    }

    private var metricHeading: String {
        flight.headingDegrees.map { "\($0)°" } ?? "—"
    }
}

private struct EndpointTwin: View {
    let code: String
    let city: String
    var alignment: HorizontalAlignment = .leading

    var body: some View {
        VStack(alignment: alignment, spacing: 1) {
            Text(code)
                .font(.system(size: 15, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
            Text(city)
                .font(.system(size: 7, weight: .medium, design: .rounded))
                .foregroundStyle(.white.opacity(0.47))
                .lineLimit(1)
        }
    }
}

private struct PreviewMetric: View {
    let symbol: String
    let value: String

    var body: some View {
        VStack(spacing: 3) {
            Image(systemName: symbol)
                .font(.system(size: 11))
                .foregroundStyle(.white.opacity(0.72))
            Text(value)
                .font(.system(size: 7, weight: .semibold, design: .monospaced))
                .foregroundStyle(.white.opacity(0.86))
        }
    }
}

private struct GraphiteWorldTwin: View {
    let vertical: Bool
    let accent: Color

    var body: some View {
        Canvas { context, size in
            context.fill(
                Path(CGRect(origin: .zero, size: size)),
                with: .color(.black)
            )
            drawGraphiteContinents(
                context: &context,
                size: size,
                vertical: vertical
            )
        }
    }
}

private struct FlightFallbackRouteTwin: View {
    let points: [CGPoint]
    let accent: Color

    var body: some View {
        Canvas { context, _ in
            guard points.count > 1 else { return }
            var path = Path()
            path.move(to: points[0])
            for point in points.dropFirst() {
                path.addLine(to: point)
            }
            context.stroke(path, with: .color(accent.opacity(0.23)), lineWidth: 10)
            context.stroke(path, with: .color(accent.opacity(0.92)), lineWidth: 2.2)
        }
    }
}

private struct FlightDetailTwin: View {
    let flight: TrackedFlight
    let accent: Color
    let source: String
    let tracksDesignatedFlight: Bool
    let units: WeatherDisplayUnits
    @State private var showsTrackedAuxiliary = true

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            VStack(spacing: 0) {
                Text(flight.airlineName.uppercased())
                    .font(.system(size: 10, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white.opacity(0.78))
                    .padding(.top, 29)

                Image(systemName: "airplane")
                    .font(.system(size: 50, weight: .regular))
                    .foregroundStyle(accent)
                    .scaleEffect(x: -1, y: 1)
                    .shadow(color: accent.opacity(0.45), radius: 10)
                    .frame(height: 66)

                Text(flight.aircraftType)
                    .font(.system(size: 8, weight: .medium, design: .rounded))
                    .foregroundStyle(.white.opacity(0.54))

                RouteDetailTwin(flight: flight, accent: accent)
                    .frame(height: 89)

                VStack(spacing: 1) {
                    Text(flight.marketingNumber)
                        .font(.system(size: 16, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                    if tracksDesignatedFlight {
                        Text(flight.operationalCallsign.isEmpty ? "—" : flight.operationalCallsign)
                            .font(.system(size: 8, weight: .medium, design: .monospaced))
                            .tracking(0.7)
                            .foregroundStyle(.white.opacity(0.46))
                    }
                }
                .frame(height: tracksDesignatedFlight ? 31 : 22)
                .offset(y: -8)
                .opacity(tracksDesignatedFlight && !showsTrackedAuxiliary ? 0 : 1)

                HStack(spacing: 27) {
                    PreviewMetric(
                        symbol: "arrow.up.to.line",
                        value: units.altitudeLabel(meters: flight.altitudeMeters)
                    )
                    PreviewMetric(
                        symbol: "speedometer",
                        value: units.speedLabel(
                            kilometersPerHour: flight.groundSpeedKPH
                        )
                    )
                    PreviewMetric(
                        symbol: "location.north.fill",
                        value: flight.headingDegrees.map { cardinal($0) } ?? "—"
                    )
                }

                Spacer()
                if tracksDesignatedFlight {
                    let timing = trackedTiming(at: context.date)
                    VStack(spacing: 2) {
                        Text(timing.title)
                            .font(.system(size: 7, weight: .medium, design: .monospaced))
                            .tracking(0.8)
                            .foregroundStyle(.white.opacity(0.46))
                        Text(timing.value)
                            .font(.system(size: 10, weight: .semibold, design: .rounded))
                            .foregroundStyle(.white.opacity(0.92))
                    }
                    .opacity(showsTrackedAuxiliary ? 1 : 0)
                    .animation(.easeInOut(duration: 0.4), value: timing.title)
                    .padding(.bottom, 25)
                } else {
                    Text("\(source)  \(flight.operationalCallsign)")
                        .font(.system(size: 7, weight: .medium, design: .monospaced))
                        .tracking(0.8)
                        .foregroundStyle(.white.opacity(0.34))
                        .padding(.bottom, 31)
                }
            }
        }
        .contentShape(Circle())
        .onTapGesture {
            guard tracksDesignatedFlight else { return }
            withAnimation(.easeInOut(duration: 0.32)) {
                showsTrackedAuxiliary.toggle()
            }
        }
    }

    private func trackedTiming(at date: Date) -> (title: String, value: String) {
        let showSecondValue = Int(date.timeIntervalSince1970 / 5) % 2 == 1
        let preflight = flight.journeyPhase == "preflight"
        if preflight {
            if showSecondValue {
                return (
                    "ETD",
                    trackedLocalTime(
                        flight.estimatedDeparture ?? flight.scheduledDeparture,
                        airport: flight.origin
                    )
                )
            }
            return (
                "\(flight.origin.code) TIME",
                trackedLocalTime(date, airport: flight.origin)
            )
        }
        if showSecondValue {
            return (
                "ETA",
                trackedLocalTime(
                    flight.actualArrival ?? flight.estimatedArrival,
                    airport: flight.destination
                )
            )
        }
        return (
            "\(flight.destination.code) TIME",
            trackedLocalTime(date, airport: flight.destination)
        )
    }

    private func trackedLocalTime(_ date: Date?, airport: Airport) -> String {
        guard let date else { return "—" }
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm"
        formatter.timeZone = airport.timeZoneIdentifier.flatMap(TimeZone.init(identifier:))
            ?? .current
        return formatter.string(from: date)
    }
}

private struct RouteDetailTwin: View {
    let flight: TrackedFlight
    let accent: Color

    var body: some View {
        GeometryReader { proxy in
            Canvas { context, size in
                var path = Path()
                path.move(to: CGPoint(x: size.width * 0.20, y: size.height * 0.72))
                path.addCurve(
                    to: CGPoint(x: size.width * 0.80, y: size.height * 0.72),
                    control1: CGPoint(x: size.width * 0.36, y: size.height * 0.08),
                    control2: CGPoint(x: size.width * 0.64, y: size.height * 0.08)
                )
                context.stroke(path, with: .color(accent.opacity(0.90)), lineWidth: 2.2)
                context.fill(
                    Path(
                        ellipseIn: CGRect(
                            x: size.width * 0.50 - 4,
                            y: size.height * 0.19 - 4,
                            width: 8,
                            height: 8
                        )
                    ),
                    with: .color(.white)
                )
            }

            HStack {
                RouteEndpointFlagTwin(
                    code: flight.origin.code,
                    city: flight.origin.displayCity,
                    country: flight.origin.countryCode
                )
                Spacer()
                RouteEndpointFlagTwin(
                    code: flight.destination.code,
                    city: flight.destination.displayCity,
                    country: flight.destination.countryCode
                )
            }
            .padding(.horizontal, proxy.size.width * 0.05)
            .padding(.top, 31)
        }
    }
}

private struct RouteEndpointFlagTwin: View {
    let code: String
    let city: String
    let country: String?

    var body: some View {
        VStack(spacing: 1) {
            CircularFlagTwin(countryCode: country)
                .frame(width: 25, height: 25)
            Text(code)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
            Text(city)
                .font(.system(size: 6, weight: .medium, design: .rounded))
                .foregroundStyle(.white.opacity(0.48))
                .lineLimit(1)
                .frame(width: 60)
        }
    }
}

private struct CircularFlagTwin: View {
    let countryCode: String?

    var body: some View {
        Canvas { context, size in
            let rect = CGRect(origin: .zero, size: size)
            context.clip(to: Path(ellipseIn: rect))
            switch countryCode?.uppercased() {
            case "US":
                for stripe in 0..<7 {
                    context.fill(
                        Path(
                            CGRect(
                                x: 0,
                                y: CGFloat(stripe) * size.height / 7,
                                width: size.width,
                                height: size.height / 7 + 0.5
                            )
                        ),
                        with: .color(stripe.isMultiple(of: 2) ? .red : .white)
                    )
                }
                context.fill(
                    Path(CGRect(x: 0, y: 0, width: size.width * 0.52, height: size.height * 0.52)),
                    with: .color(.blue)
                )
            case "KR":
                context.fill(Path(rect), with: .color(.white))
                context.fill(
                    Path(ellipseIn: rect.insetBy(dx: size.width * 0.29, dy: size.height * 0.29)),
                    with: .color(.red)
                )
                context.fill(
                    Path(
                        CGRect(
                            x: size.width * 0.21,
                            y: size.height * 0.50,
                            width: size.width * 0.58,
                            height: size.height * 0.29
                        )
                    ),
                    with: .color(.blue)
                )
            case "DE":
                for (index, color) in [Color.black, .red, .yellow].enumerated() {
                    context.fill(
                        Path(
                            CGRect(
                                x: 0,
                                y: CGFloat(index) * size.height / 3,
                                width: size.width,
                                height: size.height / 3 + 0.5
                            )
                        ),
                        with: .color(color)
                    )
                }
            case "RU":
                for (index, color) in [Color.white, .blue, .red].enumerated() {
                    context.fill(
                        Path(
                            CGRect(
                                x: 0,
                                y: CGFloat(index) * size.height / 3,
                                width: size.width,
                                height: size.height / 3 + 0.5
                            )
                        ),
                        with: .color(color)
                    )
                }
            default:
                context.fill(Path(rect), with: .color(.gray.opacity(0.55)))
            }
            context.stroke(Path(ellipseIn: rect.insetBy(dx: 0.7, dy: 0.7)), with: .color(.white.opacity(0.55)), lineWidth: 0.8)
        }
    }
}

private struct WeatherCurrentTwin: View {
    let location: String
    let units: WeatherDisplayUnits

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 13) {
                CityGlyphTwin(location: location)
                    .frame(width: 72, height: 72)
                VStack(alignment: .leading, spacing: 0) {
                    Text(units == .fahrenheit ? "59°" : "15°")
                        .font(.system(size: 42, weight: .light, design: .rounded))
                        .foregroundStyle(.white)
                    Text("CLEAR")
                        .font(.system(size: 8, weight: .semibold, design: .monospaced))
                        .tracking(1)
                        .foregroundStyle(.white.opacity(0.48))
                }
                Image(systemName: "sun.max.fill")
                    .font(.system(size: 38, weight: .light))
                    .foregroundStyle(.yellow)
            }
            .padding(.top, 54)

            LinearGradient(
                colors: [.clear, .white.opacity(0.72), .clear],
                startPoint: .leading,
                endPoint: .trailing
            )
            .frame(width: 240, height: 1)
            .padding(.vertical, 16)

            HStack(spacing: 54) {
                WeatherSmallValue(title: "PRECIP", value: "8%")
                WeatherSmallValue(title: "HUMIDITY", value: "61%")
            }

            VStack(spacing: 12) {
                WeatherBarTwin(title: "WIND", value: "12 km/h", fraction: 0.28, colors: [.cyan, .blue])
                WeatherBarTwin(
                    title: "PRESSURE",
                    value: units == .fahrenheit ? "101.1 kPa" : "758 mmHg",
                    fraction: 0.48,
                    colors: [.blue, .yellow, .green, .yellow, .red]
                )
                WeatherBarTwin(title: "AIR QUALITY", value: "GOOD", fraction: 0.22, colors: [.green, .yellow])
                WeatherBarTwin(title: "UV INDEX", value: "3", fraction: 0.34, colors: [.green, .orange, .red])
            }
            .padding(.top, 19)
            .padding(.horizontal, 58)
            Spacer()
        }
    }
}

private struct WeatherForecastTwin: View {
    let location: String
    let units: WeatherDisplayUnits
    private let days = [
        ("WED", "sun.max.fill", "15", "9"),
        ("THU", "cloud.sun.fill", "17", "10"),
        ("FRI", "cloud.rain.fill", "14", "8"),
        ("SAT", "cloud.fill", "13", "7"),
        ("SUN", "sun.max.fill", "18", "9"),
    ]

    var body: some View {
        VStack(spacing: 18) {
            Text(location.uppercased())
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .tracking(1.1)
                .foregroundStyle(.white)
                .padding(.top, 51)

            LinearGradient(
                colors: [.clear, .white.opacity(0.52), .clear],
                startPoint: .leading,
                endPoint: .trailing
            )
            .frame(width: 230, height: 1)

            HStack(spacing: 9) {
                ForEach(days, id: \.0) { day in
                    VStack(spacing: 12) {
                        Text(day.0)
                            .font(.system(size: 8, weight: .semibold, design: .monospaced))
                            .foregroundStyle(.white.opacity(0.48))
                        Image(systemName: day.1)
                            .font(.system(size: 21))
                            .symbolRenderingMode(.multicolor)
                            .frame(height: 28)
                        Text("\(displayTemperature(day.2))°")
                            .font(.system(size: 17, weight: .medium, design: .rounded))
                            .foregroundStyle(.white)
                        Text("\(displayTemperature(day.3))°")
                            .font(.system(size: 11, weight: .medium, design: .rounded))
                            .foregroundStyle(.white.opacity(0.42))
                    }
                    .frame(width: 46, height: 164)
                    .background(.white.opacity(0.025), in: Capsule())
                }
            }

            Text("5 DAY FORECAST")
                .font(.system(size: 8, weight: .semibold, design: .monospaced))
                .tracking(1.2)
                .foregroundStyle(.white.opacity(0.34))
            Spacer()
        }
    }

    private func displayTemperature(_ raw: String) -> Int {
        let celsius = Int(raw) ?? 0
        return units == .fahrenheit
            ? Int((Double(celsius) * 1.8 + 32.0).rounded())
            : celsius
    }
}

private struct WeatherRadarTwin: View {
    let location: String
    let mapImage: NSImage?
    let accent: Color
    let units: WeatherDisplayUnits

    var body: some View {
        ZStack {
            if let mapImage {
                Image(nsImage: mapImage)
                    .resizable()
                    .scaledToFill()
                    .saturation(0)
                    .brightness(-0.08)
                    .contrast(1.08)
            }

            Canvas { context, size in
                if mapImage == nil {
                    context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.black))
                    drawGraphiteRoads(context: &context, size: size)
                } else {
                    context.fill(
                        Path(CGRect(origin: .zero, size: size)),
                        with: .color(.black.opacity(0.16))
                    )
                }
                for cell in [
                    (CGPoint(x: size.width * 0.30, y: size.height * 0.38), Color.green, 54.0),
                    (CGPoint(x: size.width * 0.54, y: size.height * 0.55), Color.yellow, 38.0),
                    (CGPoint(x: size.width * 0.66, y: size.height * 0.31), Color.purple, 46.0),
                ] {
                    context.fill(
                        Path(ellipseIn: CGRect(x: cell.0.x - cell.2 / 2, y: cell.0.y - cell.2 / 2, width: cell.2, height: cell.2)),
                        with: .color(cell.1.opacity(0.30))
                    )
                }
            }

            VStack {
                ZStack {
                    Circle()
                        .fill(.black.opacity(0.52))
                    Text(units == .fahrenheit ? "59°" : "15°")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                }
                .frame(width: 42, height: 42)
                .padding(.top, 41)

                Spacer()

                HStack(spacing: 20) {
                    ForEach([10, 20, 50], id: \.self) { radius in
                        Text("\(radius)")
                            .font(.system(size: 9, weight: .semibold, design: .rounded))
                            .foregroundStyle(radius == 20 ? .black : .white)
                            .frame(width: 34, height: 34)
                            .background(
                                radius == 20 ? accent : .black.opacity(0.58),
                                in: Circle()
                            )
                    }
                }
                .padding(.bottom, 35)
            }

            VStack {
                Spacer()
                HStack(spacing: 5) {
                    Image(systemName: "play.fill")
                    Capsule()
                        .fill(.white.opacity(0.20))
                        .frame(width: 100, height: 2)
                        .overlay(alignment: .leading) {
                            Capsule().fill(accent).frame(width: 38, height: 2)
                        }
                    Text("+40m")
                }
                .font(.system(size: 7, weight: .medium, design: .monospaced))
                .foregroundStyle(.white.opacity(0.66))
                .padding(.bottom, 83)
            }
        }
    }
}

private struct CityGlyphTwin: View {
    let location: String

    var body: some View {
        ZStack {
            Circle()
                .stroke(
                    LinearGradient(
                        colors: [.cyan, .blue, .purple],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    ),
                    lineWidth: 1.4
                )
            Image(systemName: citySymbol)
                .font(.system(size: 28, weight: .light))
                .foregroundStyle(.cyan)
        }
    }

    private var citySymbol: String {
        let value = location.lowercased()
        if value.contains("san francisco") { return "bridge" }
        if value.contains("san diego") { return "beach.umbrella" }
        if value.contains("moscow") { return "building.columns" }
        if value.contains("petersburg") { return "sparkles" }
        if value.contains("seoul") || value.contains("incheon") { return "building.2" }
        return "building.2.crop.circle"
    }
}

private struct WeatherSmallValue: View {
    let title: String
    let value: String

    var body: some View {
        VStack(spacing: 2) {
            Text(value)
                .font(.system(size: 17, weight: .medium, design: .rounded))
                .foregroundStyle(.white)
            Text(title)
                .font(.system(size: 7, weight: .semibold, design: .monospaced))
                .foregroundStyle(.white.opacity(0.38))
        }
    }
}

private struct WeatherBarTwin: View {
    let title: String
    let value: String
    let fraction: CGFloat
    let colors: [Color]

    var body: some View {
        VStack(spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(value)
            }
            .font(.system(size: 7, weight: .semibold, design: .monospaced))
            .foregroundStyle(.white.opacity(0.70))
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    LinearGradient(colors: colors, startPoint: .leading, endPoint: .trailing)
                        .opacity(0.62)
                    Circle()
                        .fill(.white)
                        .frame(width: 6, height: 6)
                        .offset(x: proxy.size.width * fraction - 3)
                        .shadow(color: .white, radius: 3)
                }
            }
            .frame(height: 6)
            .clipShape(Capsule())
        }
    }
}

private struct SolarSystemTwin: View {
    let accent: Color

    private struct OrbitalElements {
        let base: [Double]
        let rate: [Double]
    }

    /* Same JPL 1800-2050 approximate heliocentric elements used by the
       instrument firmware. Keeping this table here makes the lightweight
       SwiftUI fallback preview agree with the native/device renderer. */
    private static let orbits: [OrbitalElements] = [
        .init(base: [0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593], rate: [0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081]),
        .init(base: [0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255], rate: [0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418]),
        .init(base: [1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0], rate: [0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0]),
        .init(base: [1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891], rate: [0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343]),
        .init(base: [5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909], rate: [-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106]),
        .init(base: [9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448], rate: [-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794]),
        .init(base: [19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503], rate: [-0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589]),
        .init(base: [30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574], rate: [0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664]),
    ]

    private static func normalizedRadians(_ value: Double) -> Double {
        let result = value.truncatingRemainder(dividingBy: 2 * .pi)
        return result < 0 ? result + 2 * .pi : result
    }

    private static func planetAngle(index: Int, at date: Date) -> Double {
        let julianDate = 2_440_587.5 + date.timeIntervalSince1970 / 86_400
        let centuries = (julianDate - 2_451_545.0) / 36_525
        let orbit = orbits[index]
        let value = zip(orbit.base, orbit.rate).map { pair in
            pair.0 + pair.1 * centuries
        }
        let eccentricity = value[1]
        let inclination = value[2] * .pi / 180
        let meanAnomaly = normalizedRadians((value[3] - value[4]) * .pi / 180)
        var eccentricAnomaly = meanAnomaly
        for _ in 0..<8 {
            eccentricAnomaly -=
                (eccentricAnomaly - eccentricity * sin(eccentricAnomaly) - meanAnomaly) /
                (1 - eccentricity * cos(eccentricAnomaly))
        }
        let orbitalX = value[0] * (cos(eccentricAnomaly) - eccentricity)
        let orbitalY = value[0] * sqrt(1 - eccentricity * eccentricity) * sin(eccentricAnomaly)
        let argumentPerihelion = (value[4] - value[5]) * .pi / 180
        let ascendingNode = value[5] * .pi / 180
        let x =
            (cos(argumentPerihelion) * cos(ascendingNode) - sin(argumentPerihelion) * sin(ascendingNode) * cos(inclination)) * orbitalX +
            (-sin(argumentPerihelion) * cos(ascendingNode) - cos(argumentPerihelion) * sin(ascendingNode) * cos(inclination)) * orbitalY
        let y =
            (cos(argumentPerihelion) * sin(ascendingNode) + sin(argumentPerihelion) * cos(ascendingNode) * cos(inclination)) * orbitalX +
            (-sin(argumentPerihelion) * sin(ascendingNode) + cos(argumentPerihelion) * cos(ascendingNode) * cos(inclination)) * orbitalY
        return normalizedRadians(atan2(y, x))
    }

    var body: some View {
        TimelineView(
            .animation(
                minimumInterval: 0.1,
                paused: false
            )
        ) { timeline in
            let elapsed =
                timeline.date
                    .timeIntervalSinceReferenceDate
            let earthPulse =
                0.5 +
                0.5 *
                    sin(
                        elapsed *
                        2 *
                        .pi /
                        2.8
                    )
            let planetAngles = (0..<8).map {
                Self.planetAngle(index: $0, at: timeline.date)
            }
            Canvas { context, size in
                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                let radii = stride(
                    from: size.width * 0.12,
                    through: size.width * 0.46,
                    by: size.width * 0.048
                )
                for (index, radius) in radii.enumerated() {
                    context.stroke(
                        Path(ellipseIn: CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)),
                        with: .color(.white.opacity(0.10)),
                        lineWidth: 0.7
                    )
                    let angle = planetAngles[index]
                    let planet = CGPoint(
                        x: center.x + cos(angle) * radius,
                        y: center.y - sin(angle) * radius
                    )
                    let planetRadius = CGFloat(index == 2 || index == 4 ? 4.2 : 2.8)
                    if index == 2 {
                        let glowRadius =
                            CGFloat(
                                8.0 +
                                earthPulse * 2.5
                            )
                        context.drawLayer { layer in
                            layer.addFilter(
                                .blur(
                                    radius:
                                        4.0 +
                                        earthPulse * 2.0
                                )
                            )
                            layer.fill(
                                Path(
                                    ellipseIn: CGRect(
                                        x: planet.x - glowRadius,
                                        y: planet.y - glowRadius,
                                        width: glowRadius * 2,
                                        height: glowRadius * 2
                                    )
                                ),
                                with: .color(
                                    .cyan.opacity(
                                        0.18 +
                                        earthPulse * 0.20
                                    )
                                )
                            )
                        }
                    }
                    context.fill(
                        Path(ellipseIn: CGRect(x: planet.x - planetRadius, y: planet.y - planetRadius, width: planetRadius * 2, height: planetRadius * 2)),
                        with: .color(index == 2 ? .blue : (index == 4 ? .orange : .white.opacity(0.75)))
                    )
                }
                context.fill(
                    Path(ellipseIn: CGRect(x: center.x - 14, y: center.y - 14, width: 28, height: 28)),
                    with: .color(.orange)
                )
                context.fill(
                    Path(ellipseIn: CGRect(x: center.x - 8, y: center.y - 8, width: 16, height: 16)),
                    with: .color(.yellow)
                )
            }
        }
        .overlay(alignment: .bottom) {
            Text("LIVE HELIOCENTRIC POSITIONS")
                .font(.system(size: 7, weight: .semibold, design: .monospaced))
                .tracking(1)
                .foregroundStyle(.white.opacity(0.34))
                .padding(.bottom, 35)
        }
    }
}

private struct MoonSystemTwin: View {
    let accent: Color

    var body: some View {
        VStack(spacing: 0) {
            GeometryReader { proxy in
                ZStack {
                    Circle()
                        .fill(.orange)
                        .frame(width: 108, height: 108)
                        .blur(radius: 11)
                        .opacity(0.28)
                        .position(x: proxy.size.width / 2, y: -2)
                    Circle()
                        .fill(
                            RadialGradient(
                                colors: [.yellow, .orange, .red],
                                center: .center,
                                startRadius: 4,
                                endRadius: 48
                            )
                        )
                        .frame(width: 92, height: 92)
                        .position(x: proxy.size.width / 2, y: -9)

                    Canvas { context, size in
                        var orbit = Path()
                        orbit.move(to: CGPoint(x: size.width * 0.08, y: size.height * 0.78))
                        orbit.addCurve(
                            to: CGPoint(x: size.width * 0.92, y: size.height * 0.72),
                            control1: CGPoint(x: size.width * 0.34, y: size.height * 0.53),
                            control2: CGPoint(x: size.width * 0.68, y: size.height * 0.56)
                        )
                        context.stroke(orbit, with: .color(.gray.opacity(0.55)), lineWidth: 1.2)
                    }

                    Circle()
                        .fill(
                            RadialGradient(
                                colors: [.cyan.opacity(0.95), .blue, .black],
                                center: .topLeading,
                                startRadius: 2,
                                endRadius: 28
                            )
                        )
                        .frame(width: 45, height: 45)
                        .shadow(color: .cyan.opacity(0.50), radius: 5)
                        .position(x: proxy.size.width * 0.42, y: proxy.size.height * 0.72)

                    ZStack {
                        Circle().fill(.white.opacity(0.85))
                        Circle()
                            .fill(.black.opacity(0.82))
                            .offset(x: -7)
                    }
                    .clipShape(Circle())
                    .frame(width: 18, height: 18)
                    .position(x: proxy.size.width * 0.52, y: proxy.size.height * 0.58)
                }
            }
            .frame(height: 202)

            HStack(spacing: 10) {
                ForEach(0..<7, id: \.self) { index in
                    MoonPhaseTwin(index: index)
                        .frame(width: index == 3 ? 27 : 22, height: index == 3 ? 27 : 22)
                        .overlay(
                            Circle().stroke(
                                index == 3 ? accent.opacity(0.85) : .clear,
                                lineWidth: 1
                            )
                            .padding(-4)
                        )
                }
            }
            Text("CURRENT PHASE")
                .font(.system(size: 7, weight: .semibold, design: .monospaced))
                .tracking(1)
                .foregroundStyle(.white.opacity(0.34))
                .padding(.top, 12)
            Spacer()
        }
    }
}

private struct MoonPhaseTwin: View {
    let index: Int

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                Circle().fill(.white.opacity(0.82))
                Circle()
                    .fill(.black.opacity(0.90))
                    .offset(x: CGFloat(index - 3) * proxy.size.width * 0.23)
            }
            .clipShape(Circle())
        }
    }
}

private struct ISSTwin: View {
    let accent: Color

    var body: some View {
        ZStack {
            Canvas { context, size in
                context.fill(
                    Path(CGRect(origin: .zero, size: size)),
                    with: .color(.black)
                )
                drawGraphiteContinents(context: &context, size: size, vertical: false)

                var equator = Path()
                equator.move(to: CGPoint(x: 0, y: size.height * 0.5))
                equator.addLine(to: CGPoint(x: size.width, y: size.height * 0.5))
                context.stroke(equator, with: .color(.white.opacity(0.34)), lineWidth: 0.8)

                var meridian = Path()
                meridian.move(to: CGPoint(x: size.width * 0.5, y: 0))
                meridian.addLine(to: CGPoint(x: size.width * 0.5, y: size.height))
                context.stroke(meridian, with: .color(.red.opacity(0.48)), lineWidth: 0.8)

                var trajectory = Path()
                trajectory.move(to: CGPoint(x: 0, y: size.height * 0.66))
                trajectory.addCurve(
                    to: CGPoint(x: size.width, y: size.height * 0.34),
                    control1: CGPoint(x: size.width * 0.31, y: size.height * 0.10),
                    control2: CGPoint(x: size.width * 0.67, y: size.height * 0.90)
                )
                context.stroke(
                    trajectory,
                    with: .linearGradient(
                        Gradient(stops: [
                            .init(
                                color: accent.opacity(0),
                                location: 0
                            ),
                            .init(
                                color: accent.opacity(0.18),
                                location: 0.28
                            ),
                            .init(
                                color: accent.opacity(0.58),
                                location: 0.68
                            ),
                            .init(
                                color: accent.opacity(0.92),
                                location: 1
                            ),
                        ]),
                        startPoint: CGPoint(
                            x: 0,
                            y: size.height * 0.5
                        ),
                        endPoint: CGPoint(
                            x: size.width,
                            y: size.height * 0.5
                        )
                    ),
                    lineWidth: 1.8
                )

                let shadow = Path { path in
                    path.move(to: CGPoint(x: size.width * 0.70, y: 0))
                    path.addCurve(
                        to: CGPoint(x: size.width * 0.55, y: size.height),
                        control1: CGPoint(x: size.width * 0.48, y: size.height * 0.28),
                        control2: CGPoint(x: size.width * 0.82, y: size.height * 0.72)
                    )
                    path.addLine(to: CGPoint(x: size.width, y: size.height))
                    path.addLine(to: CGPoint(x: size.width, y: 0))
                    path.closeSubpath()
                }
                context.fill(shadow, with: .color(.black.opacity(0.48)))
            }

            ZStack {
                Circle()
                    .stroke(
                        accent.opacity(0.94),
                        lineWidth: 2
                    )
                ISSGlyphTwin()
                    .frame(width: 32, height: 22)
            }
                .frame(width: 48, height: 48)
                .position(x: 218, y: 165)

            VStack {
                HStack(spacing: 32) {
                    if let nasa =
                        spaceResourceImage(
                            named: "iss_nasa"
                        ) {
                        Image(nsImage: nasa)
                            .resizable()
                            .interpolation(.high)
                            .scaledToFit()
                            .frame(
                                width: 54,
                                height: 45
                            )
                    }
                    if let roscosmos =
                        spaceResourceImage(
                            named: "iss_roscosmos"
                        ) {
                        Image(nsImage: roscosmos)
                            .resizable()
                            .interpolation(.high)
                            .scaledToFit()
                            .frame(
                                width: 45,
                                height: 45
                            )
                    }
                }
                .padding(.top, 20)

                Spacer()
                HStack(spacing: 34) {
                    PreviewMetric(symbol: "speedometer", value: "27,572 km/h")
                    PreviewMetric(symbol: "arrow.up.to.line", value: "417 km")
                }
                .padding(.bottom, 35)
            }
        }
    }
}

private func spaceResourceImage(
    named name: String
) -> NSImage? {
    guard
        let url = AppResources.bundle.url(
            forResource: name,
            withExtension: "png"
        )
    else {
        return nil
    }
    return NSImage(contentsOf: url)
}

private struct ISSGlyphTwin: View {
    var body: some View {
        Canvas { context, size in
            let centerX = size.width / 2
            let centerY = size.height / 2

            context.fill(
                Path(
                    CGRect(
                        x: centerX - 4,
                        y: centerY - 5,
                        width: 8,
                        height: 10
                    )
                ),
                with: .color(.white)
            )
            context.fill(
                Path(
                    CGRect(
                        x: centerX - 11,
                        y: centerY - 2,
                        width: 22,
                        height: 4
                    )
                ),
                with: .color(.white)
            )
            for x in [CGFloat(1), size.width - 9] {
                context.fill(
                    Path(CGRect(x: x, y: 2, width: 8, height: size.height - 4)),
                    with: .color(.white.opacity(0.92))
                )
                var divider = Path()
                divider.move(to: CGPoint(x: x, y: centerY))
                divider.addLine(to: CGPoint(x: x + 8, y: centerY))
                context.stroke(divider, with: .color(.black.opacity(0.65)), lineWidth: 1)
            }
        }
    }
}

private struct ClockScreenTwin: View {
    let worldMode: Bool
    let cityKeys: [String]
    let locationName: String

    var body: some View {
        ZStack {
            if worldMode {
                WorldClockTwin(
                    cityKeys: cityKeys,
                    currentLocationName: locationName
                )
            } else {
                GMTClockTwin(
                    locationName: locationName
                )
            }

            GMTBezelTwin(locationName: locationName)
                .allowsHitTesting(false)
        }
    }
}

private struct GMTBezelTwin: View {
    let locationName: String

    private var locationKey: String {
        RadarLocationPreset.preset(
            label: locationName,
            metar: ""
        )?.key ?? "SJC"
    }

    var body: some View {
        TimelineView(.periodic(from: .now, by: 60)) {
            context in
            Canvas { graphics, size in
                drawGMTBezel(
                    context: &graphics,
                    size: size,
                    center: CGPoint(
                        x: size.width / 2,
                        y: size.height / 2
                    ),
                    locationKey: locationKey,
                    date: context.date
                )
            }
        }
    }
}

private struct GMTClockTwin: View {
    let locationName: String

    private var currentKey: String {
        RadarLocationPreset.preset(
            label: locationName,
            metar: ""
        )?.key ?? "SJC"
    }

    private var secondaryKey: String {
        currentKey == "SJC" ? "MOW" : "SJC"
    }

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            GeometryReader { proxy in
                let size = proxy.size
                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                ZStack {
                    Text(dayString(context.date))
                        .font(.system(size: 22, weight: .regular, design: .rounded))
                        .foregroundStyle(.white)
                        .position(x: size.width * 0.72, y: size.height * 0.50)

                    VStack(spacing: 1) {
                        Text(
                            timeString(
                                context.date,
                                zone: clockTimeZoneIdentifier(
                                    for: currentKey
                                )
                            )
                        )
                            .font(.system(size: 34, weight: .regular, design: .rounded))
                            .foregroundStyle(.white)
                        Text(clockCityName(for: currentKey).uppercased())
                            .font(.system(size: 8, weight: .semibold, design: .monospaced))
                            .tracking(1)
                            .foregroundStyle(.white.opacity(0.56))
                    }
                    .position(x: size.width / 2, y: size.height * 0.64)

                    LinearGradient(
                        colors: [.clear, .white.opacity(0.45), .clear],
                        startPoint: .leading,
                        endPoint: .trailing
                    )
                    .frame(width: 116, height: 1)
                    .position(x: size.width / 2, y: size.height * 0.73)

                    VStack(spacing: 2) {
                        Text(
                            clockCityName(
                                for: secondaryKey
                            ).uppercased()
                        )
                            .font(.system(size: 7, weight: .medium))
                            .foregroundStyle(.white.opacity(0.72))
                        Text(
                            timeString(
                                context.date,
                                zone: clockTimeZoneIdentifier(
                                    for: secondaryKey
                                )
                            )
                        )
                            .font(.system(size: 15, weight: .regular, design: .rounded))
                            .foregroundStyle(.white)
                    }
                    .position(x: size.width / 2, y: size.height * 0.79)

                    Canvas { graphics, canvasSize in
                        drawClockHands(
                            context: &graphics,
                            center: center,
                            date: context.date,
                            currentKey: currentKey,
                            secondaryKey: secondaryKey
                        )
                    }
                }
            }
        }
    }
}

private struct WorldClockTwin: View {
    let cityKeys: [String]
    let currentLocationName: String

    private var currentKey: String {
        RadarLocationPreset.preset(
            label: currentLocationName,
            metar: ""
        )?.key ?? "SJC"
    }

    private var cities: [String] {
        var result = [currentKey]
        for key in cityKeys
        where !result.contains(key) && result.count < 5 {
            result.append(key)
        }
        return result
    }

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            ZStack {
                VStack(spacing: 20) {
                    Text("WORLD TIME")
                        .font(.system(size: 16, weight: .medium))
                        .foregroundStyle(.white.opacity(0.95))

                    VStack(spacing: 0) {
                        ForEach(Array(cities.enumerated()), id: \.element) { item in
                            let sunTimes = clockSunriseSunset(
                                for: item.element,
                                date: context.date
                            )
                            let localHour = clockLocalHour(
                                context.date,
                                key: item.element
                            )
                            let daylight =
                                localHour >= sunTimes.sunrise &&
                                localHour < sunTimes.sunset
                            HStack {
                                if item.offset == 0 {
                                    Triangle()
                                        .fill(.white)
                                        .frame(width: 7, height: 9)
                                } else {
                                    Color.clear.frame(width: 7, height: 9)
                                }
                                Circle()
                                    .fill(
                                        daylight
                                            ? Color(
                                                red: 0.75,
                                                green: 0.33,
                                                blue: 0.36
                                            )
                                            : Color(
                                                red: 0.33,
                                                green: 0.41,
                                                blue: 0.51
                                            )
                                    )
                                    .frame(width: 6, height: 6)
                                Text(item.element)
                                    .font(.system(size: 16, weight: .medium, design: .monospaced))
                                    .foregroundStyle(.white)
                                    .frame(width: 48, alignment: .leading)
                                Text(clockCityName(for: item.element))
                                    .font(.system(size: 11, weight: .regular))
                                    .foregroundStyle(.white.opacity(0.42))
                                    .lineLimit(1)
                                Spacer(minLength: 8)
                                Text(
                                    timeString(
                                        context.date,
                                        zone: clockTimeZoneIdentifier(
                                            for: item.element
                                        )
                                    )
                                )
                                    .font(.system(size: 17, weight: .regular, design: .rounded))
                                    .foregroundStyle(.white)
                            }
                            .frame(width: 258, height: 34)
                            if item.offset < cities.count - 1 {
                                LinearGradient(
                                    colors: [
                                        .clear,
                                        .white.opacity(0.13),
                                        .clear,
                                    ],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                )
                                .frame(width: 210, height: 1)
                            }
                        }
                    }
                }
                .offset(y: 10)
            }
        }
    }
}

private struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        Path { path in
            path.move(to: CGPoint(x: rect.minX, y: rect.midY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
            path.closeSubpath()
        }
    }
}

private func drawGraphiteRoads(context: inout GraphicsContext, size: CGSize) {
    let roads: [[CGPoint]] = [
        [
            CGPoint(x: size.width * 0.03, y: size.height * 0.34),
            CGPoint(x: size.width * 0.24, y: size.height * 0.29),
            CGPoint(x: size.width * 0.47, y: size.height * 0.46),
            CGPoint(x: size.width * 0.72, y: size.height * 0.40),
            CGPoint(x: size.width * 0.98, y: size.height * 0.52),
        ],
        [
            CGPoint(x: size.width * 0.18, y: size.height * 0.02),
            CGPoint(x: size.width * 0.29, y: size.height * 0.28),
            CGPoint(x: size.width * 0.52, y: size.height * 0.51),
            CGPoint(x: size.width * 0.60, y: size.height * 0.96),
        ],
        [
            CGPoint(x: size.width * 0.01, y: size.height * 0.74),
            CGPoint(x: size.width * 0.30, y: size.height * 0.65),
            CGPoint(x: size.width * 0.58, y: size.height * 0.73),
            CGPoint(x: size.width * 0.94, y: size.height * 0.67),
        ],
    ]
    for road in roads {
        var path = Path()
        guard let first = road.first else { continue }
        path.move(to: first)
        for point in road.dropFirst() {
            path.addCurve(
                to: point,
                control1: CGPoint(x: point.x - size.width * 0.08, y: point.y - size.height * 0.04),
                control2: CGPoint(x: point.x - size.width * 0.03, y: point.y + size.height * 0.03)
            )
        }
        context.stroke(path, with: .color(.white.opacity(0.14)), lineWidth: 1.1)
    }
}

private func drawGraphiteContinents(
    context: inout GraphicsContext,
    size: CGSize,
    vertical: Bool
) {
    let shapes: [[CGPoint]] = vertical
        ? [
            [
                CGPoint(x: size.width * 0.06, y: size.height * 0.12),
                CGPoint(x: size.width * 0.48, y: size.height * 0.03),
                CGPoint(x: size.width * 0.58, y: size.height * 0.32),
                CGPoint(x: size.width * 0.28, y: size.height * 0.43),
            ],
            [
                CGPoint(x: size.width * 0.37, y: size.height * 0.55),
                CGPoint(x: size.width * 0.77, y: size.height * 0.46),
                CGPoint(x: size.width * 0.91, y: size.height * 0.83),
                CGPoint(x: size.width * 0.53, y: size.height * 0.97),
            ],
        ]
        : [
            [
                CGPoint(x: size.width * 0.02, y: size.height * 0.23),
                CGPoint(x: size.width * 0.30, y: size.height * 0.17),
                CGPoint(x: size.width * 0.40, y: size.height * 0.45),
                CGPoint(x: size.width * 0.22, y: size.height * 0.74),
                CGPoint(x: size.width * 0.05, y: size.height * 0.59),
            ],
            [
                CGPoint(x: size.width * 0.64, y: size.height * 0.16),
                CGPoint(x: size.width * 0.96, y: size.height * 0.28),
                CGPoint(x: size.width * 0.88, y: size.height * 0.68),
                CGPoint(x: size.width * 0.62, y: size.height * 0.62),
            ],
        ]

    for shape in shapes {
        var path = Path()
        guard let first = shape.first else { continue }
        path.move(to: first)
        shape.dropFirst().forEach { path.addLine(to: $0) }
        path.closeSubpath()
        context.fill(path, with: .color(.white.opacity(0.045)))
        context.stroke(path, with: .color(.white.opacity(0.24)), lineWidth: 1.2)
    }
}

private func drawGMTBezel(
    context: inout GraphicsContext,
    size: CGSize,
    center: CGPoint,
    locationKey: String,
    date: Date
) {
    let outerRadius = size.width * 0.455
    let width = size.width * 0.094
    let radius = outerRadius - width / 2
    let rect = CGRect(
        x: center.x - radius,
        y: center.y - radius,
        width: radius * 2,
        height: radius * 2
    )
    context.stroke(
        Path(ellipseIn: rect),
        with: .color(Color(red: 0.04, green: 0.15, blue: 0.30)),
        lineWidth: width
    )

    let sunTimes = clockSunriseSunset(
        for: locationKey,
        date: date
    )
    var daylight = Path()
    daylight.addArc(
        center: center,
        radius: radius,
        startAngle: .degrees(
            sunTimes.sunrise * 15 - 90
        ),
        endAngle: .degrees(
            sunTimes.sunset * 15 - 90
        ),
        clockwise: false
    )
    context.stroke(
        daylight,
        with: .color(Color(red: 0.65, green: 0.05, blue: 0.08)),
        lineWidth: width
    )

    for hour in stride(from: 2, to: 24, by: 2) {
        let angle = Double(hour) / 24 * 2 * Double.pi - Double.pi / 2
        let point = CGPoint(
            x: center.x + cos(angle) * radius,
            y: center.y + sin(angle) * radius
        )
        context.drawLayer { layer in
            layer.translateBy(
                x: point.x,
                y: point.y
            )
            layer.rotate(
                by: .radians(angle + .pi / 2)
            )
            layer.draw(
                Text(String(format: "%02d", hour))
                    .font(
                        .system(
                            size: 28,
                            weight: .regular,
                            design: .rounded
                        )
                    )
                    .foregroundStyle(.white),
                at: .zero
            )
        }
    }

    let dotRadius =
        size.width * 180 / 466
    for hour in stride(from: 1, to: 24, by: 2) {
        let angle =
            Double(hour) / 24 * 2 * Double.pi -
            Double.pi / 2
        let point = CGPoint(
            x: center.x +
                cos(angle) *
                dotRadius,
            y: center.y +
                sin(angle) *
                dotRadius
        )
        context.fill(
            Path(
                ellipseIn: CGRect(
                    x: point.x - 4,
                    y: point.y - 4,
                    width: 8,
                    height: 8
                )
            ),
            with: .color(.white)
        )
    }

    var marker = Path()
    let markerHalfWidth = size.width * 32 / 466
    let markerInset = size.width * 5 / 466
    let markerCurveDepth = size.width * 2.5 / 466
    let markerTop = center.y - outerRadius + markerInset
    marker.move(
        to: CGPoint(
            x: center.x - markerHalfWidth,
            y: markerTop + markerCurveDepth
        )
    )
    marker.addQuadCurve(
        to: CGPoint(
            x: center.x + markerHalfWidth,
            y: markerTop + markerCurveDepth
        ),
        control: CGPoint(
            x: center.x,
            y: markerTop - markerCurveDepth
        )
    )
    marker.addLine(
        to: CGPoint(
            x: center.x,
            y: markerTop + size.width * 32 / 466
        )
    )
    marker.closeSubpath()
    context.fill(marker, with: .color(.white))

    context.stroke(
        Path(
            ellipseIn: CGRect(
                x: center.x - outerRadius - 1,
                y: center.y - outerRadius - 1,
                width: outerRadius * 2 + 2,
                height: outerRadius * 2 + 2
            )
        ),
        with: .color(.white.opacity(0.30)),
        lineWidth: 1.5
    )
    context.stroke(
        Path(
            ellipseIn: CGRect(
                x: center.x - size.width * 0.341,
                y: center.y - size.width * 0.341,
                width: size.width * 0.682,
                height: size.width * 0.682
            )
        ),
        with: .color(.white.opacity(0.27)),
        lineWidth: 1.5
    )
}

private func drawClockHands(
    context: inout GraphicsContext,
    center: CGPoint,
    date: Date,
    currentKey: String,
    secondaryKey: String
) {
    var localCalendar = Calendar(identifier: .gregorian)
    localCalendar.timeZone =
        TimeZone(
            identifier:
                clockTimeZoneIdentifier(
                    for: currentKey
                )
        ) ?? .current
    var secondaryCalendar =
        Calendar(identifier: .gregorian)
    secondaryCalendar.timeZone =
        TimeZone(
            identifier:
                clockTimeZoneIdentifier(
                    for: secondaryKey
                )
        ) ?? .gmt
    let hour =
        Double(
            localCalendar.component(
                .hour,
                from: date
            ) % 12
        )
    let minute =
        Double(
            localCalendar.component(
                .minute,
                from: date
            )
        )
    let secondaryHour =
        Double(
            secondaryCalendar.component(
                .hour,
                from: date
            )
        )
    let secondaryMinute =
        Double(
            secondaryCalendar.component(
                .minute,
                from: date
            )
        )
    let hourAngle =
        (hour + minute / 60) /
        12 * 2 * Double.pi -
        Double.pi / 2
    let minuteAngle =
        minute /
        60 * 2 * Double.pi -
        Double.pi / 2
    let gmtAngle =
        (secondaryHour +
         secondaryMinute / 60) /
        24 * 2 * Double.pi -
        Double.pi / 2
    let metal =
        Color(
            red: 0.616,
            green: 0.635,
            blue: 0.624
        )
    let luminous =
        Color(
            red: 0.969,
            green: 0.973,
            blue: 0.965
        )
    let red =
        Color(
            red: 0.89,
            green: 0.13,
            blue: 0.18
        )

    func point(
        angle: Double,
        distance: CGFloat,
        perpendicular: CGFloat = 0
    ) -> CGPoint {
        CGPoint(
            x:
                center.x +
                cos(angle) * distance -
                sin(angle) * perpendicular,
            y:
                center.y +
                sin(angle) * distance +
                cos(angle) * perpendicular
        )
    }

    func stroke(
        angle: Double,
        start: CGFloat,
        end: CGFloat,
        color: Color,
        width: CGFloat
    ) {
        var path = Path()
        path.move(
            to: point(
                angle: angle,
                distance: start
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: end
            )
        )
        context.stroke(path, with: .color(color), lineWidth: width)
    }

    func triangle(
        angle: Double,
        tip: CGFloat,
        base: CGFloat,
        halfWidth: CGFloat,
        color: Color
    ) {
        var path = Path()
        path.move(
            to: point(
                angle: angle,
                distance: tip
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: base,
                perpendicular: halfWidth
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: base,
                perpendicular: -halfWidth
            )
        )
        path.closeSubpath()
        context.fill(
            path,
            with: .color(color)
        )
    }

    func handQuad(
        angle: Double,
        start: CGFloat,
        end: CGFloat,
        halfWidth: CGFloat,
        color: Color
    ) {
        var path = Path()
        path.move(
            to: point(
                angle: angle,
                distance: start,
                perpendicular: halfWidth
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: end,
                perpendicular: halfWidth
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: end,
                perpendicular: -halfWidth
            )
        )
        path.addLine(
            to: point(
                angle: angle,
                distance: start,
                perpendicular: -halfWidth
            )
        )
        path.closeSubpath()
        context.fill(
            path,
            with: .color(color)
        )
    }

    stroke(
        angle: gmtAngle,
        start: -5,
        end: 128,
        color: red,
        width: 3
    )
    triangle(
        angle: gmtAngle,
        tip: 154,
        base: 126,
        halfWidth: 15,
        color: red
    )
    triangle(
        angle: gmtAngle,
        tip: 146,
        base: 132,
        halfWidth: 8,
        color: .white
    )

    stroke(
        angle: hourAngle,
        start: -4,
        end: 42,
        color: metal,
        width: 15
    )
    stroke(
        angle: hourAngle,
        start: -4,
        end: 40,
        color: luminous,
        width: 7
    )
    let medallion =
        point(
            angle: hourAngle,
            distance: 58
        )
    context.fill(
        Path(
            ellipseIn: CGRect(
                x: medallion.x - 16.5,
                y: medallion.y - 16.5,
                width: 33,
                height: 33
            )
        ),
        with: .color(metal)
    )
    context.fill(
        Path(
            ellipseIn: CGRect(
                x: medallion.x - 12,
                y: medallion.y - 12,
                width: 24,
                height: 24
            )
        ),
        with: .color(luminous)
    )
    for endpoint in [
        point(
            angle: hourAngle,
            distance: 72
        ),
        point(
            angle: hourAngle,
            distance: 51,
            perpendicular: 9
        ),
        point(
            angle: hourAngle,
            distance: 51,
            perpendicular: -9
        ),
    ] {
        var spoke = Path()
        spoke.move(to: medallion)
        spoke.addLine(to: endpoint)
        context.stroke(
            spoke,
            with: .color(metal),
            lineWidth: 5
        )
    }
    triangle(
        angle: hourAngle,
        tip: 101,
        base: 69,
        halfWidth: 8.5,
        color: metal
    )
    triangle(
        angle: hourAngle,
        tip: 94,
        base: 72,
        halfWidth: 4.8,
        color: luminous
    )
    context.fill(
        Path(
            ellipseIn: CGRect(
                x: center.x - 12,
                y: center.y - 12,
                width: 24,
                height: 24
            )
        ),
        with: .color(metal)
    )
    context.fill(
        Path(
            ellipseIn: CGRect(
                x: center.x - 8.5,
                y: center.y - 8.5,
                width: 17,
                height: 17
            )
        ),
        with: .color(luminous)
    )

    handQuad(
        angle: minuteAngle,
        start: -4,
        end: 90,
        halfWidth: 7.5,
        color: metal,
    )
    triangle(
        angle: minuteAngle,
        tip: 117,
        base: 90,
        halfWidth: 7.5,
        color: metal
    )
    handQuad(
        angle: minuteAngle,
        start: -4,
        end: 84,
        halfWidth: 3,
        color: luminous,
    )

    context.fill(
        Path(
            ellipseIn: CGRect(
                x: center.x - 9,
                y: center.y - 9,
                width: 18,
                height: 18
            )
        ),
        with: .color(metal)
    )
    context.fill(
        Path(
            ellipseIn: CGRect(
                x: center.x - 5,
                y: center.y - 5,
                width: 10,
                height: 10
            )
        ),
        with: .color(luminous)
    )
}

private func cardinal(_ degrees: Int) -> String {
    let directions = [
        "N", "NNE", "NE", "ENE",
        "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW",
        "W", "WNW", "NW", "NNW",
    ]
    return directions[((degrees + 11) / 22) % 16]
}

private func timeString(_ date: Date, zone: String) -> String {
    let formatter = DateFormatter()
    formatter.dateFormat = "HH:mm"
    formatter.timeZone = TimeZone(identifier: zone)
    return formatter.string(from: date)
}

private func clockTimeZoneIdentifier(for key: String) -> String {
    switch key.uppercased() {
    case "SJC", "SFO", "SAN":
        "America/Los_Angeles"
    case "JFK":
        "America/New_York"
    case "DUS":
        "Europe/Berlin"
    case "MOW", "LED":
        "Europe/Moscow"
    case "ICN":
        "Asia/Seoul"
    default:
        "UTC"
    }
}

private func clockLocalHour(
    _ date: Date,
    key: String
) -> Double {
    var calendar =
        Calendar(identifier: .gregorian)
    calendar.timeZone =
        TimeZone(
            identifier:
                clockTimeZoneIdentifier(
                    for: key
                )
        ) ?? .gmt
    let components =
        calendar.dateComponents(
            [.hour, .minute],
            from: date
        )
    return
        Double(components.hour ?? 0) +
        Double(components.minute ?? 0) /
            60.0
}

private func clockCityName(for key: String) -> String {
    switch key.uppercased() {
    case "SJC": "San Jose"
    case "SFO": "San Francisco"
    case "SAN": "San Diego"
    case "JFK": "New York"
    case "DUS": "Düsseldorf"
    case "MOW": "Moscow"
    case "LED": "Saint Petersburg"
    case "ICN": "Seoul"
    default: key
    }
}

private func clockSunriseSunset(
    for key: String,
    date: Date
) -> (sunrise: Double, sunset: Double) {
    guard
        let location = RadarLocationPreset.preset(
            for: key
        ),
        let timeZone = TimeZone(
            identifier: clockTimeZoneIdentifier(for: key)
        )
    else {
        return (6, 18)
    }

    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = timeZone
    let day = Double(
        calendar.ordinality(
            of: .day,
            in: .year,
            for: date
        ) ?? 1
    )
    let latitude =
        location.coordinate.latitude
    let longitude =
        location.coordinate.longitude
    let longitudeHour = longitude / 15
    let utcOffset =
        Double(
            timeZone.secondsFromGMT(for: date)
        ) / 3_600

    func normalizedDegrees(_ value: Double) -> Double {
        let result = value.truncatingRemainder(
            dividingBy: 360
        )
        return result < 0 ? result + 360 : result
    }

    func normalizedHours(_ value: Double) -> Double {
        let result = value.truncatingRemainder(
            dividingBy: 24
        )
        return result < 0 ? result + 24 : result
    }

    func eventTime(sunrise: Bool) -> Double? {
        let approximate =
            day +
            ((sunrise ? 6 : 18) - longitudeHour) /
                24
        let meanAnomaly =
            0.9856 * approximate - 3.289
        let radians = Double.pi / 180
        let trueLongitude = normalizedDegrees(
            meanAnomaly +
                1.916 * sin(meanAnomaly * radians) +
                0.020 * sin(2 * meanAnomaly * radians) +
                282.634
        )
        var rightAscension = normalizedDegrees(
            atan(
                0.91764 *
                    tan(trueLongitude * radians)
            ) / radians
        )
        rightAscension +=
            floor(trueLongitude / 90) * 90 -
            floor(rightAscension / 90) * 90
        rightAscension /= 15

        let sineDeclination =
            0.39782 *
            sin(trueLongitude * radians)
        let cosineDeclination =
            cos(asin(sineDeclination))
        let cosineHour =
            (cos(90.833 * radians) -
                sineDeclination *
                    sin(latitude * radians)) /
            (cosineDeclination *
                cos(latitude * radians))
        guard (-1...1).contains(cosineHour) else {
            return nil
        }

        var localHour =
            acos(cosineHour) / radians
        if sunrise {
            localHour = 360 - localHour
        }
        localHour /= 15
        let localMeanTime =
            localHour +
            rightAscension -
            0.06571 * approximate -
            6.622
        return normalizedHours(
            normalizedHours(
                localMeanTime - longitudeHour
            ) + utcOffset
        )
    }

    guard
        let sunrise = eventTime(sunrise: true),
        let sunset = eventTime(sunrise: false)
    else {
        return (6, 18)
    }
    return (sunrise, sunset)
}

private func dayString(_ date: Date) -> String {
    let formatter = DateFormatter()
    formatter.dateFormat = "dd"
    return formatter.string(from: date)
}

private struct XDRRadarPreview: View {
    let flight: TrackedFlight

    var body: some View {
        Canvas { context, size in
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            drawRadarGrid(context: &context, size: size, center: center, accent: .cyan)
            drawWeatherWisps(context: &context, size: size)
            drawPlaneGlyph(context: &context, center: CGPoint(x: center.x + 68, y: center.y - 56), angle: -0.72, color: .white)
            drawPlaneGlyph(context: &context, center: CGPoint(x: center.x - 92, y: center.y + 46), angle: 0.18, color: .orange)
            drawPlaneGlyph(context: &context, center: CGPoint(x: center.x + 12, y: center.y + 112), angle: -1.1, color: .cyan)
        }
        .overlay(alignment: .bottom) {
            glassLabel(title: "LOCAL RADAR", value: "5 targets · KSJC VFR", tint: .cyan)
                .padding(.bottom, 68)
        }
    }
}

private struct CartographicGlassPreview: View {
    let flight: TrackedFlight
    let mapImage: NSImage?
    let routeProgress: Double

    var body: some View {
        ZStack {
            if let mapImage {
                Image(nsImage: mapImage)
                    .resizable()
                    .scaledToFill()
                    .opacity(0.82)
            } else {
                Canvas { context, size in
                    drawMapLikeLayer(context: &context, size: size)
                }
            }

            LinearGradient(colors: [.black.opacity(0.24), .clear, .black.opacity(0.34)], startPoint: .top, endPoint: .bottom)

            if mapImage == nil {
                Canvas { context, size in
                    let points = fallbackRoutePoints(size: size, progress: routeProgress)
                    let path = Path { p in
                        p.move(to: CGPoint(x: size.width * 0.34, y: size.height * 0.78))
                        p.addCurve(
                            to: CGPoint(x: size.width * 0.68, y: size.height * 0.22),
                            control1: CGPoint(x: size.width * 0.24, y: size.height * 0.46),
                            control2: CGPoint(x: size.width * 0.56, y: size.height * 0.28)
                        )
                    }
                    context.stroke(path, with: .color(.cyan.opacity(0.32)), lineWidth: 12)
                    context.stroke(path, with: .color(.white.opacity(0.95)), lineWidth: 3)
                    drawPlaneGlyph(context: &context, center: points.plane, angle: points.angle, color: .white)
                }
            }

            VStack {
                glassLabel(title: flight.marketingNumber, value: "\(flight.origin.code) → \(flight.destination.code) · \(flight.operationalCallsign)", tint: .mint)
                    .padding(.top, 76)
                Spacer()
            }
        }
    }
}

private func fallbackRoutePoints(size: CGSize, progress: Double) -> (plane: CGPoint, angle: Double) {
    let p = min(0.98, max(0.02, progress))
    let start = CGPoint(x: size.width * 0.50, y: size.height * 0.84)
    let end = CGPoint(x: size.width * 0.50, y: size.height * 0.16)
    let c1 = CGPoint(x: size.width * 0.34, y: size.height * 0.58)
    let c2 = CGPoint(x: size.width * 0.64, y: size.height * 0.38)

    func bezier(_ t: Double) -> CGPoint {
        let mt = 1 - t
        let x = mt * mt * mt * start.x
            + 3 * mt * mt * t * c1.x
            + 3 * mt * t * t * c2.x
            + t * t * t * end.x
        let y = mt * mt * mt * start.y
            + 3 * mt * mt * t * c1.y
            + 3 * mt * t * t * c2.y
            + t * t * t * end.y
        return CGPoint(x: x, y: y)
    }

    let point = bezier(p)
    let before = bezier(max(0, p - 0.01))
    let after = bezier(min(1, p + 0.01))
    return (point, atan2(after.y - before.y, after.x - before.x))
}

private struct GMTChronographPreview: View {
    let flight: TrackedFlight

    var body: some View {
        Canvas { context, size in
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            drawWatchDial(context: &context, size: size, center: center)
        }
        .overlay {
            VStack(spacing: 4) {
                Text("18:42")
                    .font(.system(size: 38, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text("San Jose")
                    .font(.system(size: 12, weight: .medium, design: .monospaced))
                    .foregroundStyle(.cyan)
            }
        }
        .overlay(alignment: .top) {
            glassLabel(title: "GERMANY", value: "+9h", tint: .white)
                .padding(.top, 78)
        }
        .overlay(alignment: .trailing) {
            glassLabel(title: "NYC", value: "+3h", tint: .cyan)
                .frame(width: 126)
                .padding(.trailing, 42)
        }
        .overlay(alignment: .bottom) {
            glassLabel(title: "MOSCOW", value: "+10h", tint: .orange)
                .padding(.bottom, 74)
        }
    }
}

private func glassLabel(title: String, value: String, tint: Color) -> some View {
    HStack(spacing: 10) {
        Text(title)
            .font(.system(size: 12, weight: .semibold, design: .monospaced))
            .foregroundStyle(tint.opacity(0.92))
        Text(value)
            .font(.system(size: 12, weight: .medium, design: .monospaced))
            .foregroundStyle(.white.opacity(0.82))
    }
    .padding(.horizontal, 18)
    .padding(.vertical, 12)
    .background(.black.opacity(0.42), in: Capsule())
    .overlay(Capsule().stroke(.white.opacity(0.16), lineWidth: 1))
}

private func drawRadarGrid(context: inout GraphicsContext, size: CGSize, center: CGPoint, accent: Color) {
    for radius in stride(from: 56.0, through: 190.0, by: 44.0) {
        let rect = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)
        context.stroke(Path(ellipseIn: rect), with: .color(accent.opacity(0.16)), lineWidth: 1.2)
    }

    for angle in stride(from: 0.0, to: 360.0, by: 45.0) {
        let radians = angle * .pi / 180
        var path = Path()
        path.move(to: center)
        path.addLine(to: CGPoint(x: center.x + cos(radians) * 190, y: center.y + sin(radians) * 190))
        context.stroke(path, with: .color(.cyan.opacity(0.11)), lineWidth: 1)
    }

    var sweep = Path()
    sweep.move(to: center)
    sweep.addLine(to: CGPoint(x: center.x + 142, y: center.y - 104))
    context.stroke(sweep, with: .color(.cyan.opacity(0.72)), lineWidth: 4)
}

private func drawWeatherWisps(context: inout GraphicsContext, size: CGSize) {
    let bands: [(CGPoint, CGPoint, Color, CGFloat)] = [
        (CGPoint(x: 70, y: 170), CGPoint(x: 340, y: 136), .green, 12),
        (CGPoint(x: 96, y: 306), CGPoint(x: 380, y: 330), .purple, 16),
        (CGPoint(x: 160, y: 112), CGPoint(x: 360, y: 210), .cyan, 8)
    ]

    for band in bands {
        var path = Path()
        path.move(to: band.0)
        path.addCurve(
            to: band.1,
            control1: CGPoint(x: band.0.x + 90, y: band.0.y - 54),
            control2: CGPoint(x: band.1.x - 110, y: band.1.y + 42)
        )
        context.stroke(path, with: .color(band.2.opacity(0.22)), lineWidth: band.3)
    }
}

private func drawMapLikeLayer(context: inout GraphicsContext, size: CGSize) {
    context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Color(red: 0.018, green: 0.038, blue: 0.055)))
    let land = Color(red: 0.08, green: 0.15, blue: 0.17)
    let coast = Color(red: 0.30, green: 0.48, blue: 0.54)

    let shapes: [[CGPoint]] = [
        [.init(x: 86, y: 92), .init(x: 168, y: 86), .init(x: 222, y: 138), .init(x: 196, y: 210), .init(x: 112, y: 206)],
        [.init(x: 302, y: 92), .init(x: 388, y: 126), .init(x: 376, y: 220), .init(x: 306, y: 210), .init(x: 274, y: 144)],
        [.init(x: 290, y: 274), .init(x: 360, y: 318), .init(x: 340, y: 398), .init(x: 272, y: 372)]
    ]

    for shape in shapes {
        var path = Path()
        guard let first = shape.first else { continue }
        path.move(to: first)
        shape.dropFirst().forEach { path.addLine(to: $0) }
        path.closeSubpath()
        context.fill(path, with: .color(land.opacity(0.78)))
        context.stroke(path, with: .color(coast.opacity(0.55)), lineWidth: 2)
    }
}

private func drawPlaneGlyph(context: inout GraphicsContext, center: CGPoint, angle: Double, color: Color) {
    let forward = CGVector(dx: cos(angle), dy: sin(angle))
    let wing = CGVector(dx: -sin(angle), dy: cos(angle))

    func point(_ f: CGFloat, _ w: CGFloat) -> CGPoint {
        CGPoint(x: center.x + forward.dx * f + wing.dx * w, y: center.y + forward.dy * f + wing.dy * w)
    }

    var body = Path()
    body.move(to: point(-16, 0))
    body.addLine(to: point(18, 0))
    context.stroke(body, with: .color(color.opacity(0.96)), lineWidth: 5)

    var wings = Path()
    wings.move(to: point(0, -17))
    wings.addLine(to: point(3, 17))
    context.stroke(wings, with: .color(color.opacity(0.88)), lineWidth: 5)

    var tail = Path()
    tail.move(to: point(-12, -7))
    tail.addLine(to: point(-10, 7))
    context.stroke(tail, with: .color(color.opacity(0.82)), lineWidth: 3)
}

private func drawWatchDial(context: inout GraphicsContext, size: CGSize, center: CGPoint) {
    let outer = CGRect(x: 53, y: 53, width: size.width - 106, height: size.height - 106)
    context.stroke(Path(ellipseIn: outer), with: .color(.white.opacity(0.26)), lineWidth: 6)

    for hour in 0..<24 {
        let angle = Double(hour) / 24 * 2 * .pi - .pi / 2
        let outerRadius: CGFloat = 178
        let innerRadius: CGFloat = hour.isMultiple(of: 6) ? 148 : 163
        var tick = Path()
        tick.move(to: CGPoint(x: center.x + cos(angle) * innerRadius, y: center.y + sin(angle) * innerRadius))
        tick.addLine(to: CGPoint(x: center.x + cos(angle) * outerRadius, y: center.y + sin(angle) * outerRadius))
        context.stroke(tick, with: .color(hour.isMultiple(of: 6) ? .white.opacity(0.75) : .white.opacity(0.25)), lineWidth: hour.isMultiple(of: 6) ? 3 : 1)
    }

    var gmtHand = Path()
    gmtHand.move(to: center)
    gmtHand.addLine(to: CGPoint(x: center.x + 44, y: center.y - 118))
    context.stroke(gmtHand, with: .color(.orange.opacity(0.95)), lineWidth: 4)

    var localHand = Path()
    localHand.move(to: center)
    localHand.addLine(to: CGPoint(x: center.x - 86, y: center.y + 52))
    context.stroke(localHand, with: .color(.white.opacity(0.9)), lineWidth: 5)

    context.fill(Path(ellipseIn: CGRect(x: center.x - 7, y: center.y - 7, width: 14, height: 14)), with: .color(.white))
}
