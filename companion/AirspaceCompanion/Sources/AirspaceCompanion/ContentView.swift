// AirESP32ace
// Created by VM, August 2026

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var model: CompanionModel

    private let guideDeviceManagementStep: DeviceManagementGuideStep?
    private let guideMode: Bool

    @State private var page: CompanionPage = .radar
    @State private var utilitySheet: CompanionUtility?
    @State private var queuedMapRefresh = false
    @State private var selectedLibrary: LibraryKind = .aircraft
    @State private var previewPageIndex = 0
    @State private var deviceManagementPresented = false

    init(
        model: CompanionModel,
        guidePage: String? = nil,
        guidePreviewPageIndex: Int = 0,
        guideDeviceManagementStep: DeviceManagementGuideStep? = nil
    ) {
        self.model = model
        self.guideDeviceManagementStep = guideDeviceManagementStep
        guideMode = guidePage != nil || guideDeviceManagementStep != nil
        _page = State(
            initialValue: CompanionPage(rawValue: guidePage ?? "") ?? .radar
        )
        _previewPageIndex = State(initialValue: guidePreviewPageIndex)
        _deviceManagementPresented = State(
            initialValue: guideDeviceManagementStep != nil
        )
    }

    private var isBusy: Bool {
        model.configPushState.isPushing ||
            model.pushState.isPushing ||
            model.renderState.isRendering
    }

    var body: some View {
        ZStack {
            HStack(spacing: 0) {
                CompanionRail(
                    page: $page,
                    openUtility: { utilitySheet = $0 },
                    openDeviceManagement: {
                        withAnimation(.easeInOut(duration: 0.22)) {
                            deviceManagementPresented = true
                        }
                    }
                )
                    .frame(width: 214)

                VStack(spacing: 0) {
                    CompanionTopBar(
                        page: page,
                        queuedMapRefresh: queuedMapRefresh,
                        isBusy: isBusy,
                        statusMessage: operationStatusMessage,
                        progress: model.syncProgress,
                        sync: syncDevice
                    )

                    Divider().overlay(.white.opacity(0.08))

                    HStack(spacing: 0) {
                        pageContent
                            .frame(maxWidth: .infinity, maxHeight: .infinity)

                        Divider().overlay(.white.opacity(0.08))

                        DevicePreviewColumn(
                            model: model,
                            section: page,
                            selection: $previewPageIndex
                        )
                        .frame(width: 420)
                    }
                }
            }
            .saturation(deviceManagementPresented ? 0.38 : 1)
            .opacity(deviceManagementPresented ? 0.24 : 1)
            .blur(radius: deviceManagementPresented ? 4 : 0)
            .allowsHitTesting(!deviceManagementPresented)

            if deviceManagementPresented {
                Color.black.opacity(0.56)
                    .ignoresSafeArea()
                    .transition(.opacity)

                DeviceManagementView(
                    model: model,
                    guideStep: guideDeviceManagementStep
                ) {
                    withAnimation(.easeInOut(duration: 0.22)) {
                        deviceManagementPresented = false
                    }
                }
                .transition(
                    .opacity.combined(with: .scale(scale: 0.975))
                )
            }
        }
        .frame(minWidth: 1160, minHeight: 740)
        .background(RadarTheme.background)
        .animation(
            .easeInOut(duration: 0.22),
            value: deviceManagementPresented
        )
        .onChange(of: page) {
            previewPageIndex = 0
        }
        .sheet(item: $utilitySheet) { utility in
            CompanionUtilitySheet(
                utility: utility,
                model: model,
                selectedLibrary: $selectedLibrary
            )
        }
        .task {
            guard !guideMode else { return }
            if model.trackedFlight.marketingNumber == TrackedFlight.airPremiaYP111.marketingNumber {
                _ = await model.refreshTrackedFlight()
            }
            if model.nextLaunch.id ==
                TrackedLaunch.preview.id {
                _ = await model.refreshNextLaunch()
            }
            await model.refreshDeviceStatus(announce: false)
        }
    }

    private var operationStatusMessage: String? {
        if model.pushState.isPushing {
            return model.pushState.message
        }
        if model.configPushState.isPushing {
            return model.configPushState.message
        }
        if model.renderState.isRendering {
            return model.renderState.message
        }
        return nil
    }

    @ViewBuilder
    private var pageContent: some View {
        switch page {
        case .radar:
            DeviceWorkspace(
                model: model,
                queuedMapRefresh: $queuedMapRefresh
            )
        case .tracker:
            FlightWorkspace(model: model)
        case .weather:
            ScreenFamilyWorkspace(
                detail: "Weather follows the city selected on Radar.",
                pages: [
                    ("cloud.sun.fill", "Current"),
                    ("calendar", "Forecast"),
                    ("cloud.rain.fill", "Radar"),
                ]
            )
        case .clock:
            ClockWorkspace(model: model)
        case .space:
            ScreenFamilyWorkspace(
                detail: "Live astronomical positions, eclipses, and the ISS ground track.",
                pages: [
                    ("sun.max.fill", "Solar system"),
                    ("circle.lefthalf.filled", "Solar eclipse"),
                    ("moonphase.full.moon", "Lunar phase"),
                    ("globe.americas.fill", "ISS"),
                ]
            )
        case .launches:
            LaunchWorkspace(model: model)
        case .dock:
            DockWorkspace(
                model: model,
                previewSelection: $previewPageIndex
            )
        case .settings:
            SettingsWorkspace(model: model)
        }
    }

    private func syncDevice() {
        Task {
            // A tracked-flight package is independent of general settings.
            // Sending config first can reconnect/reboot the ESP32 and abort
            // the map and flight-state transfer that the user requested.
            if page == .tracker {
                await model.syncTrackedFlightsToDevice()
                return
            }

            guard await model.pushPortableConfigToDevice() else {
                return
            }
            if model.configPushRequiresReconnect {
                model.pushState = .pushed("Settings saved. Sync again after the ESP32 reconnects.")
                return
            }

            if page == .radar && queuedMapRefresh {
                await model.pushLocalRadarMapToDevice()
                if !model.pushState.isFailure {
                    queuedMapRefresh = false
                }
            } else if page == .launches {
                await model.syncNextLaunchToDevice()
            } else if page != .dock && page != .settings {
                await model.syncScreenToDevice(
                    style: page.instrumentStyle
                )
            }
        }
    }
}

private enum CompanionPage: String, CaseIterable, Identifiable {
    case radar
    case tracker
    case weather
    case clock
    case space
    case launches
    case dock
    case settings

    var id: String { rawValue }

    var title: String {
        switch self {
        case .radar: "Radar"
        case .tracker: "Tracker"
        case .weather: "Weather"
        case .clock: "Clock"
        case .space: "Space"
        case .launches: "Space Launches"
        case .dock: "Dock"
        case .settings: "Settings"
        }
    }

    var subtitle: String {
        switch self {
        case .radar: "Nearby aircraft and location"
        case .tracker: "Scheduled family flights"
        case .weather: "Conditions, forecast, and radar"
        case .clock: "Local GMT dial and world time"
        case .space: "Solar system, eclipses, Moon, and ISS"
        case .launches: "Crewed missions and Starship"
        case .dock: "Automatic screen rotation"
        case .settings: "Network, display, and navigation"
        }
    }

    var symbol: String {
        switch self {
        case .radar: "scope"
        case .tracker: "airplane"
        case .weather: "cloud.rain.fill"
        case .clock: "clock.fill"
        case .space: "sparkles"
        case .launches: "rocket.fill"
        case .dock: "circle"
        case .settings: "gearshape.fill"
        }
    }

    var color: Color {
        switch self {
        case .radar: RadarTheme.green
        case .tracker: Color(red: 1.0, green: 0.78, blue: 0.18)
        case .weather: Color(red: 0.25, green: 0.72, blue: 1.0)
        case .clock: Color(red: 0.94, green: 0.27, blue: 0.31)
        case .space: Color(red: 0.62, green: 0.47, blue: 1.0)
        case .launches: Color(red: 1.0, green: 0.47, blue: 0.19)
        case .dock: Color(red: 0.64, green: 0.67, blue: 0.70)
        case .settings: Color(red: 0.70, green: 0.73, blue: 0.76)
        }
    }

    var instrumentStyle: InstrumentStyle {
        switch self {
        case .radar: .xdrRadar
        case .tracker: .cartographicGlass
        case .weather: .weather
        case .clock: .gmtChronograph
        case .space: .space
        case .launches: .launch
        case .dock: .xdrRadar
        case .settings: .xdrRadar
        }
    }

    var syncTitle: String {
        "Sync"
    }
}

private extension SharedConfiguration.DockScreen {
    var previewPage: DevicePreviewPage {
        switch self {
        case .radar: .radarMain
        case .tracker: .flightGlobe
        case .weather: .weatherCurrent
        case .clock: .clockGMT
        case .solar: .spaceSolar
        case .eclipse: .spaceEclipse
        case .moon: .spaceMoon
        case .iss: .spaceISS
        case .launchNasa, .launchRoscosmos, .launchSpacex:
            .launchMain
        case .launchNasaMap, .launchRoscosmosMap, .launchSpacexMap:
            .launchMap
        case .trackerFrame: .flightFrame
        case .trackerDetail: .flightDetail
        case .weatherForecast: .weatherForecast
        case .weatherRadar: .weatherRadar
        case .clockWorld: .clockWorld
        }
    }

    var symbol: String {
        switch self {
        case .radar: "scope"
        case .tracker: "airplane"
        case .weather: "cloud.rain.fill"
        case .clock: "clock.fill"
        case .solar: "sun.max.fill"
        case .eclipse: "circle.lefthalf.filled"
        case .moon: "moonphase.full.moon"
        case .iss: "globe.americas.fill"
        case .launchNasa, .launchRoscosmos, .launchSpacex:
            "rocket.fill"
        case .launchNasaMap, .launchRoscosmosMap, .launchSpacexMap:
            "map.fill"
        case .trackerFrame: "arrow.up"
        case .trackerDetail: "airplane.circle.fill"
        case .weatherForecast: "calendar"
        case .weatherRadar: "cloud.rain.fill"
        case .clockWorld: "globe"
        }
    }

    var color: Color {
        switch self {
        case .radar: RadarTheme.green
        case .tracker: Color(red: 1.0, green: 0.78, blue: 0.18)
        case .weather: .cyan
        case .clock: .red
        case .solar, .eclipse, .moon, .iss: .indigo
        case .launchNasa, .launchRoscosmos, .launchSpacex,
             .launchNasaMap, .launchRoscosmosMap, .launchSpacexMap: .orange
        case .trackerFrame, .trackerDetail:
            Color(red: 1.0, green: 0.78, blue: 0.18)
        case .weatherForecast, .weatherRadar: .cyan
        case .clockWorld: .red
        }
    }

    var usesSputnikGlyph: Bool {
        switch self {
        case .launchNasa, .launchRoscosmos, .launchSpacex:
            true
        default:
            false
        }
    }
}

private enum CompanionUtility: String, Identifiable {
    case apiKeys
    case libraries

    var id: String { rawValue }
}

private enum LibraryKind: String, CaseIterable, Identifiable {
    case aircraft
    case airlines

    var id: String { rawValue }

    var title: String {
        switch self {
        case .aircraft: "Aircraft"
        case .airlines: "Airlines"
        }
    }
}

private struct CompanionUtilitySheet: View {
    let utility: CompanionUtility
    @ObservedObject var model: CompanionModel
    @Binding var selectedLibrary: LibraryKind
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Image(
                    systemName: utility == .apiKeys
                        ? "key.horizontal.fill"
                        : "square.grid.3x3.fill"
                )
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(RadarTheme.green)
                Text(
                    utility == .apiKeys
                        ? "API Keys"
                        : "Asset Libraries"
                )
                .font(.system(size: 20, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
                Spacer()
                Button {
                    dismiss()
                } label: {
                    Image(systemName: "xmark")
                        .frame(width: 28, height: 28)
                }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 22)
            .frame(height: 62)

            Divider().overlay(.white.opacity(0.08))

            if utility == .apiKeys {
                APIsWorkspace(model: model)
            } else {
                LibrariesWorkspace(
                    selectedLibrary: $selectedLibrary
                )
            }
        }
        .frame(width: 820, height: 660)
        .background(RadarTheme.background)
    }
}

/// Native utility-sheet surface used by the documentation renderer. Keeping
/// this wrapper beside the production sheet ensures the guide captures the
/// exact API and asset-library UI instead of a hand-built approximation.
struct CompanionUtilityGuideView: View {
    @ObservedObject var model: CompanionModel
    private let utilityName: String
    @State private var selectedLibrary: LibraryKind

    init(
        model: CompanionModel,
        utilityName: String,
        selectedLibrary: String = "aircraft"
    ) {
        self.model = model
        self.utilityName = utilityName
        _selectedLibrary = State(
            initialValue: LibraryKind(rawValue: selectedLibrary) ?? .aircraft
        )
    }

    var body: some View {
        CompanionUtilitySheet(
            utility: CompanionUtility(rawValue: utilityName) ?? .apiKeys,
            model: model,
            selectedLibrary: $selectedLibrary
        )
    }
}

private struct CompanionRail: View {
    @Binding var page: CompanionPage
    let openUtility: (CompanionUtility) -> Void
    let openDeviceManagement: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 9) {
                    Image(systemName: "scope")
                        .font(.system(size: 22, weight: .medium))
                        .foregroundStyle(RadarTheme.green)
                    Text("Radar")
                        .font(.system(size: 25, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                }

                Text("AirESP32ace")
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .tracking(1.8)
                    .foregroundStyle(.secondary)
                    .padding(.leading, 34)
            }
            .padding(.horizontal, 22)
            .padding(.top, 24)
            .padding(.bottom, 30)

            VStack(spacing: 5) {
                ForEach(CompanionPage.allCases) { item in
                    Button {
                        page = item
                    } label: {
                        HStack(spacing: 11) {
                            CompanionSectionIcon(
                                page: item,
                                selected: page == item
                            )
                            Text(item.title)
                                .font(.system(size: 14, weight: .medium))
                            Spacer()
                            if page == item {
                                Circle()
                                    .fill(RadarTheme.green)
                                    .frame(width: 5, height: 5)
                            }
                        }
                        .foregroundStyle(page == item ? .white : .secondary)
                        .padding(.horizontal, 10)
                        .frame(height: 44)
                        .background(
                            page == item ? .white.opacity(0.075) : .clear,
                            in: RoundedRectangle(cornerRadius: 13, style: .continuous)
                        )
                        .overlay(alignment: .leading) {
                            if page == item {
                                Capsule()
                                    .fill(RadarTheme.green)
                                    .frame(width: 3, height: 20)
                                    .offset(x: -1)
                            }
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 12)

            Spacer()

            HStack(spacing: 10) {
                UtilityButton(
                    symbol: "key.horizontal.fill",
                    title: "API Keys"
                ) {
                    openUtility(.apiKeys)
                }
                UtilityButton(
                    symbol: "square.grid.3x3.fill",
                    title: "Libraries"
                ) {
                    openUtility(.libraries)
                }
                DeviceUtilityButton(action: openDeviceManagement)
            }
            .padding(.horizontal, 18)
            .padding(.bottom, 15)

            HStack(spacing: 9) {
                Circle()
                    .fill(RadarTheme.green)
                    .frame(width: 7, height: 7)
                    .shadow(color: RadarTheme.green, radius: 5)
                VStack(alignment: .leading, spacing: 2) {
                    Text("ESP32-S3")
                        .font(.caption.weight(.medium))
                        .foregroundStyle(.white)
                    Text("466 × 466 AMOLED")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(22)
        }
        .background(Color.black.opacity(0.52))
        .overlay(alignment: .trailing) {
            Rectangle()
                .fill(.white.opacity(0.07))
                .frame(width: 1)
        }
    }
}

private struct CompanionSectionIcon: View {
    let page: CompanionPage
    let selected: Bool

    var body: some View {
        ZStack {
            Circle()
                .fill(
                    LinearGradient(
                        colors: [
                            page.color.opacity(selected ? 0.34 : 0.15),
                            Color.black.opacity(0.68),
                        ],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )
                )
            Circle()
                .stroke(
                    page.color.opacity(selected ? 0.88 : 0.28),
                    lineWidth: 1
                )
            if page == .launches {
                SputnikGlyph(
                    color: selected
                        ? page.color
                        : .white.opacity(0.62)
                )
                .frame(width: 16, height: 16)
            } else if page == .dock {
                DockDeviceGlyph(
                    color: selected
                        ? page.color
                        : .white.opacity(0.62)
                )
                .frame(width: 17, height: 17)
            } else if page == .settings {
                GearAssetGlyph(
                    color: selected
                        ? page.color
                        : .white.opacity(0.62)
                )
                .frame(width: 18, height: 18)
            } else {
                Image(systemName: page.symbol)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(
                        selected ? page.color : .white.opacity(0.62)
                    )
            }
        }
        .frame(width: 30, height: 30)
        .shadow(
            color: selected ? page.color.opacity(0.35) : .clear,
            radius: 7
        )
    }
}

private struct GearAssetGlyph: View {
    let color: Color

    private static let artwork: NSImage? = {
        guard let url = Bundle.module.url(
            forResource: "gear_sidebar",
            withExtension: "png"
        ) else { return nil }
        return NSImage(contentsOf: url)
    }()

    var body: some View {
        Group {
            if let artwork = Self.artwork {
                Image(nsImage: artwork)
                    .resizable()
                    .renderingMode(.template)
                    .scaledToFit()
            } else {
                Image(systemName: "gearshape.fill")
                    .resizable()
                    .scaledToFit()
            }
        }
        .foregroundStyle(color)
        .accessibilityHidden(true)
    }
}

private struct DockDeviceGlyph: View {
    let color: Color

    var body: some View {
        ZStack(alignment: .bottom) {
            DeviceOutlineGlyph(color: color)
                .padding(.bottom, 3)
            Capsule()
                .fill(color)
                .frame(width: 14, height: 2.5)
        }
    }
}

private struct SputnikGlyph: View {
    let color: Color

    private static let bundledImage: NSImage? = {
        guard let url = Bundle.module.url(
            forResource: "sputnik_launch",
            withExtension: "png"
        ) else {
            return nil
        }
        return NSImage(contentsOf: url)
    }()

    var body: some View {
        Group {
            if let bundledImage = Self.bundledImage {
                Image(nsImage: bundledImage)
                    .resizable()
                    .renderingMode(.template)
                    .scaledToFit()
            } else {
                Image(systemName: "satellite.fill")
                    .resizable()
                    .scaledToFit()
            }
        }
        .foregroundStyle(color)
        .accessibilityHidden(true)
    }
}

private struct UtilityButton: View {
    let symbol: String
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .semibold))
                .foregroundStyle(.white.opacity(0.72))
                .frame(width: 34, height: 34)
                .background(.white.opacity(0.055), in: Circle())
                .overlay(
                    Circle()
                        .stroke(.white.opacity(0.08), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
        .help(title)
        .accessibilityLabel(title)
    }
}

private struct DeviceUtilityButton: View {
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            DeviceOutlineGlyph(color: .white.opacity(0.74))
                .frame(width: 17, height: 17)
                .frame(width: 34, height: 34)
                .background(.white.opacity(0.055), in: Circle())
                .overlay(
                    Circle()
                        .stroke(.white.opacity(0.08), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
        .help("Device Setup & Firmware")
        .accessibilityLabel("Device Setup and Firmware")
    }
}

private struct CompanionTopBar: View {
    let page: CompanionPage
    let queuedMapRefresh: Bool
    let isBusy: Bool
    let statusMessage: String?
    let progress: Double?
    let sync: () -> Void

    var body: some View {
        HStack(spacing: 18) {
            VStack(alignment: .leading, spacing: 3) {
                Text(page.title)
                    .font(.system(size: 23, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text(page.subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            if let statusMessage {
                VStack(alignment: .trailing, spacing: 6) {
                    Text(statusMessage)
                        .font(.system(size: 11, weight: .medium, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.66))
                        .lineLimit(1)
                        .truncationMode(.middle)
                    if let progress {
                        ProgressView(value: progress)
                            .progressViewStyle(.linear)
                            .tint(RadarTheme.green)
                            .frame(width: 260)
                    }
                }
                .frame(maxWidth: 330, alignment: .trailing)
            }

            Button(action: sync) {
                HStack(spacing: 8) {
                    if isBusy {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Image(systemName: queuedMapRefresh ? "arrow.triangle.2.circlepath" : "arrow.up.circle.fill")
                    }
                    Text(page.syncTitle)
                        .fontWeight(.semibold)
                }
                .frame(minWidth: 104)
            }
            .buttonStyle(.borderedProminent)
            .tint(RadarTheme.green)
            .controlSize(.large)
            .disabled(isBusy)
            .keyboardShortcut("s", modifiers: [.command])
        }
        .padding(.horizontal, 25)
        .frame(height: 76)
        .background(.black.opacity(0.28))
    }
}

private struct DeviceWorkspace: View {
    @ObservedObject var model: CompanionModel
    @Binding var queuedMapRefresh: Bool
    @State private var addingCity = false

    private let locationColumns = [
        GridItem(.adaptive(minimum: 126, maximum: 168), spacing: 10)
    ]

    var body: some View {
        WorkspaceScroll {
            RadarPanel(title: "Radar locations", symbol: "scope") {
                VStack(alignment: .leading, spacing: 10) {
                    HStack {
                        Text("UP TO EIGHT CITIES")
                            .font(.system(size: 9, weight: .semibold, design: .monospaced))
                            .tracking(1.1)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Button {
                            queuedMapRefresh.toggle()
                        } label: {
                            Label(
                                queuedMapRefresh ? "Maps queued" : "Rebuild maps",
                                systemImage: queuedMapRefresh
                                    ? "checkmark.circle.fill"
                                    : "arrow.triangle.2.circlepath"
                            )
                        }
                        .buttonStyle(.borderless)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(
                            queuedMapRefresh ? RadarTheme.green : .secondary
                        )
                    }

                    ScrollView(.vertical, showsIndicators: true) {
                        LazyVGrid(columns: locationColumns, spacing: 10) {
                            ForEach(Array(radarCities.enumerated()), id: \.element.key) { item in
                                RadarCityEditorCard(
                                    location: item.element,
                                    selected: selectedLocationKey == item.element.key,
                                    canMoveLeft: item.offset > 0,
                                    canMoveRight: item.offset + 1 < radarCities.count,
                                    canRemove: radarCities.count > 1,
                                    artworkRevision: model.cityArtworkRevision,
                                    select: { select(item.element) },
                                    upload: { chooseIcon(for: item.element) },
                                    moveLeft: { moveRadarCity(at: item.offset, by: -1) },
                                    moveRight: { moveRadarCity(at: item.offset, by: 1) },
                                    remove: {
                                        withAnimation(.snappy(duration: 0.22)) {
                                            model.radarCityKeys.removeAll { $0 == item.element.key }
                                        }
                                    }
                                )
                            }
                        }
                        .padding(.trailing, 8)
                    }
                    .frame(minHeight: 220, maxHeight: 390)

                    if model.radarCityKeys.count < 8 {
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 8) {
                                Text("ADD")
                                    .font(.system(size: 9, weight: .bold, design: .monospaced))
                                    .foregroundStyle(.secondary)
                                ForEach(availableRadarCities) { location in
                                    Button {
                                        guard model.radarCityKeys.count < 8 else { return }
                                        withAnimation(.snappy(duration: 0.22)) {
                                            model.radarCityKeys.append(location.key)
                                        }
                                    } label: {
                                        HStack(spacing: 5) {
                                            CityArtwork(key: location.key, size: 25)
                                            Text(location.key)
                                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                                            Image(systemName: "plus")
                                                .font(.system(size: 9, weight: .bold))
                                        }
                                        .padding(.horizontal, 8)
                                        .frame(height: 36)
                                        .background(.white.opacity(0.04), in: Capsule())
                                    }
                                    .buttonStyle(.plain)
                                }
                            }
                        }
                    }

                    HStack(spacing: 10) {
                        Button {
                            addingCity = true
                        } label: {
                            Label("New City", systemImage: "plus.circle.fill")
                                .font(.system(size: 10, weight: .semibold, design: .rounded))
                                .padding(.horizontal, 11)
                                .frame(height: 36)
                                .background(RadarTheme.green.opacity(0.10), in: Capsule())
                                .overlay(
                                    Capsule().stroke(
                                        RadarTheme.green.opacity(0.42),
                                        lineWidth: 1
                                    )
                                )
                        }
                        .buttonStyle(.plain)
                        .foregroundStyle(RadarTheme.green)
                        .disabled(model.radarCityKeys.count >= 8)
                        .opacity(model.radarCityKeys.count >= 8 ? 0.38 : 1)
                        .help("Add a city using decimal latitude and longitude")

                        if model.radarCityKeys.count >= 8 {
                            Text("Remove one city to add another.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
        }
        .sheet(isPresented: $addingCity) {
            AddRadarCityView(model: model) { location in
                select(location)
                queuedMapRefresh = true
                addingCity = false
            } cancel: {
                addingCity = false
            }
        }
    }

    private var selectedLocationKey: String {
        model.selectedRadarPreviewLocationKey
    }

    private var radarCities: [RadarLocationPreset] {
        model.configuredRadarLocations
    }

    private var availableRadarCities: [RadarLocationPreset] {
        model.radarLocationCatalog.filter {
            !model.radarCityKeys.contains($0.key)
        }
    }

    private func moveRadarCity(at index: Int, by offset: Int) {
        let destination = index + offset
        guard model.radarCityKeys.indices.contains(index),
              model.radarCityKeys.indices.contains(destination) else { return }
        withAnimation(.snappy(duration: 0.22)) {
            model.radarCityKeys.swapAt(index, destination)
        }
    }

    private func chooseIcon(for location: RadarLocationPreset) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.png, .jpeg, .webP]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task { await model.installCityIcon(from: url, for: location) }
    }

    private func select(_ location: RadarLocationPreset) {
        model.selectRadarLocation(location)
    }
}

private struct AddRadarCityView: View {
    @ObservedObject var model: CompanionModel
    let completion: (RadarLocationPreset) -> Void
    let cancel: () -> Void

    @State private var code = ""
    @State private var cityName = ""
    @State private var latitude = ""
    @State private var longitude = ""
    @State private var timezone = "UTC0"
    @State private var metar = ""
    @State private var errorMessage: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Add Radar City")
                        .font(.system(size: 23, weight: .semibold, design: .rounded))
                    Text("The city and its generated map pack will sync to the instrument.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button(action: cancel) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 21))
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }

            HStack(spacing: 12) {
                entry("Short code", placeholder: "SEA", text: $code)
                    .frame(width: 130)
                entry("City name", placeholder: "Seattle", text: $cityName)
            }

            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 12) {
                    entry("Latitude", placeholder: "47.4502", text: $latitude)
                    entry("Longitude", placeholder: "-122.3088", text: $longitude)
                }
                Text("Use signed decimal degrees: north/east are positive; south/west are negative.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 12) {
                entry(
                    "Time zone (POSIX)",
                    placeholder: "PST8PDT,M3.2.0,M11.1.0",
                    text: $timezone
                )
                entry("Nearest METAR (optional)", placeholder: "KSEA", text: $metar)
                    .frame(width: 190)
            }

            VStack(alignment: .leading, spacing: 7) {
                Text("Use the nearest airport's IATA code as the short code, or choose any unique three-letter label.")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                HStack(spacing: 18) {
                    Link(destination: URL(string: "https://ourairports.com/")!) {
                        Label("Find code & coordinates", systemImage: "mappin.and.ellipse")
                    }
                    Link(destination: URL(string: "https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv")!) {
                        Label("Find POSIX time zone", systemImage: "clock")
                    }
                }
                .font(.caption.weight(.semibold))
                .foregroundStyle(RadarTheme.green)
                .buttonStyle(.plain)
            }

            if let errorMessage {
                Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundStyle(.orange)
            }

            HStack {
                Text("Up to eight Radar cities can be stored.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Cancel", action: cancel)
                    .buttonStyle(.borderless)
                Button("Add City", action: addCity)
                    .buttonStyle(.borderedProminent)
                    .tint(RadarTheme.green)
                    .disabled(!isValid)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(26)
        .frame(width: 590)
        .background(Color(red: 0.025, green: 0.028, blue: 0.03))
    }

    private func entry(
        _ title: String,
        placeholder: String,
        text: Binding<String>
    ) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .tracking(0.7)
                .foregroundStyle(.secondary)
            MacConfigTextField(placeholder: placeholder, text: text)
                .frame(height: 28)
        }
    }

    private var normalizedCode: String {
        code.trimmed.uppercased()
    }

    private var coordinate: (Double, Double)? {
        guard let lat = Double(latitude.trimmed),
              let lon = Double(longitude.trimmed),
              (-90...90).contains(lat),
              (-180...180).contains(lon) else { return nil }
        return (lat, lon)
    }

    private var isValid: Bool {
        (2...7).contains(normalizedCode.count) &&
            normalizedCode.allSatisfy {
                $0.isASCII && ($0.isLetter || $0.isNumber)
            } &&
            !cityName.trimmed.isEmpty &&
            fieldIsSafe(cityName.trimmed, maximumLength: 47) &&
            fieldIsSafe(
                timezone.trimmed.isEmpty ? "UTC0" : timezone.trimmed,
                maximumLength: 63
            ) &&
            fieldIsSafe(metar.trimmed, maximumLength: 7) &&
            coordinate != nil &&
            model.radarLocation(for: normalizedCode) == nil &&
            model.radarCityKeys.count < 8
    }

    private func addCity() {
        guard let coordinate else {
            errorMessage = "Enter valid decimal coordinates."
            return
        }
        guard model.radarLocation(for: normalizedCode) == nil else {
            errorMessage = "That short code is already in use."
            return
        }
        guard let location = model.addCustomRadarLocation(
            key: normalizedCode,
            label: cityName,
            latitude: coordinate.0,
            longitude: coordinate.1,
            timezone: timezone,
            metar: metar
        ) else {
            errorMessage = "The city could not be added. Check the fields and the eight-city limit."
            return
        }
        completion(location)
    }

    private func fieldIsSafe(
        _ value: String,
        maximumLength: Int
    ) -> Bool {
        value.count <= maximumLength &&
            !value.contains("|") &&
            !value.contains("\n") &&
            !value.contains("\r")
    }
}

private struct FlightWorkspace: View {
    @ObservedObject var model: CompanionModel

    var body: some View {
        WorkspaceScroll {
            RadarPanel(title: "Flight to Track", symbol: "airplane.departure") {
                HStack(alignment: .bottom, spacing: 14) {
                    LabeledField("Flight number") {
                        MacConfigTextField(placeholder: "YP111", text: $model.trackedFlightNumber)
                            .frame(width: 190, height: 27)
                    }

                    LabeledField("Departure date") {
                        DatePicker(
                            "",
                            selection: $model.trackedFlightDate,
                            displayedComponents: .date
                        )
                        .labelsHidden()
                        .datePickerStyle(.field)
                        .frame(width: 152)
                    }

                    Button("Add Flight") {
                        Task { await model.addFlightToSchedule() }
                    }
                    .controlSize(.large)

                    Button("Refresh Active") {
                        Task { _ = await model.refreshTrackedFlight() }
                    }
                    .controlSize(.large)
                    .disabled(model.flightPlans.isEmpty)
                }

                Text("Store up to four non-overlapping flights. The active flight stays on the tracker until it is reported landed, arrived, or cancelled.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            RadarPanel(title: "Flight Schedule", symbol: "calendar.badge.clock") {
                if model.flightPlans.isEmpty {
                    Text("No flights scheduled")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(model.flightPlans) { plan in
                        FlightScheduleRow(
                            plan: plan,
                            isActive: plan.id == model.activeFlightPlanID,
                            refresh: {
                                Task {
                                    await model.refreshFlightInSchedule(plan.id)
                                }
                            },
                            remove: {
                                model.removeFlightFromSchedule(plan.id)
                            }
                        )
                        if plan.id != model.flightPlans.last?.id {
                            Divider().overlay(.white.opacity(0.06))
                        }
                    }
                }
            }

            if model.hasFlightToTrack {
                FlightSummaryCard(
                    flight: model.trackedFlight,
                    status: model.flightStatusText
                )
            } else {
                NothingToTrackCard(status: model.flightStatusText)
            }
        }
        .sheet(item: $model.pendingFlightLegChoice) { choice in
            FlightLegChooser(
                choice: choice,
                select: { candidate in
                    Task { await model.chooseFlightLeg(candidate) }
                },
                cancel: {
                    model.dismissFlightLegChoice()
                }
            )
        }
    }
}

struct FlightLegChooser: View {
    let choice: PendingFlightLegChoice
    let select: (FlightLegCandidate) -> Void
    let cancel: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 5) {
                    Text("Choose \(choice.marketingNumber) leg")
                        .font(.system(size: 24, weight: .semibold))
                    Text(
                        "This flight number is used for multiple segments on \(Self.dayFormatter.string(from: choice.departureDate))."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                }
                Spacer()
                Button("Cancel", action: cancel)
                    .buttonStyle(.borderless)
            }

            VStack(spacing: 9) {
                ForEach(choice.candidates) { candidate in
                    Button {
                        select(candidate)
                    } label: {
                        HStack(spacing: 15) {
                            VStack(alignment: .leading, spacing: 5) {
                                Text(
                                    "\(candidate.originCode)  →  \(candidate.destinationCode)"
                                )
                                .font(.system(
                                    size: 19,
                                    weight: .semibold,
                                    design: .monospaced
                                ))
                                .foregroundStyle(.white)
                                Text(Self.departureText(candidate))
                                    .font(.callout)
                                    .foregroundStyle(.secondary)
                            }
                            Spacer()
                            VStack(alignment: .trailing, spacing: 6) {
                                if candidate.id ==
                                    choice.candidates.first?.id &&
                                    !candidate.isComplete {
                                    Text("RECOMMENDED")
                                        .font(.system(
                                            size: 9,
                                            weight: .bold,
                                            design: .monospaced
                                        ))
                                        .foregroundStyle(RadarTheme.green)
                                }
                                Text(
                                    candidate.isComplete
                                        ? "COMPLETED"
                                        : candidate.statusSummary.uppercased()
                                )
                                .font(.system(
                                    size: 10,
                                    weight: .semibold,
                                    design: .monospaced
                                ))
                                .foregroundStyle(
                                    candidate.isComplete
                                        ? Color.secondary
                                        : Color.white.opacity(0.8)
                                )
                            }
                            Image(systemName: "chevron.right")
                                .font(.system(size: 12, weight: .semibold))
                                .foregroundStyle(.secondary)
                        }
                        .padding(.horizontal, 16)
                        .padding(.vertical, 14)
                        .background(
                            .white.opacity(0.045),
                            in: RoundedRectangle(
                                cornerRadius: 15,
                                style: .continuous
                            )
                        )
                        .overlay {
                            RoundedRectangle(
                                cornerRadius: 15,
                                style: .continuous
                            )
                            .stroke(
                                candidate.id ==
                                    choice.candidates.first?.id &&
                                    !candidate.isComplete
                                    ? RadarTheme.green.opacity(0.45)
                                    : .white.opacity(0.08),
                                lineWidth: 1
                            )
                        }
                    }
                    .buttonStyle(.plain)
                }
            }

            Text(
                "The selected route and departure time are saved together, so later refreshes stay on this exact leg."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
        }
        .padding(26)
        .frame(width: 570)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private static let dayFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .long
        formatter.timeStyle = .none
        return formatter
    }()

    private static func departureText(
        _ candidate: FlightLegCandidate
    ) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "EEE, MMM d · h:mm a zzz"
        if let identifier = candidate.departureTimeZoneIdentifier,
           let timeZone = TimeZone(identifier: identifier) {
            formatter.timeZone = timeZone
        }
        return formatter.string(from: candidate.scheduledDeparture)
    }
}

private struct NothingToTrackCard: View {
    let status: String

    var body: some View {
        VStack(spacing: 13) {
            Text("NOTHING TO TRACK")
                .font(.system(size: 14, weight: .semibold, design: .monospaced))
                .tracking(1.8)
                .foregroundStyle(.white)

            Text("Add a flight number and departure date above, then sync it to the device.")
                .font(.callout)
                .foregroundStyle(.secondary)

            if !status.isEmpty,
               status != "Nothing to track. Add a flight number and date to begin." {
                Text(status)
                    .font(.caption)
                    .foregroundStyle(status.hasPrefix("Looking up") ?
                        RadarTheme.green : .orange)
                    .multilineTextAlignment(.center)
                    .textSelection(.enabled)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 32)
        .background(
            LinearGradient(
                colors: [
                    RadarTheme.green.opacity(0.045),
                    .white.opacity(0.018),
                ],
                startPoint: .top,
                endPoint: .bottom
            ),
            in: RoundedRectangle(cornerRadius: 22, style: .continuous)
        )
        .overlay {
            RoundedRectangle(cornerRadius: 22, style: .continuous)
                .stroke(.white.opacity(0.07), lineWidth: 1)
        }
    }
}

private struct FlightScheduleRow: View {
    let plan: PlannedFlight
    let isActive: Bool
    let refresh: () -> Void
    let remove: () -> Void

    private static let dayFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "EEE, MMM d, yyyy"
        return formatter
    }()

    private static let timeFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm"
        return formatter
    }()

    var body: some View {
        HStack(spacing: 13) {
            ZStack {
                Circle()
                    .fill(isActive ? RadarTheme.green.opacity(0.18) : .white.opacity(0.045))
                Image(systemName: plan.isComplete ? "checkmark" : "airplane")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(isActive ? RadarTheme.green : .white.opacity(0.62))
            }
            .frame(width: 34, height: 34)

            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 8) {
                    Text(plan.flightNumber)
                        .font(.system(size: 15, weight: .semibold, design: .monospaced))
                        .foregroundStyle(.white)
                    if isActive {
                        Text("ACTIVE")
                            .font(.system(size: 8, weight: .bold, design: .monospaced))
                            .foregroundStyle(RadarTheme.green)
                    }
                    if plan.mapsPrepared {
                        Image(systemName: "checkmark.circle.fill")
                            .font(.system(size: 11, weight: .semibold))
                            .foregroundStyle(RadarTheme.green)
                            .help("Both route maps are prepared")
                    } else {
                        ProgressView()
                            .controlSize(.mini)
                            .help("Preparing route maps")
                    }
                }
                Text(Self.dayFormatter.string(from: plan.departureDate))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(width: 170, alignment: .leading)

            VStack(alignment: .leading, spacing: 3) {
                Text(plan.statusSummary)
                    .font(.caption)
                    .foregroundStyle(
                        plan.isComplete
                            ? Color.secondary
                            : Color.white.opacity(0.78)
                    )
                    .lineLimit(1)
                if let departure = plan.scheduledDeparture {
                    Text("Scheduled \(Self.timeFormatter.string(from: departure))")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                } else {
                    Text("Exact departure time pending")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            Button(action: refresh) {
                Image(systemName: "arrow.clockwise")
            }
            .buttonStyle(.borderless)
            .help("Refresh this flight")

            Button(role: .destructive, action: remove) {
                Image(systemName: "trash")
            }
            .buttonStyle(.borderless)
            .help("Remove from schedule")
        }
        .padding(.vertical, 2)
    }
}

private struct ScreenFamilyWorkspace: View {
    let detail: String
    let pages: [(symbol: String, title: String)]

    var body: some View {
        WorkspaceScroll {
            VStack(alignment: .leading, spacing: 22) {
                Text(detail)
                    .font(.system(size: 17, weight: .medium, design: .rounded))
                    .foregroundStyle(.white.opacity(0.82))

                HStack(spacing: 0) {
                    ForEach(
                        Array(pages.enumerated()),
                        id: \.offset
                    ) { item in
                        HStack(spacing: 9) {
                            Image(systemName: item.element.symbol)
                                .font(.system(size: 16, weight: .semibold))
                                .foregroundStyle(RadarTheme.green)
                            Text(item.element.title)
                                .font(.callout.weight(.medium))
                                .foregroundStyle(.white)
                        }
                        .frame(maxWidth: .infinity)

                        if item.offset < pages.count - 1 {
                            Image(systemName: "chevron.right")
                                .font(.system(size: 9, weight: .bold))
                                .foregroundStyle(.secondary.opacity(0.55))
                        }
                    }
                }

                Text("Use the dots below the digital twin to inspect the same swipe views available on the instrument.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 12)
        }
    }
}

private struct DockWorkspace: View {
    @ObservedObject var model: CompanionModel
    @Binding var previewSelection: Int

    var body: some View {
        WorkspaceScroll {
            RadarPanel(title: "Dock rotation", symbol: "rectangle.stack.fill") {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Text("Choose 3–10 screens, arrange their order, and set each dwell time.")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text("\(model.dockSchedule.count) / 10 ACTIVE")
                            .font(.system(size: 9, weight: .semibold, design: .monospaced))
                            .tracking(0.8)
                            .foregroundStyle(RadarTheme.green)
                    }

                    ForEach(model.dockOrderedScreens) { screen in
                        dockRow(screen)
                    }
                }
            }

            Text("Dock Mode uses this exact order. Data stays live, but every device control remains locked until Dock Mode is exited with the two-button chord.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 8)
        }
    }

    @ViewBuilder
    private func dockRow(
        _ screen: SharedConfiguration.DockScreen
    ) -> some View {
        let enabled = model.dockIsEnabled(screen)
        let scheduleIndex = model.dockSchedule.firstIndex(
            where: { $0.screen == screen }
        )
        HStack(spacing: 12) {
            Toggle(
                "",
                isOn: Binding(
                    get: { model.dockIsEnabled(screen) },
                    set: { model.setDockScreen(screen, enabled: $0) }
                )
            )
            .labelsHidden()
            .toggleStyle(.checkbox)
            .disabled(
                enabled
                    ? model.dockSchedule.count <= 3
                    : model.dockSchedule.count >= 10
            )

            Group {
                if screen.usesSputnikGlyph {
                    SputnikGlyph(
                        color: enabled ? screen.color : .secondary
                    )
                    .frame(width: 18, height: 18)
                } else {
                    Image(systemName: screen.symbol)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundStyle(
                            enabled ? screen.color : .secondary
                        )
                }
            }
            .frame(width: 24)

            VStack(alignment: .leading, spacing: 2) {
                Text(screen.title)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundStyle(enabled ? .white : .secondary)
                Text(enabled ? durationText(screen) : "Not in rotation")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            if enabled, let scheduleIndex {
                Stepper(
                    value: Binding(
                        get: {
                            model.dockSchedule[scheduleIndex]
                                .durationSeconds
                        },
                        set: {
                            model.setDockDuration($0, for: screen)
                        }
                    ),
                    in: 5...300,
                    step: 5
                ) {
                    Text(durationText(screen))
                        .font(.system(size: 11, weight: .medium, design: .monospaced))
                        .frame(width: 58, alignment: .trailing)
                }
                .frame(width: 116)

                Image(systemName: "line.3.horizontal")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(.secondary)
                    .frame(width: 25)
                    .help("Drag to reorder")
            } else {
                Color.clear.frame(width: 149, height: 1)
            }
        }
        .padding(.horizontal, 12)
        .frame(height: 54)
        .background(
            enabled ? .white.opacity(0.045) : .white.opacity(0.018),
            in: RoundedRectangle(cornerRadius: 13, style: .continuous)
        )
        .overlay(alignment: .leading) {
            if enabled {
                Capsule()
                    .fill(screen.color)
                    .frame(width: 2, height: 26)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture {
            guard enabled,
                  let index = model.dockSchedule.firstIndex(
                    where: { $0.screen == screen }
                  ) else { return }
            previewSelection = index
        }
        .draggable(enabled ? screen.rawValue : "")
        .dropDestination(for: String.self) { values, _ in
            guard enabled,
                  let raw = values.first,
                  let dragged = SharedConfiguration.DockScreen(rawValue: raw),
                  let destination = model.dockSchedule.firstIndex(
                    where: { $0.screen == screen }
                  ) else { return false }
            withAnimation(.snappy(duration: 0.22)) {
                model.moveDockScreen(dragged, to: destination)
                previewSelection = destination
            }
            return true
        }
    }

    private func durationText(
        _ screen: SharedConfiguration.DockScreen
    ) -> String {
        guard let seconds = model.dockSchedule.first(
            where: { $0.screen == screen }
        )?.durationSeconds else { return "—" }
        if seconds >= 60, seconds % 60 == 0 {
            return "\(seconds / 60) min"
        }
        return "\(seconds) sec"
    }
}

private extension SharedConfiguration.MainScreen {
    var symbol: String {
        switch self {
        case .radar: "scope"
        case .tracker: "airplane"
        case .weather: "cloud.rain.fill"
        case .clock: "clock.fill"
        case .space: "sparkles"
        case .launches: "rocket.fill"
        }
    }

    var color: Color {
        switch self {
        case .radar: RadarTheme.green
        case .tracker: Color(red: 1.0, green: 0.78, blue: 0.18)
        case .weather: Color(red: 0.25, green: 0.72, blue: 1.0)
        case .clock: Color(red: 0.94, green: 0.27, blue: 0.31)
        case .space: Color(red: 0.62, green: 0.47, blue: 1.0)
        case .launches: Color(red: 1.0, green: 0.47, blue: 0.19)
        }
    }
}

private struct SettingsWorkspace: View {
    @ObservedObject var model: CompanionModel
    @State private var showingRestoreConfirmation = false

    var body: some View {
        WorkspaceScroll {
            RadarPanel(
                title: "Device & Network",
                symbol: "antenna.radiowaves.left.and.right"
            ) {
                HStack(alignment: .top, spacing: 20) {
                    VStack(alignment: .leading, spacing: 12) {
                        LabeledField("ESP32 address") {
                            MacConfigTextField(
                                placeholder: "192.168.0.34",
                                text: $model.deviceHost
                            )
                            .frame(height: 25)
                        }

                        HStack {
                            ProviderStateDot(ready: model.deviceReachable)
                            Text(
                                model.deviceReachable
                                    ? "Device responded"
                                    : "Not checked yet"
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            Spacer()
                            Button("Check") {
                                Task { await model.refreshDeviceStatus() }
                            }
                        }
                    }
                    .frame(maxWidth: .infinity)

                    Divider().overlay(.white.opacity(0.07))

                    VStack(alignment: .leading, spacing: 12) {
                        Picker(
                            "Active",
                            selection: $model.activeDeviceWiFiProfile
                        ) {
                            ForEach(DeviceWiFiProfile.allCases) { profile in
                                Text(profile.title).tag(profile)
                            }
                        }
                        .pickerStyle(.segmented)

                        if model.activeDeviceWiFiProfile == .home {
                            networkFields(
                                title: "Home network",
                                ssid: model.isGuidePresentation
                                    ? .constant("Studio Wi-Fi")
                                    : $model.homeWiFiSSID,
                                password: model.isGuidePresentation
                                    ? .constant("")
                                    : $model.homeWiFiPassword
                            )
                        } else {
                            networkFields(
                                title: "iPhone hotspot",
                                ssid: model.isGuidePresentation
                                    ? .constant("Personal Hotspot")
                                    : $model.hotspotWiFiSSID,
                                password: model.isGuidePresentation
                                    ? .constant("")
                                    : $model.hotspotWiFiPassword
                            )
                        }
                    }
                    .frame(maxWidth: .infinity)
                }
            }

            RadarPanel(title: "Display", symbol: "slider.horizontal.3") {
                VStack(spacing: 14) {
                    HStack(spacing: 18) {
                        Toggle(
                            "Flight numbers",
                            isOn: $model.aircraftLabelsEnabled
                        )
                        Toggle(
                            "Sweep wedge",
                            isOn: $model.radarWedgeEnabled
                        )
                        Spacer()
                    }

                    Divider().overlay(.white.opacity(0.07))

                    HStack(spacing: 12) {
                        Text("Units")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        HStack(spacing: 4) {
                            ForEach(WeatherDisplayUnits.allCases) { units in
                                Button {
                                    model.weatherDisplayUnits = units
                                } label: {
                                    Text(units.rawValue)
                                        .font(.system(
                                            size: 12,
                                            weight: .semibold,
                                            design: .rounded
                                        ))
                                        .frame(width: 28, height: 24)
                                        .foregroundStyle(
                                            model.weatherDisplayUnits == units
                                                ? Color(
                                                    rgbHex: model.accentHex(
                                                        for: model.accentColor
                                                    )
                                                )
                                                : Color.secondary
                                        )
                                        .background {
                                            Capsule()
                                                .fill(
                                                    model.weatherDisplayUnits == units
                                                        ? Color(
                                                            rgbHex: model.accentHex(
                                                                for: model.accentColor
                                                            )
                                                        ).opacity(0.13)
                                                        : .clear
                                                )
                                        }
                                }
                                .buttonStyle(.plain)
                                .help(
                                    units == .celsius
                                        ? "Celsius and mmHg"
                                        : "Fahrenheit and kPa"
                                )
                            }
                        }
                        .padding(3)
                        .background(.black.opacity(0.35), in: Capsule())
                        .overlay {
                            Capsule()
                                .stroke(.white.opacity(0.12), lineWidth: 1)
                        }
                        Spacer(minLength: 28)
                        Text("Accent")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        ForEach(DeviceAccentColor.allCases) { color in
                            AccentButton(
                                color: color,
                                displayColor: Color(
                                    rgbHex: model.accentHex(for: color)
                                ),
                                editable: color != .green,
                                editableColor: accentBinding(for: color),
                                selected: model.accentColor == color
                            ) {
                                model.accentColor = color
                            }
                        }
                        Spacer()
                    }
                }
            }

            RadarPanel(title: "Main navigation", symbol: "arrow.left.arrow.right") {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Choose the visible main screens and drag active rows into the order used by swipes and the top button.")
                        .font(.callout)
                        .foregroundStyle(.secondary)

                    ForEach(model.mainOrderedScreens) { screen in
                        mainScreenRow(screen)
                    }
                }
            }

            HStack {
                Button(role: .destructive) {
                    showingRestoreConfirmation = true
                } label: {
                    Label("Restore Defaults", systemImage: "arrow.counterclockwise")
                }
                .buttonStyle(.bordered)

                Text("Restores display, navigation, and Dock rotation. Networks, API keys, flights, cities, and maps are preserved.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
            }
            .padding(.horizontal, 8)
        }
        .confirmationDialog(
            "Restore display and navigation defaults?",
            isPresented: $showingRestoreConfirmation
        ) {
            Button("Restore Defaults", role: .destructive) {
                withAnimation(.snappy(duration: 0.24)) {
                    model.restoreDisplayAndNavigationDefaults()
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Your networks, API keys, flight schedule, city library, and downloaded maps will not be changed.")
        }
    }

    private func accentBinding(
        for color: DeviceAccentColor
    ) -> Binding<Color> {
        Binding(
            get: {
                Color(rgbHex: model.accentHex(for: color))
            },
            set: { newColor in
                if let hex = newColor.rgbHexString {
                    model.setAccentHex(hex, for: color)
                }
            }
        )
    }

    @ViewBuilder
    private func networkFields(
        title: String,
        ssid: Binding<String>,
        password: Binding<String>
    ) -> some View {
        LabeledField(title) {
            MacConfigTextField(placeholder: "SSID", text: ssid)
                .frame(height: 25)
        }
        LabeledField("Password") {
            MacConfigTextField(
                placeholder: "Saved if left blank",
                text: password,
                isSecure: true
            )
            .frame(height: 25)
        }
    }

    @ViewBuilder
    private func mainScreenRow(
        _ screen: SharedConfiguration.MainScreen
    ) -> some View {
        let enabled = model.mainScreenIsEnabled(screen)
        HStack(spacing: 12) {
            Toggle(
                "",
                isOn: Binding(
                    get: { model.mainScreenIsEnabled(screen) },
                    set: { model.setMainScreen(screen, enabled: $0) }
                )
            )
            .labelsHidden()
            .toggleStyle(.checkbox)
            .disabled(enabled && model.mainScreenOrder.count == 1)

            Group {
                if screen == .launches {
                    SputnikGlyph(
                        color: enabled ? screen.color : .secondary
                    )
                    .frame(width: 18, height: 18)
                } else {
                    Image(systemName: screen.symbol)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundStyle(
                            enabled ? screen.color : .secondary
                        )
                }
            }
            .frame(width: 24)

            Text(screen.title)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundStyle(enabled ? .white : .secondary)
            Spacer()

            if enabled {
                Image(systemName: "line.3.horizontal")
                    .foregroundStyle(.secondary)
                    .help("Drag to reorder")
            }
        }
        .padding(.horizontal, 12)
        .frame(height: 48)
        .background(
            enabled ? .white.opacity(0.045) : .white.opacity(0.018),
            in: RoundedRectangle(cornerRadius: 13, style: .continuous)
        )
        .overlay(alignment: .leading) {
            if enabled {
                Capsule()
                    .fill(screen.color)
                    .frame(width: 2, height: 24)
            }
        }
        .draggable(enabled ? screen.rawValue : "")
        .dropDestination(for: String.self) { values, _ in
            guard enabled,
                  let raw = values.first,
                  let dragged = SharedConfiguration.MainScreen(rawValue: raw),
                  let destination = model.mainScreenOrder.firstIndex(of: screen)
            else { return false }
            withAnimation(.snappy(duration: 0.22)) {
                model.moveMainScreen(dragged, to: destination)
            }
            return true
        }
    }
}

private struct LaunchWorkspace: View {
    @ObservedObject var model: CompanionModel

    private var launches: [TrackedLaunch] {
        let selected = model.selectedAgencyLaunches
        return selected.isEmpty ? [model.nextLaunch] : selected
    }

    var body: some View {
        WorkspaceScroll {
            RadarPanel(
                title: "Mission sources",
                symbol: "rocket.fill"
            ) {
                HStack(spacing: 10) {
                    Text("SHOW")
                        .font(.system(size: 9, weight: .bold, design: .monospaced))
                        .tracking(1.1)
                        .foregroundStyle(.secondary)
                    ForEach(LaunchAgency.allCases, id: \.self) { agency in
                        Toggle(
                            agency.displayName,
                            isOn: Binding(
                                get: { model.selectedLaunchAgencies.contains(agency) },
                                set: { model.setLaunchAgency(agency, enabled: $0) }
                            )
                        )
                        .toggleStyle(.checkbox)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(
                            model.selectedLaunchAgencies.contains(agency)
                                ? RadarTheme.green
                                : .secondary
                        )
                    }
                    Spacer()
                }

                HStack(spacing: 12) {
                    Text(model.launchStatusText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                    Spacer()
                    Button {
                        Task {
                            _ = await model
                                .refreshNextLaunch()
                        }
                    } label: {
                        Label(
                            "Refresh Schedule",
                            systemImage: "arrow.clockwise"
                        )
                    }
                    .buttonStyle(.bordered)
                }
            }

            ForEach(launches) { launch in
                LaunchMissionPanel(launch: launch)
            }
        }
        .task {
            _ = await model.refreshNextLaunch()
        }
    }
}

private struct LaunchMissionPanel: View {
    let launch: TrackedLaunch

    private var targetText: String {
        launch.net.formatted(
            date: .abbreviated,
            time: .shortened
        )
    }

    var body: some View {
        RadarPanel(
            title: launch.agency.displayName,
            symbol: "sparkles"
        ) {
            HStack(alignment: .top, spacing: 16) {
                LaunchAgencyLogo(agency: launch.agency)
                    .frame(width: 72, height: 72)

                VStack(alignment: .leading, spacing: 5) {
                    Text(launch.missionName)
                        .font(
                            .system(
                                size: 24,
                                weight: .semibold,
                                design: .rounded
                            )
                        )
                        .foregroundStyle(.white)
                        .lineLimit(2)
                    Text(launch.vehicle.displayName)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }

            Divider().overlay(.white.opacity(0.07))
            LaunchFactRow(title: "TARGET", value: targetText)
            LaunchFactRow(title: "SITE", value: launch.site.displayName)
            LaunchFactRow(title: "PAD", value: launch.padName)
            LaunchFactRow(
                title: "STATUS",
                value: launch.statusName,
                color: RadarTheme.green
            )
        }
    }
}

private struct LaunchAgencyLogo: View {
    let agency: LaunchAgency

    var body: some View {
        Group {
            if let image = ProjectAssetCatalog.launchAgencyLogo(for: agency) {
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.high)
                    .scaledToFit()
            } else {
                Text(agency.displayName)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(.white)
            }
        }
        .padding(5)
    }
}

private struct LaunchFactRow: View {
    let title: String
    let value: String
    var color: Color = .white

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(title)
                .font(
                    .system(
                        size: 9,
                        weight: .semibold,
                        design: .monospaced
                    )
                )
                .tracking(1.1)
                .foregroundStyle(.secondary)
                .frame(width: 70, alignment: .leading)
            Text(value)
                .font(.callout.weight(.medium))
                .foregroundStyle(color)
            Spacer()
        }
    }
}

private struct ClockWorkspace: View {
    @ObservedObject var model: CompanionModel

    private var selectedCities: [RadarLocationPreset] {
        model.clockCityKeys.compactMap {
            model.radarLocation(for: $0)
        }
    }

    private var availableCities: [RadarLocationPreset] {
        model.configuredRadarLocations.filter {
            !model.clockCityKeys.contains($0.key)
        }
    }

    var body: some View {
        WorkspaceScroll {
            RadarPanel(
                title: "World clock cities",
                symbol: "clock.badge"
            ) {
                VStack(
                    alignment: .leading,
                    spacing: 4
                ) {
                    HStack {
                        Text("Choose up to five cities from your radar list.")
                            .font(.callout)
                            .foregroundStyle(.white.opacity(0.86))
                        Spacer()
                        Button {
                            restoreDefaultCities()
                        } label: {
                            Label(
                                "Restore Defaults",
                                systemImage: "arrow.counterclockwise"
                            )
                        }
                        .buttonStyle(.borderless)
                        .font(.caption.weight(.semibold))
                    }
                    Text("The radar’s current location is always shown first; these saved cities follow it.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                VStack(spacing: 7) {
                    ForEach(
                        Array(selectedCities.enumerated()),
                        id: \.element.key
                    ) { item in
                        ClockCityRow(
                            city: item.element,
                            index: item.offset,
                            count: selectedCities.count,
                            replacementCities: availableCities,
                            moveUp: {
                                moveCity(
                                    at: item.offset,
                                    by: -1
                                )
                            },
                            moveDown: {
                                moveCity(
                                    at: item.offset,
                                    by: 1
                                )
                            },
                            remove: {
                                removeCity(item.element)
                            },
                            replace: { replacement in
                                replaceCity(
                                    at: item.offset,
                                    with: replacement
                                )
                            }
                        )
                    }
                }

                if !availableCities.isEmpty {
                    Divider()
                        .overlay(.white.opacity(0.07))

                    Text("ADD A CITY")
                        .font(
                            .system(
                                size: 10,
                                weight: .semibold,
                                design: .monospaced
                            )
                        )
                        .tracking(1.2)
                        .foregroundStyle(.secondary)

                    LazyVGrid(
                        columns: [
                            GridItem(.flexible(), spacing: 8),
                            GridItem(.flexible(), spacing: 8),
                        ],
                        spacing: 8
                    ) {
                        ForEach(availableCities) { city in
                            Button {
                                addCity(city)
                            } label: {
                                ClockCityCandidateLabel(
                                    city: city,
                                    symbol: model.clockCityKeys.count < 5
                                        ? "plus"
                                        : "lock.fill"
                                )
                            }
                            .buttonStyle(.plain)
                            .disabled(model.clockCityKeys.count >= 5)
                            .opacity(model.clockCityKeys.count < 5 ? 1 : 0.38)
                        }
                    }
                    if model.clockCityKeys.count >= 5 {
                        Text("Remove one city to add another.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }

        }
    }

    private func addCity(_ city: RadarLocationPreset) {
        guard model.clockCityKeys.count < 5,
              !model.clockCityKeys.contains(city.key) else {
            return
        }
        withAnimation(.snappy(duration: 0.22)) {
            model.clockCityKeys.append(city.key)
        }
    }

    private func removeCity(_ city: RadarLocationPreset) {
        guard model.clockCityKeys.count > 1 else {
            return
        }
        withAnimation(.snappy(duration: 0.22)) {
            model.clockCityKeys.removeAll {
                $0 == city.key
            }
        }
    }

    private func moveCity(at index: Int, by offset: Int) {
        let destination = index + offset
        guard model.clockCityKeys.indices.contains(index),
              model.clockCityKeys.indices.contains(destination) else {
            return
        }
        withAnimation(.snappy(duration: 0.22)) {
            model.clockCityKeys.swapAt(index, destination)
        }
    }

    private func replaceCity(
        at index: Int,
        with city: RadarLocationPreset
    ) {
        guard model.clockCityKeys.indices.contains(index),
              !model.clockCityKeys.contains(city.key) else {
            return
        }
        withAnimation(.snappy(duration: 0.22)) {
            model.clockCityKeys[index] = city.key
        }
    }

    private func restoreDefaultCities() {
        withAnimation(.snappy(duration: 0.24)) {
            let allowed = Set(model.radarCityKeys)
            let defaults = RadarLocationPreset.defaultClockCityKeys.filter {
                allowed.contains($0)
            }
            model.clockCityKeys = Array(defaults.prefix(5))
        }
    }
}

private struct ClockCityCandidateLabel: View {
    let city: RadarLocationPreset
    let symbol: String

    var body: some View {
        HStack(spacing: 9) {
            CityArtwork(key: city.key, size: 30)
            VStack(alignment: .leading, spacing: 1) {
                Text(city.key)
                    .font(
                        .system(
                            size: 12,
                            weight: .semibold,
                            design: .monospaced
                        )
                    )
                    .foregroundStyle(.white)
                Text(city.displayCity)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            Image(systemName: symbol)
                .font(.system(size: 10, weight: .bold))
                .foregroundStyle(RadarTheme.green)
        }
        .padding(.horizontal, 10)
        .frame(height: 48)
        .background(
            .white.opacity(0.035),
            in: RoundedRectangle(
                cornerRadius: 12,
                style: .continuous
            )
        )
    }
}

private struct ClockCityRow: View {
    let city: RadarLocationPreset
    let index: Int
    let count: Int
    let replacementCities: [RadarLocationPreset]
    let moveUp: () -> Void
    let moveDown: () -> Void
    let remove: () -> Void
    let replace: (RadarLocationPreset) -> Void

    var body: some View {
        HStack(spacing: 11) {
            CityArtwork(key: city.key, size: 34)

            VStack(alignment: .leading, spacing: 1) {
                Text(city.key)
                    .font(
                        .system(
                            size: 12,
                            weight: .semibold,
                            design: .monospaced
                        )
                    )
                    .foregroundStyle(.white)
                Text(city.displayCity)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            HStack(spacing: 2) {
                if !replacementCities.isEmpty {
                    Menu {
                        ForEach(replacementCities) { city in
                            Button {
                                replace(city)
                            } label: {
                                Text("\(city.key) · \(city.displayCity)")
                            }
                        }
                    } label: {
                        Image(systemName: "arrow.triangle.2.circlepath")
                    }
                    .menuStyle(.borderlessButton)
                    .help("Replace this city")
                }
                Button(action: moveUp) {
                    Image(systemName: "chevron.up")
                }
                .disabled(index == 0)
                Button(action: moveDown) {
                    Image(systemName: "chevron.down")
                }
                .disabled(index == count - 1)
                Button(role: .destructive, action: remove) {
                    Image(systemName: "minus.circle")
                }
                .disabled(count <= 1)
            }
            .buttonStyle(.borderless)
            .font(.system(size: 11, weight: .semibold))
        }
        .padding(.horizontal, 11)
        .frame(height: 46)
        .background(
            .white.opacity(0.035),
            in: RoundedRectangle(
                cornerRadius: 13,
                style: .continuous
            )
        )
        .overlay(
            RoundedRectangle(
                cornerRadius: 13,
                style: .continuous
            )
            .stroke(.white.opacity(0.06), lineWidth: 1)
        )
    }
}

private struct LocationsWorkspace: View {
    @ObservedObject var model: CompanionModel
    @Binding var queuedMapRefresh: Bool

    private let columns = [
        GridItem(.flexible(), spacing: 12),
        GridItem(.flexible(), spacing: 12),
        GridItem(.flexible(), spacing: 12),
        GridItem(.flexible(), spacing: 12),
    ]

    var body: some View {
        WorkspaceScroll {
            LazyVGrid(columns: columns, spacing: 12) {
                ForEach(RadarLocationPreset.all) { location in
                    LocationTile(
                        location: location,
                        selected: selectedLocationKey == location.key
                    ) {
                        select(location)
                    }
                }
            }

            RadarPanel(title: "Stored Map Pack", symbol: "square.stack.3d.up.fill") {
                HStack(spacing: 14) {
                    MapRadiusBadge(value: "10")
                    MapRadiusBadge(value: "20")
                    MapRadiusBadge(value: "50")

                    VStack(alignment: .leading, spacing: 3) {
                        Text("24 graphite maps")
                            .font(.callout.weight(.semibold))
                            .foregroundStyle(.white)
                        Text("8 locations × 3 radii, retained on device storage")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    Button {
                        queuedMapRefresh.toggle()
                    } label: {
                        Label(
                            queuedMapRefresh ? "Queued for Sync" : "Queue map rebuild",
                            systemImage: queuedMapRefresh ? "checkmark.circle.fill" : "arrow.triangle.2.circlepath"
                        )
                    }
                    .buttonStyle(.bordered)
                    .tint(queuedMapRefresh ? RadarTheme.green : .secondary)
                }
            }
        }
    }

    private var selectedLocationKey: String {
        model.selectedRadarPreviewLocationKey
    }

    private func select(_ location: RadarLocationPreset) {
        model.selectRadarLocation(location)
    }
}

private struct APIsWorkspace: View {
    @ObservedObject var model: CompanionModel

    private var flightAwareBinding: Binding<String> {
        model.isGuidePresentation
            ? .constant("guide-flightaware-key")
            : $model.flightAwareAPIKey
    }

    private var openSkyClientBinding: Binding<String> {
        model.isGuidePresentation
            ? .constant("client-id")
            : $model.openSkyClientID
    }

    private var openSkySecretBinding: Binding<String> {
        model.isGuidePresentation
            ? .constant("guide-client-secret")
            : $model.openSkyClientSecret
    }

    private var airLabsBinding: Binding<String> {
        model.isGuidePresentation
            ? .constant("guide-airlabs-key")
            : $model.airLabsAPIKey
    }

    var body: some View {
        WorkspaceScroll {
            VStack(spacing: 14) {
                ProviderCard(
                    name: "FlightAware",
                    role: "Tracked flight, route, schedule, ETA",
                    provider: .flightAware,
                    status: model.flightAwareCredentialState
                ) {
                    MacConfigTextField(
                        placeholder: "AeroAPI key",
                        text: flightAwareBinding,
                        isSecure: true
                    )
                    .frame(height: 26)
                }

                ProviderCard(
                    name: "OpenSky",
                    role: "Nearby aircraft positions and motion",
                    provider: .openSky,
                    status: model.openSkyCredentialState
                ) {
                    HStack(spacing: 12) {
                        MacConfigTextField(
                            placeholder: "client_id",
                            text: openSkyClientBinding
                        )
                        MacConfigTextField(
                            placeholder: "client_secret",
                            text: openSkySecretBinding,
                            isSecure: true
                        )
                    }
                    .frame(height: 26)
                }

                ProviderCard(
                    name: "AirLabs",
                    role: "Tracked-flight fallback plus missing radar details",
                    provider: .airLabs,
                    status: model.airLabsCredentialState
                ) {
                    MacConfigTextField(
                        placeholder: "Fallback API key",
                        text: airLabsBinding,
                        isSecure: true
                    )
                    .frame(height: 26)
                }
            }

            Text("Credentials are stored in this Mac account with owner-only file permissions. Only OpenSky and AirLabs are copied to the portable device; FlightAware remains on the Mac.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 4)
        }
        .task(id: model.apiCredentialFingerprint) {
            guard !model.isGuidePresentation else { return }
            model.markAPICredentialsForValidation()
            do {
                try await Task.sleep(for: .milliseconds(350))
            } catch {
                return
            }
            guard !Task.isCancelled else { return }
            await model.validateAPICredentials()
        }
    }
}

private struct LibrariesWorkspace: View {
    @Binding var selectedLibrary: LibraryKind
    @State private var airlineQuery = ""

    private let columns = [GridItem(.adaptive(minimum: 112, maximum: 150), spacing: 12)]

    private var filteredAirlines: [ProjectAssetCatalog.AirlineEntry] {
        let query = airlineQuery.trimmed
        guard !query.isEmpty else {
            return ProjectAssetCatalog.majorAirlines
        }
        return ProjectAssetCatalog.majorAirlines.filter {
            $0.code.localizedCaseInsensitiveContains(query) ||
                $0.name.localizedCaseInsensitiveContains(query)
        }
    }

    var body: some View {
        WorkspaceScroll {
            HStack(spacing: 14) {
                Picker("Library", selection: $selectedLibrary) {
                    ForEach(LibraryKind.allCases) { library in
                        Text(library.title).tag(library)
                    }
                }
                .pickerStyle(.segmented)
                .frame(width: 240)

                if selectedLibrary == .airlines {
                    TextField(
                        "Search \(ProjectAssetCatalog.majorAirlines.count) airlines",
                        text: $airlineQuery
                    )
                    .textFieldStyle(.roundedBorder)
                    .frame(maxWidth: 280)
                }

                Spacer()

                Text(
                    selectedLibrary == .airlines
                        ? "\(filteredAirlines.count) AIRLINES"
                        : "\(ProjectAssetCatalog.aircraftEntries.count) AIRCRAFT"
                )
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .tracking(1.1)
                .foregroundStyle(.secondary)
            }

            LazyVGrid(columns: columns, spacing: 12) {
                if selectedLibrary == .aircraft {
                    ForEach(
                        ProjectAssetCatalog.aircraftEntries
                    ) { aircraft in
                        AircraftLibraryTile(entry: aircraft)
                    }
                } else {
                    ForEach(filteredAirlines) { airline in
                        AirlineLibraryTile(entry: airline)
                    }
                }
            }
        }
    }
}

private struct DevicePreviewColumn: View {
    @ObservedObject var model: CompanionModel
    let section: CompanionPage
    @Binding var selection: Int

    private struct LaunchPreviewItem {
        let page: DevicePreviewPage
        let launch: TrackedLaunch
    }

    private var launchPreviewItems: [LaunchPreviewItem] {
        let launches = model.selectedAgencyLaunches.isEmpty
            ? [model.nextLaunch]
            : model.selectedAgencyLaunches
        return launches.flatMap { launch in
            [
                LaunchPreviewItem(page: .launchMain, launch: launch),
                LaunchPreviewItem(page: .launchMap, launch: launch),
                LaunchPreviewItem(page: .launchQR, launch: launch),
            ]
        }
    }

    private var pages: [DevicePreviewPage] {
        switch section {
        case .radar:
            [.radarMain]
        case .tracker:
            [.flightGlobe, .flightFrame, .flightDetail]
        case .weather:
            [.weatherCurrent, .weatherForecast, .weatherRadar]
        case .clock:
            [.clockGMT, .clockWorld]
        case .space:
            [.spaceSolar, .spaceEclipse, .spaceISS, .spaceMoon]
        case .launches:
            launchPreviewItems.map(\.page)
        case .dock:
            model.dockSchedule.map { $0.screen.previewPage }
        case .settings:
            [
                .radarLocations,
                .radarConnectivity,
                .radarBrightness,
                .radarAPIs,
            ]
        }
    }

    private var selectedPage: DevicePreviewPage {
        pages[min(max(0, selection), pages.count - 1)]
    }

    private var selectedLaunch: TrackedLaunch {
        guard section == .launches else {
            return model.nextLaunch
        }
        return launchPreviewItems[
            min(max(0, selection), launchPreviewItems.count - 1)
        ].launch
    }

    var body: some View {
        VStack(spacing: 14) {
            VStack(spacing: 4) {
                Text("DEVICE DIGITAL TWIN")
                    .font(.system(size: 10, weight: .semibold, design: .monospaced))
                    .tracking(1.6)
                    .foregroundStyle(.secondary)
                Text(
                    section == .tracker && !model.hasFlightToTrack
                        ? "Nothing to Track"
                        : previewTitle
                )
                    .font(.system(size: 20, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
            }

            ZStack {
                if section == .tracker && !model.hasFlightToTrack {
                    EmptyTrackerPreview()
                    .frame(width: 346, height: 346)
                    .clipShape(Circle())
                } else {
                    InstrumentPreview(
                        page: selectedPage,
                        flight: model.trackedFlight,
                        localRadarMapImage: model.latestLocalRadarMapImage,
                        geodesicMapImage: model.latestGeodesicMapImage,
                        routeFrameMapImage: model.latestRouteFrameMapImage,
                        airlineLogoImage: model.latestAirlineLogoImage,
                        routeProgress: model.routeProgress,
                        locationName: model.portableLocationLabel,
                        accent: model.accentColor,
                        clockCityKeys: model.clockCityKeys,
                        launch: selectedLaunch,
                        flightAwareAPIState:
                            model.flightAwareCredentialState,
                        openSkyAPIState:
                            model.openSkyCredentialState,
                        airLabsAPIState:
                            model.airLabsCredentialState,
                        radarLocations: model.configuredRadarLocations,
                        selectedRadarLocationKey: selectedRadarLocationKey,
                        deviceBatteryPercent: displayedBatteryPercent,
                        deviceBatteryCharging: displayedBatteryCharging,
                        liveAircraft: model.companionAircraftSnapshot,
                        liveWeather: model.companionWeatherSnapshot,
                        weatherUnits: model.weatherDisplayUnits
                    )
                    .id(previewIdentity)
                    .frame(width: 346, height: 346)
                    .clipShape(Circle())
                    .shadow(color: selectedPage.glowColor.opacity(0.24), radius: 32)
                }

                HStack(spacing: 0) {
                    PreviewEdgeNavigation(direction: .backward) {
                        showPreview(selection - 1)
                    }
                    Spacer(minLength: 346)
                    PreviewEdgeNavigation(direction: .forward) {
                        showPreview(selection + 1)
                    }
                }
                .frame(width: 410, height: 190)
            }
            .frame(width: 410, height: 346)

            HStack(spacing: 10) {
                ForEach(Array(pages.enumerated()), id: \.offset) { item in
                    Button {
                        withAnimation(.snappy(duration: 0.24)) {
                            selection = item.offset
                        }
                    } label: {
                        Circle()
                            .fill(
                                item.offset == selection
                                    ? Color.white
                                    : Color.white.opacity(0.23)
                            )
                            .frame(
                                width: item.offset == selection ? 8 : 6,
                                height: item.offset == selection ? 8 : 6
                            )
                            .contentShape(Circle().inset(by: -8))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(item.element.title)
                    .help(item.element.title)
                }
            }
            .frame(height: 16)

            Text(
                section == .tracker && !model.hasFlightToTrack
                    ? "ADD A FLIGHT TO ENABLE TRACKING"
                    : section == .dock
                        ? "DOCK MODE · CONTROLS LOCKED"
                        : selectedPage.gestureHint
            )
                .font(.system(size: 9, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundStyle(.secondary)

            VStack(spacing: 10) {
                DeviceTelemetryRow(
                    title: "Reachable",
                    value: model.deviceReachabilityTransport.displayName,
                    color: model.deviceReachabilityTransport.isReachable
                        ? RadarTheme.green
                        : .secondary
                )
                DeviceTelemetryRow(
                    title: "Battery",
                    value: batteryText,
                    color: batteryColor
                )
                DeviceTelemetryRow(
                    title: "Last synced",
                    value: lastSyncedText,
                    color: .secondary
                )
            }
            .padding(15)
            .background(.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 17, style: .continuous))
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(28)
        .background(.black.opacity(0.20))
        .task(
            id: [
                model.deviceHost,
                section.rawValue,
                selectedPage.rawValue,
                selectedRadarLocationKey,
                String(model.radarRadiusMiles),
                model.clockCityKeys.joined(separator: ","),
            ].joined(separator: "|")
        ) {
            guard !model.isGuidePresentation else { return }
            while !Task.isCancelled {
                await refreshLivePreview()
                let interval: Duration = switch section {
                case .radar: .seconds(20)
                case .weather: .seconds(60)
                default: .seconds(30)
                }
                try? await Task.sleep(for: interval)
            }
        }
        .onChange(of: pages.count) {
            selection = min(max(0, selection), max(0, pages.count - 1))
        }
    }

    private var previewTitle: String {
        if section == .launches {
            return "\(selectedLaunch.agency.displayName) · \(selectedPage.title)"
        }
        return selectedPage.title
    }

    private var previewIdentity: String {
        let clockRevision = switch selectedPage {
        case .clockGMT, .clockWorld:
            model.clockPreviewRevision
        default:
            0
        }
        return "\(selectedPage.rawValue)-\(clockRevision)"
    }

    private func refreshLivePreview() async {
        async let status: Void = model.refreshDeviceStatus(announce: false)
        switch section {
        case .radar:
            async let map: Void = model.prepareCompanionRadarPreview()
            async let aircraft: Void = model.refreshCompanionAircraft()
            _ = await (status, map, aircraft)
        case .weather:
            async let weather: Void = model.refreshCompanionWeather()
            _ = await (status, weather)
        default:
            _ = await status
        }
    }

    private var batteryText: String {
        guard let percent = displayedBatteryPercent else {
            return "—"
        }
        return displayedBatteryCharging
            ? "⚡︎ \(percent)%"
            : "\(percent)%"
    }

    private var displayedBatteryPercent: Int? {
        model.isGuidePresentation ? 100 : model.deviceBatteryPercent
    }

    private var displayedBatteryCharging: Bool {
        model.isGuidePresentation || model.deviceBatteryCharging
    }

    private var selectedRadarLocationKey: String {
        model.selectedRadarPreviewLocationKey
    }

    private var batteryColor: Color {
        guard let percent = displayedBatteryPercent else {
            return .secondary
        }
        if percent < 25 {
            return Color(red: 1.0, green: 0.27, blue: 0.23)
        }
        if percent < 50 {
            return Color(red: 1.0, green: 0.80, blue: 0.0)
        }
        return Color(red: 0.19, green: 0.82, blue: 0.35)
    }

    private var lastSyncedText: String {
        guard let date = model.deviceLastSyncedAt else {
            return "Not yet"
        }
        return date.formatted(date: .abbreviated, time: .shortened)
    }

    private func showPreview(_ requestedIndex: Int) {
        let count = pages.count
        guard count > 0 else { return }
        let wrapped = (requestedIndex % count + count) % count
        withAnimation(.snappy(duration: 0.24)) {
            selection = wrapped
        }
    }
}

private struct EmptyTrackerPreview: View {
    var body: some View {
        ZStack {
            Color.black
            RadialGradient(
                colors: [
                    RadarTheme.green.opacity(0.045),
                    Color.black,
                ],
                center: .center,
                startRadius: 30,
                endRadius: 170
            )
            VStack(spacing: 11) {
                Image(systemName: "airplane")
                    .font(.system(size: 22, weight: .medium))
                    .foregroundStyle(.white.opacity(0.42))
                Text("NOTHING TO TRACK")
                    .font(.system(size: 12, weight: .semibold, design: .monospaced))
                    .tracking(1.5)
                    .foregroundStyle(.white.opacity(0.82))
            }
        }
    }
}

private enum PreviewNavigationDirection {
    case backward
    case forward
}

private struct PreviewEdgeNavigation: View {
    let direction: PreviewNavigationDirection
    let action: () -> Void
    @State private var hovered = false

    var body: some View {
        Button(action: action) {
            PreviewTriangle(direction: direction)
                .fill(.white.opacity(hovered ? 0.82 : 0))
                .frame(width: 18, height: 54)
                .shadow(color: .white.opacity(hovered ? 0.22 : 0), radius: 9)
                .frame(width: 32, height: 150)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { isHovering in
            withAnimation(.easeOut(duration: 0.18)) {
                hovered = isHovering
            }
        }
        .help(direction == .backward ? "Previous device view" : "Next device view")
    }
}

private struct PreviewTriangle: Shape {
    let direction: PreviewNavigationDirection

    func path(in rect: CGRect) -> Path {
        var path = Path()
        if direction == .backward {
            path.move(to: CGPoint(x: rect.minX, y: rect.midY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        } else {
            path.move(to: CGPoint(x: rect.maxX, y: rect.midY))
            path.addLine(to: CGPoint(x: rect.minX, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        }
        path.closeSubpath()
        return path
    }
}

private struct DeviceTelemetryRow: View {
    let title: String
    let value: String
    let color: Color

    var body: some View {
        HStack(spacing: 9) {
            Circle()
                .fill(color)
                .frame(width: 7, height: 7)
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .tracking(0.7)
                .foregroundStyle(.secondary)
            Spacer(minLength: 8)
            Text(value)
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundStyle(color)
                .lineLimit(1)
        }
    }
}

private struct WorkspaceScroll<Content: View>: View {
    @ViewBuilder let content: Content

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                content
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .topLeading)
        }
        .scrollIndicators(.hidden)
    }
}

private struct RadarPanel<Content: View>: View {
    let title: String
    let symbol: String
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 13) {
            Label(title, systemImage: symbol)
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .foregroundStyle(.secondary)
                .textCase(.uppercase)

            content
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RadarTheme.panel, in: RoundedRectangle(cornerRadius: 19, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 19, style: .continuous)
                .stroke(.white.opacity(0.085), lineWidth: 1)
        )
    }
}

private struct LabeledField<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title)
                .font(.caption2)
                .foregroundStyle(.secondary)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct AccentButton: View {
    let color: DeviceAccentColor
    let displayColor: Color
    let editable: Bool
    let editableColor: Binding<Color>
    let selected: Bool
    let action: () -> Void
    @State private var hovered = false
    @State private var showingEditor = false

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Button(action: action) {
                Circle()
                    .fill(displayColor)
                    .frame(width: 24, height: 24)
                    .overlay {
                        if selected {
                            Circle()
                                .stroke(.white, lineWidth: 2)
                                .padding(3)
                        }
                    }
            }
            .buttonStyle(.plain)

            if editable && hovered {
                Button {
                    showingEditor = true
                } label: {
                    Image(systemName: "pencil.circle.fill")
                        .font(.system(size: 11, weight: .bold))
                        .symbolRenderingMode(.palette)
                        .foregroundStyle(.white, .black.opacity(0.78))
                }
                .buttonStyle(.plain)
                .offset(x: 5, y: -5)
                .help("Edit \(color.title) accent")
            }
        }
        .frame(width: 29, height: 29)
        .onHover { hovered = $0 }
        .popover(isPresented: $showingEditor, arrowEdge: .bottom) {
            VStack(alignment: .leading, spacing: 12) {
                Text("\(color.title) accent")
                    .font(.headline)
                ColorPicker(
                    "Color",
                    selection: editableColor,
                    supportsOpacity: false
                )
                .labelsHidden()
            }
            .padding(16)
            .frame(width: 170)
        }
        .help(color.title)
    }
}

private struct ProviderStateDot: View {
    let ready: Bool

    var body: some View {
        Circle()
            .fill(ready ? RadarTheme.green : Color.secondary.opacity(0.45))
            .frame(width: 8, height: 8)
            .shadow(color: ready ? RadarTheme.green : .clear, radius: 4)
    }
}

private struct ProviderCard<Content: View>: View {
    let name: String
    let role: String
    let provider: APIProviderArtwork
    let status: APICredentialState
    @ViewBuilder let content: Content

    var body: some View {
        HStack(spacing: 18) {
            APIProviderLogo(provider: provider, width: 126, height: 42)

            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 8) {
                    Text(name)
                        .font(.system(size: 17, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                    APIHealthIndicator(status: status)
                }
                Text(role)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(width: 245, alignment: .leading)

            content
        }
        .padding(18)
        .background(RadarTheme.panel, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .stroke(.white.opacity(0.085), lineWidth: 1)
        )
    }
}

private struct APIHealthIndicator: View {
    let status: APICredentialState

    var body: some View {
        Group {
            switch status {
            case .checking:
                ProgressView()
                    .controlSize(.small)
                    .tint(.orange)
            case .valid:
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(RadarTheme.green)
            case .invalid:
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(Color.red)
            }
        }
        .frame(width: 18, height: 18)
        .help(helpText)
        .animation(.easeInOut(duration: 0.18), value: status)
    }

    private var helpText: String {
        switch status {
        case .checking:
            "Checking authorization…"
        case .valid:
            "Authorized"
        case .invalid(let detail):
            detail
        }
    }
}

private struct FlightSummaryCard: View {
    let flight: TrackedFlight
    let status: String

    var body: some View {
        VStack(spacing: 16) {
            HStack {
                AirportBlock(
                    airport: flight.origin,
                    alignment: .leading
                )
                Spacer()
                VStack(spacing: 5) {
                    Image(systemName: "airplane")
                        .font(.system(size: 25, weight: .medium))
                        .foregroundStyle(RadarTheme.green)
                    Text(flight.marketingNumber)
                        .font(.system(size: 15, weight: .semibold, design: .monospaced))
                        .foregroundStyle(.white)
                }
                .frame(width: 110)
                Spacer()
                AirportBlock(
                    airport: flight.destination,
                    alignment: .trailing
                )
            }

            if let departureGateText {
                HStack(spacing: 4) {
                    Image(systemName: "arrow.up.right")
                        .font(.system(size: 10, weight: .black))
                    Text(departureGateText)
                        .font(.system(size: 12, weight: .black, design: .rounded))
                }
                .foregroundStyle(.black)
                .padding(.horizontal, 7)
                .frame(height: 20)
                .background(
                    Color(red: 1, green: 0.83, blue: 0),
                    in: RoundedRectangle(cornerRadius: 2)
                )
                .frame(maxWidth: .infinity, alignment: .center)
            }

            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Capsule()
                        .fill(.white.opacity(0.10))
                        .frame(height: 3)
                    Capsule()
                        .fill(RadarTheme.green)
                        .frame(
                            width: proxy.size.width * max(0.02, min(1, flight.routeProgress ?? 0.02)),
                            height: 3
                        )
                }
            }
            .frame(height: 3)

            HStack(spacing: 28) {
                FlightMetric(title: "Aircraft", value: flight.aircraftType)
                FlightMetric(title: "Duration", value: flight.durationSummary)
                FlightMetric(title: "Distance", value: flight.distanceSummary)
            }

            Text(status)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(22)
        .background(RadarTheme.panel, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 22, style: .continuous)
                .stroke(RadarTheme.green.opacity(0.22), lineWidth: 1)
        )
    }

    private var departureGateText: String? {
        let gate = flight.originGate?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !gate.isEmpty {
            return gate
        }
        let terminal = flight.originTerminal?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !terminal.isEmpty else { return nil }
        return terminal.uppercased().hasPrefix("T")
            ? terminal
            : "T\(terminal)"
    }
}

private struct AirportBlock: View {
    let airport: Airport
    let alignment: HorizontalAlignment

    var body: some View {
        VStack(alignment: alignment, spacing: 3) {
            Text(airport.code)
                .font(.system(size: 30, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
            Text(airport.city)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(width: 150, alignment: alignment == .leading ? .leading : .trailing)
    }
}

private struct FlightMetric: View {
    let title: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.callout.weight(.medium))
                .foregroundStyle(.white)
                .lineLimit(1)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct FeatureRow: View {
    let symbol: String
    let title: String
    let detail: String

    var body: some View {
        HStack(spacing: 11) {
            Image(systemName: symbol)
                .foregroundStyle(RadarTheme.green)
                .frame(width: 20)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.callout.weight(.medium))
                    .foregroundStyle(.white)
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

private struct PollRow: View {
    let value: String
    let label: String

    var body: some View {
        HStack(spacing: 12) {
            Text(value)
                .font(.system(size: 13, weight: .bold, design: .monospaced))
                .foregroundStyle(RadarTheme.green)
                .frame(width: 34, alignment: .trailing)
            Text(label)
                .font(.callout)
                .foregroundStyle(.white.opacity(0.84))
        }
    }
}

private struct LocationTile: View {
    let location: RadarLocationPreset
    let selected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 9) {
                CityArtwork(key: location.key, size: 52)
                    .opacity(selected ? 1 : 0.82)
                Text(location.key)
                    .font(.system(size: 15, weight: .semibold, design: .monospaced))
                    .foregroundStyle(.white)
                Text(location.displayCity)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity)
            .frame(height: 134)
            .background(
                selected ? RadarTheme.green.opacity(0.09) : .white.opacity(0.035),
                in: RoundedRectangle(cornerRadius: 18, style: .continuous)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .stroke(
                        selected ? RadarTheme.green.opacity(0.78) : .white.opacity(0.08),
                        lineWidth: selected ? 1.5 : 1
                    )
            )
        }
        .buttonStyle(.plain)
    }
}

private struct RadarCityEditorCard: View {
    let location: RadarLocationPreset
    let selected: Bool
    let canMoveLeft: Bool
    let canMoveRight: Bool
    let canRemove: Bool
    let artworkRevision: Int
    let select: () -> Void
    let upload: () -> Void
    let moveLeft: () -> Void
    let moveRight: () -> Void
    let remove: () -> Void

    var body: some View {
        VStack(spacing: 4) {
            Button(action: select) {
                VStack(spacing: 3) {
                    CityArtwork(key: location.key, size: 46)
                        .id("\(location.key)-\(artworkRevision)")
                        .opacity(selected ? 1 : 0.78)
                    Text(location.key)
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundStyle(selected ? .white : .secondary)
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.plain)

            HStack(spacing: 3) {
                editorButton("chevron.left", enabled: canMoveLeft, action: moveLeft)
                editorButton("photo.badge.plus", enabled: true, action: upload)
                editorButton("minus.circle.fill", enabled: canRemove, action: remove)
                editorButton("chevron.right", enabled: canMoveRight, action: moveRight)
            }
        }
        .padding(.vertical, 7)
        .frame(maxWidth: .infinity)
        .frame(height: 94)
        .background(
            selected ? RadarTheme.green.opacity(0.08) : .white.opacity(0.025),
            in: RoundedRectangle(cornerRadius: 14, style: .continuous)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .stroke(
                    selected ? RadarTheme.green.opacity(0.86) : .white.opacity(0.07),
                    lineWidth: selected ? 1.5 : 1
                )
        )
        .help(location.displayCity)
    }

    private func editorButton(
        _ symbol: String,
        enabled: Bool,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 10, weight: .bold))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .contentShape(Rectangle())
        }
        .frame(minWidth: 28, minHeight: 28)
        .buttonStyle(.plain)
        .background(
            enabled ? Color.white.opacity(0.045) : Color.clear,
            in: RoundedRectangle(cornerRadius: 6, style: .continuous)
        )
        .foregroundStyle(
            enabled
                ? Color.secondary
                : Color.secondary.opacity(0.22)
        )
        .disabled(!enabled)
    }
}

private struct RadarLocationButton: View {
    let location: RadarLocationPreset
    let selected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 4) {
                CityArtwork(key: location.key, size: 31)
                    .opacity(selected ? 1 : 0.70)
                Text(location.key)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(selected ? .white : .secondary)
            }
            .frame(maxWidth: .infinity)
            .frame(height: 54)
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(
                        selected
                            ? RadarTheme.green.opacity(0.86)
                            : .white.opacity(0.07),
                        lineWidth: selected ? 1.5 : 1
                    )
            )
        }
        .buttonStyle(.plain)
        .help(location.displayCity)
    }
}

private struct CityArtwork: View {
    let key: String
    let size: CGFloat

    var body: some View {
        Group {
            if let image = ProjectAssetCatalog.cityImage(for: key) {
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.high)
                    .scaledToFit()
            } else {
                Image(systemName: "mappin.and.ellipse")
                    .resizable()
                    .scaledToFit()
                    .foregroundStyle(.white.opacity(0.72))
                    .padding(size * 0.2)
            }
        }
        .frame(width: size, height: size)
    }
}

private struct MapRadiusBadge: View {
    let value: String

    var body: some View {
        VStack(spacing: 1) {
            Text(value)
                .font(.system(size: 17, weight: .semibold, design: .rounded))
                .foregroundStyle(.white)
            Text("MI")
                .font(.system(size: 8, weight: .bold, design: .monospaced))
                .foregroundStyle(.secondary)
        }
        .frame(width: 48, height: 48)
        .background(.black.opacity(0.30), in: Circle())
        .overlay(Circle().stroke(RadarTheme.green.opacity(0.28), lineWidth: 1))
    }
}

private struct AircraftLibraryTile: View {
    let entry: ProjectAssetCatalog.AircraftEntry

    var body: some View {
        VStack(spacing: 6) {
            if let image = ProjectAssetCatalog.aircraftImage(
                for: entry
            ) {
                Image(nsImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(height: 55)
            }
            Text(entry.label)
                .font(.system(size: 11, weight: .semibold))
                .lineLimit(1)
                .foregroundStyle(.white)
            Text(entry.codes.joined(separator: " · "))
                .font(
                    .system(
                        size: 8,
                        weight: .medium,
                        design: .monospaced
                    )
                )
                .lineLimit(1)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .frame(height: 112)
        .background(.white.opacity(0.035), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .stroke(.white.opacity(0.075), lineWidth: 1)
        )
    }
}

private struct AirlineLibraryTile: View {
    let entry: ProjectAssetCatalog.AirlineEntry
    @State private var logo: NSImage?

    var body: some View {
        VStack(spacing: 8) {
            Group {
                if let logo {
                    Image(nsImage: logo)
                        .resizable()
                        .scaledToFit()
                } else {
                    Text(entry.code)
                        .font(
                            .system(
                                size: 23,
                                weight: .bold,
                                design: .rounded
                            )
                        )
                        .foregroundStyle(.white.opacity(0.55))
                }
            }
            .frame(height: 52)

            Text(entry.code)
                .font(
                    .system(
                        size: 11,
                        weight: .semibold,
                        design: .monospaced
                    )
                )
                .foregroundStyle(.white)
            Text(entry.name)
                .font(.system(size: 9, weight: .medium))
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .frame(maxWidth: .infinity)
        .frame(height: 118)
        .background(
            .black.opacity(0.18),
            in: RoundedRectangle(
                cornerRadius: 16,
                style: .continuous
            )
        )
        .overlay(
            RoundedRectangle(
                cornerRadius: 16,
                style: .continuous
            )
            .stroke(.white.opacity(0.055), lineWidth: 1)
        )
        .task(id: entry.code) {
            logo = await ProjectAssetCatalog.airlineLogo(
                for: entry.code
            )
        }
    }
}

private struct SyncStatusPanel: View {
    @ObservedObject var model: CompanionModel

    var body: some View {
        RadarPanel(title: "Last Sync", symbol: "checkmark.icloud") {
            CompanionStatusRow(title: "Config", state: model.configPushState)
            CompanionStatusRow(title: "Device", state: model.pushState)
        }
    }
}

private struct CompanionStatusRow: View {
    let title: String
    let value: String
    let color: Color

    init(title: String, value: String, color: Color) {
        self.title = title
        self.value = value
        self.color = color
    }

    init(title: String, state: PushState) {
        self.title = title
        self.value = state.message
        self.color = state.color
    }

    var body: some View {
        HStack(spacing: 9) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 48, alignment: .leading)
            Circle()
                .fill(color)
                .frame(width: 6, height: 6)
            Text(value)
                .font(.caption)
                .foregroundStyle(color)
                .lineLimit(2)
            Spacer()
        }
    }
}

private enum RadarTheme {
    static let green = Color(red: 0.12, green: 0.72, blue: 0.24)

    static let background = LinearGradient(
        colors: [
            Color(red: 0.004, green: 0.006, blue: 0.007),
            Color(red: 0.012, green: 0.019, blue: 0.017),
        ],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    static let panel = LinearGradient(
        colors: [
            Color.white.opacity(0.065),
            Color.white.opacity(0.026),
        ],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
}

private extension DeviceAccentColor {
    var color: Color {
        switch self {
        case .green: RadarTheme.green
        case .orange: .orange
        case .cyan: .cyan
        case .red: .red
        }
    }
}

private extension Color {
    init(rgbHex: String) {
        let normalized = rgbHex
            .trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        let value = UInt64(normalized, radix: 16) ?? 0xFFFFFF
        self.init(
            red: Double((value >> 16) & 0xFF) / 255.0,
            green: Double((value >> 8) & 0xFF) / 255.0,
            blue: Double(value & 0xFF) / 255.0
        )
    }

    var rgbHexString: String? {
        guard let converted = NSColor(self).usingColorSpace(.deviceRGB)
        else { return nil }
        return String(
            format: "%02X%02X%02X",
            Int((converted.redComponent * 255.0).rounded()),
            Int((converted.greenComponent * 255.0).rounded()),
            Int((converted.blueComponent * 255.0).rounded())
        )
    }
}

extension RadarLocationPreset {
    var displayCity: String {
        switch key {
        case "SJC": "San Jose"
        case "SFO": "San Francisco"
        case "SAN": "San Diego"
        case "JFK": "New York"
        case "MOW": "Moscow"
        case "LED": "St. Petersburg"
        case "DUS": "Düsseldorf"
        case "ICN": "Seoul"
        default: title
        }
    }

    var symbol: String {
        switch key {
        case "SFO": "bridge.fill"
        case "SAN": "sun.horizon.fill"
        case "JFK": "building.2.fill"
        case "MOW": "star.circle.fill"
        case "LED": "building.columns.fill"
        case "DUS": "tram.fill"
        case "ICN": "building.fill"
        default: "dot.scope"
        }
    }
}

private extension String {
    var trimmed: String {
        trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

private extension PushState {
    var isFailure: Bool {
        if case .failed = self { return true }
        return false
    }

    var color: Color {
        switch self {
        case .idle: .secondary
        case .pushing: .cyan
        case .pushed: RadarTheme.green
        case .failed: .orange
        }
    }
}

private extension RenderState {
    var isRendering: Bool {
        if case .rendering = self { return true }
        return false
    }

    var isFailure: Bool {
        if case .failed = self { return true }
        return false
    }
}
