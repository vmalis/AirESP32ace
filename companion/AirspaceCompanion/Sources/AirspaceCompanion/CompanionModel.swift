// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation

enum DeviceReachabilityTransport: Equatable, Sendable {
    case offline
    case wifi
    case usb

    var displayName: String {
        switch self {
        case .offline: "Offline"
        case .wifi: "Wi-Fi"
        case .usb: "USB-C"
        }
    }

    var isReachable: Bool {
        self != .offline
    }
}

@MainActor
final class CompanionModel: ObservableObject {
    /// Documentation renders use the production views, but must never expose
    /// credentials or network names saved in the current macOS account.
    var isGuidePresentation = false

    @Published var selectedStyle: InstrumentStyle = .cartographicGlass
    @Published var trackedFlight: TrackedFlight = .airPremiaYP111 {
        didSet {
            loadCachedTrackedFlightMaps()
            refreshTrackedAirlineLogo()
        }
    }
    @Published private(set) var flightPlans: [PlannedFlight] =
        CompanionModel.loadFlightPlans()
    @Published private(set) var activeFlightPlanID: UUID?
    @Published var pendingFlightLegChoice: PendingFlightLegChoice?
    @Published var trackedFlightNumber: String = CompanionModel.loadDefault(
        .trackedFlightNumber,
        fallback: "YP111"
    ) {
        didSet {
            guard !isGuidePresentation else { return }
            Self.storeDefault(trackedFlightNumber, key: .trackedFlightNumber)
        }
    }
    @Published var trackedFlightDate: Date = CompanionModel.loadDateDefault(.trackedFlightDate) {
        didSet {
            guard !isGuidePresentation else { return }
            UserDefaults.standard.set(
                trackedFlightDate.timeIntervalSince1970,
                forKey: DefaultsKey.trackedFlightDate.rawValue
            )
        }
    }
    @Published var renderState: RenderState = .idle
    @Published var latestMapImage: NSImage?
    @Published var latestGeodesicMapImage: NSImage?
    @Published var latestRouteFrameMapImage: NSImage?
    @Published var latestLocalRadarMapImage: NSImage?
    @Published var latestAirlineLogoImage: NSImage?
    @Published private(set) var nextLaunch: TrackedLaunch =
        CompanionModel.loadNextLaunch()
    @Published private(set) var nextLaunches: [TrackedLaunch] =
        CompanionModel.loadNextLaunches()
    @Published var launchStatusText: String =
        "Refreshes from the public launch schedule"
    @Published var latestDeviceAsset: ImageWriter.DeviceImageAsset?
    @Published var routeProgress: Double = 0.46
    @Published var deviceHost: String = CompanionModel.loadDefault(
        .deviceHost,
        fallback: "192.168.0.34"
    ) {
        didSet { Self.storeDefault(deviceHost, key: .deviceHost) }
    }
    @Published var flightAwareAPIKey: String = CompanionModel.loadCredential(
        .flightAwareAeroAPIKey,
        environmentKeys: ["FLIGHTAWARE_AEROAPI_KEY", "AEROAPI_KEY"]
    ) {
        didSet { Self.storeCredential(flightAwareAPIKey, account: .flightAwareAeroAPIKey) }
    }
    @Published var nearbyAircraftProvider: NearbyAircraftProvider = CompanionModel.loadNearbyAircraftProvider() {
        didSet {
            UserDefaults.standard.set(nearbyAircraftProvider.rawValue, forKey: DefaultsKey.nearbyAircraftProvider.rawValue)
        }
    }
    @Published var openSkyClientID: String = CompanionModel.loadCredential(
        .openSkyClientID,
        environmentKeys: ["OPENSKY_CLIENT_ID"]
    ) {
        didSet { Self.storeCredential(openSkyClientID, account: .openSkyClientID) }
    }
    @Published var openSkyClientSecret: String = CompanionModel.loadCredential(
        .openSkyClientSecret,
        environmentKeys: ["OPENSKY_CLIENT_SECRET"]
    ) {
        didSet { Self.storeCredential(openSkyClientSecret, account: .openSkyClientSecret) }
    }
    @Published var airLabsAPIKey: String = CompanionModel.loadCredential(
        .airLabsAPIKey,
        environmentKeys: ["AIRLABS_API_KEY"]
    ) {
        didSet { Self.storeCredential(airLabsAPIKey, account: .airLabsAPIKey) }
    }
    @Published private(set) var flightAwareCredentialState:
        APICredentialState = .checking
    @Published private(set) var openSkyCredentialState:
        APICredentialState = .checking
    @Published private(set) var airLabsCredentialState:
        APICredentialState = .checking
    @Published var activeDeviceWiFiProfile: DeviceWiFiProfile = CompanionModel.loadDeviceWiFiProfile() {
        didSet {
            UserDefaults.standard.set(activeDeviceWiFiProfile.rawValue, forKey: DefaultsKey.activeDeviceWiFiProfile.rawValue)
        }
    }
    @Published var homeWiFiSSID: String = CompanionModel.loadSharedDevice()?.wifiProfiles.home.ssid ?? CompanionModel.loadDefault(.homeWiFiSSID, fallback: "") {
        didSet { Self.storeDefault(homeWiFiSSID, key: .homeWiFiSSID) }
    }
    @Published var homeWiFiPassword: String = CompanionModel.loadCredential(.homeWiFiPassword, environmentKeys: []) {
        didSet { Self.storeCredential(homeWiFiPassword, account: .homeWiFiPassword) }
    }
    @Published var hotspotWiFiSSID: String = CompanionModel.loadSharedDevice()?.wifiProfiles.hotspot.ssid ?? CompanionModel.loadDefault(.hotspotWiFiSSID, fallback: "") {
        didSet { Self.storeDefault(hotspotWiFiSSID, key: .hotspotWiFiSSID) }
    }
    @Published var hotspotWiFiPassword: String = CompanionModel.loadCredential(.hotspotWiFiPassword, environmentKeys: []) {
        didSet { Self.storeCredential(hotspotWiFiPassword, account: .hotspotWiFiPassword) }
    }
    @Published var portableLocationLabel: String = CompanionModel.loadSharedDevice()?.location.label ?? CompanionModel.loadDefault(.portableLocationLabel, fallback: "San Jose") {
        didSet {
            Self.storeDefault(
                portableLocationLabel,
                key: .portableLocationLabel
            )
            loadCachedLocalRadarMap()
        }
    }
    @Published var portableLatitude: String = CompanionModel.loadSharedDevice()?.location.lat ?? CompanionModel.loadDefault(.portableLatitude, fallback: "37.3382") {
        didSet { Self.storeDefault(portableLatitude, key: .portableLatitude) }
    }
    @Published var portableLongitude: String = CompanionModel.loadSharedDevice()?.location.lon ?? CompanionModel.loadDefault(.portableLongitude, fallback: "-121.8863") {
        didSet { Self.storeDefault(portableLongitude, key: .portableLongitude) }
    }
    @Published var portableTimezone: String = CompanionModel.loadSharedDevice()?.location.timezone ?? CompanionModel.loadDefault(.portableTimezone, fallback: "PST8PDT,M3.2.0,M11.1.0") {
        didSet { Self.storeDefault(portableTimezone, key: .portableTimezone) }
    }
    @Published var portableMetarStation: String = CompanionModel.loadSharedDevice()?.location.metar ?? CompanionModel.loadDefault(.portableMetarStation, fallback: "KSJC") {
        didSet { Self.storeDefault(portableMetarStation, key: .portableMetarStation) }
    }
    @Published var portableRadarHalfDegrees: String = CompanionModel.loadSharedDevice()?.location.radarHalfDegrees ?? CompanionModel.loadDefault(.portableRadarHalfDegrees, fallback: "0.65") {
        didSet { Self.storeDefault(portableRadarHalfDegrees, key: .portableRadarHalfDegrees) }
    }
    @Published private(set) var selectedRadarPreviewLocationKey = "SJC"
    @Published var aircraftLabelsEnabled: Bool = CompanionModel.loadSharedDevice()?.display.aircraftLabels ?? UserDefaults.standard.bool(forKey: DefaultsKey.aircraftLabelsEnabled.rawValue) {
        didSet { UserDefaults.standard.set(aircraftLabelsEnabled, forKey: DefaultsKey.aircraftLabelsEnabled.rawValue) }
    }
    @Published var radarWedgeEnabled: Bool = CompanionModel.loadSharedDevice()?.display.radarWedge ?? CompanionModel.loadBoolDefault(.radarWedgeEnabled, fallback: true) {
        didSet { UserDefaults.standard.set(radarWedgeEnabled, forKey: DefaultsKey.radarWedgeEnabled.rawValue) }
    }
    @Published var issFlagsEnabled: Bool = CompanionModel.loadSharedDevice()?.display.issFlags ?? CompanionModel.loadBoolDefault(.issFlagsEnabled, fallback: false) {
        didSet { UserDefaults.standard.set(issFlagsEnabled, forKey: DefaultsKey.issFlagsEnabled.rawValue) }
    }
    @Published var weatherDisplayUnits: WeatherDisplayUnits =
        WeatherDisplayUnits(
            rawValue: CompanionModel.loadSharedDevice()?.display.weatherUnits ?? ""
        ) ?? WeatherDisplayUnits(
            rawValue: UserDefaults.standard.string(
                forKey: DefaultsKey.weatherDisplayUnits.rawValue
            ) ?? ""
        ) ?? .celsius {
        didSet {
            UserDefaults.standard.set(
                weatherDisplayUnits.rawValue,
                forKey: DefaultsKey.weatherDisplayUnits.rawValue
            )
        }
    }
    @Published var accentColor: DeviceAccentColor = CompanionModel.loadAccentColor() {
        didSet { UserDefaults.standard.set(accentColor.rawValue, forKey: DefaultsKey.accentColor.rawValue) }
    }
    @Published var customAccentColors: [String: String] =
        CompanionModel.loadCustomAccentColors() {
        didSet {
            let normalized = Self.normalizeCustomAccentColors(
                customAccentColors
            )
            if normalized != customAccentColors {
                customAccentColors = normalized
                return
            }
            if let data = try? JSONEncoder().encode(normalized) {
                UserDefaults.standard.set(
                    data,
                    forKey: DefaultsKey.customAccentColors.rawValue
                )
            }
            persistSharedConfiguration()
        }
    }
    @Published private(set) var customRadarLocations:
        [SharedConfiguration.LocationDefinition] =
        CompanionModel.loadCustomRadarLocations() {
        didSet {
            let normalized = Self.normalizeCustomRadarLocations(
                customRadarLocations
            )
            if normalized != customRadarLocations {
                customRadarLocations = normalized
                return
            }
            if let data = try? JSONEncoder().encode(normalized) {
                UserDefaults.standard.set(
                    data,
                    forKey: DefaultsKey.customRadarLocations.rawValue
                )
            }
            persistSharedConfiguration()
        }
    }
    @Published var radarCityKeys: [String] = CompanionModel.loadRadarCityKeys() {
        didSet {
            let normalized = Self.normalizeRadarCityKeys(radarCityKeys)
            if normalized != radarCityKeys {
                radarCityKeys = normalized
                return
            }
            UserDefaults.standard.set(
                normalized,
                forKey: DefaultsKey.radarCityKeys.rawValue
            )
            let compatibleClock = Self.normalizeClockCityKeys(
                clockCityKeys,
                allowedKeys: Set(normalized)
            )
            if compatibleClock != clockCityKeys {
                clockCityKeys = compatibleClock
            }
            persistSharedConfiguration()
        }
    }
    @Published private(set) var clockPreviewRevision = 0
    @Published var clockCityKeys: [String] = CompanionModel.loadClockCityKeys() {
        didSet {
            let normalized = Self.normalizeClockCityKeys(
                clockCityKeys,
                allowedKeys: Set(radarCityKeys)
            )
            if normalized != clockCityKeys {
                clockCityKeys = normalized
                return
            }
            UserDefaults.standard.set(
                normalized,
                forKey: DefaultsKey.clockCityKeys.rawValue
            )
            clockPreviewRevision &+= 1
            persistSharedConfiguration()
        }
    }

    var radarLocationCatalog: [RadarLocationPreset] {
        RadarLocationPreset.all + customRadarLocations.map(
            RadarLocationPreset.init(definition:)
        )
    }

    var configuredRadarLocations: [RadarLocationPreset] {
        radarCityKeys.compactMap(radarLocation(for:))
    }

    func radarLocation(for key: String) -> RadarLocationPreset? {
        radarLocationCatalog.first {
            $0.key.caseInsensitiveCompare(key) == .orderedSame
        }
    }

    func radarLocation(label: String, metar: String) -> RadarLocationPreset? {
        let label = label.trimmed
        let metar = metar.trimmed.uppercased()
        return radarLocationCatalog.first {
            $0.key.caseInsensitiveCompare(label) == .orderedSame ||
                $0.title.caseInsensitiveCompare(label) == .orderedSame ||
                (!metar.isEmpty && $0.metar.uppercased() == metar)
        }
    }

    @discardableResult
    func addCustomRadarLocation(
        key: String,
        label: String,
        latitude: Double,
        longitude: Double,
        timezone: String,
        metar: String
    ) -> RadarLocationPreset? {
        guard radarCityKeys.count < 8 else { return nil }
        let normalizedKey = key.trimmed.uppercased()
        let normalizedLabel = label.trimmed
        let normalizedTimezone = timezone.trimmed.isEmpty
            ? "UTC0"
            : timezone.trimmed
        let normalizedMetar = metar.trimmed.uppercased()
        guard Self.locationKeyIsValid(normalizedKey),
              radarLocation(for: normalizedKey) == nil,
              Self.locationFieldIsSafe(normalizedLabel, maximumLength: 47),
              !normalizedLabel.isEmpty,
              Self.locationFieldIsSafe(normalizedTimezone, maximumLength: 63),
              Self.locationFieldIsSafe(normalizedMetar, maximumLength: 7),
              (-90...90).contains(latitude),
              (-180...180).contains(longitude) else {
            return nil
        }
        let definition = SharedConfiguration.LocationDefinition(
            key: normalizedKey,
            label: normalizedLabel,
            lat: String(
                format: "%.6f",
                locale: Locale(identifier: "en_US_POSIX"),
                latitude
            ),
            lon: String(
                format: "%.6f",
                locale: Locale(identifier: "en_US_POSIX"),
                longitude
            ),
            timezone: normalizedTimezone,
            metar: normalizedMetar,
            radarHalfDegrees: "1.55"
        )
        customRadarLocations.append(definition)
        radarCityKeys.append(normalizedKey)
        return RadarLocationPreset(definition: definition)
    }
    @Published var selectedLaunchAgencies: Set<LaunchAgency> =
        CompanionModel.loadLaunchAgencies() {
        didSet {
            if selectedLaunchAgencies.isEmpty {
                selectedLaunchAgencies = [.spacex]
                return
            }
            UserDefaults.standard.set(
                selectedLaunchAgencies.map(\.rawValue).sorted(),
                forKey: DefaultsKey.launchAgencies.rawValue
            )
            persistSharedConfiguration()
        }
    }
    @Published private(set) var dockSchedule:
        [SharedConfiguration.DockScheduleEntry] =
        CompanionModel.loadDockSchedule() {
        didSet {
            if let data = try? JSONEncoder().encode(dockSchedule) {
                UserDefaults.standard.set(
                    data,
                    forKey: DefaultsKey.dockSchedule.rawValue
                )
            }
            persistSharedConfiguration()
        }
    }
    @Published private(set) var mainScreenOrder:
        [SharedConfiguration.MainScreen] =
        CompanionModel.loadMainScreenOrder() {
        didSet {
            if let data = try? JSONEncoder().encode(mainScreenOrder) {
                UserDefaults.standard.set(
                    data,
                    forKey: DefaultsKey.mainScreenOrder.rawValue
                )
            }
            persistSharedConfiguration()
        }
    }
    @Published var radarRadiusMiles: Int = CompanionModel.loadSharedDevice()?.radarRadiusMiles ?? 20
    @Published var pushState: PushState = .idle
    /// Determinate progress for operations that move a known number of bytes.
    /// `nil` keeps the existing indeterminate treatment for ordinary Syncs.
    @Published private(set) var syncProgress: Double?
    @Published var configPushState: PushState = .idle
    @Published var configPushRequiresReconnect: Bool = false
    @Published var deviceStatusText: String = ""
    @Published private(set) var deviceReachable = false
    @Published private(set) var deviceReachabilityTransport:
        DeviceReachabilityTransport = .offline
    @Published private(set) var deviceBatteryPercent: Int?
    @Published private(set) var deviceBatteryCharging = false
    @Published private(set) var deviceLastSyncedAt: Date?
    @Published private(set) var companionAircraftSnapshot:
        DeviceClient.AircraftSnapshot?
    @Published private(set) var companionWeatherSnapshot:
        DeviceClient.WeatherSnapshot?
    @Published private(set) var companionAircraftStatusText = "Waiting for device"
    @Published private(set) var companionWeatherStatusText = "Waiting for device"
    @Published private(set) var cityArtworkRevision = 0
    @Published var flightStatusText: String = "Static route preset"
    @Published var trackedFlightPollingText: String = "Polling starts after Refresh or Sync"
    @Published var nearbyAircraftTestText: String = "Not tested"
    @Published private(set) var trackedFlightTrackingActive = false

    private let staticFlightProvider: FlightStatusProviding
    private let mapRenderer: FlightMapRendering
    private let geodesicMapRenderer: FlightMapRendering
    private let localRadarMapRenderer: LocalRadarMapRenderer
    private let deviceClient: DeviceClient
    private let usbConfigurationClient = USBConfigurationClient()
    private var deviceStatusRefreshInFlight = false
    private let openSkyDiagnosticClient: OpenSkyDiagnosticClient
    private let previewDataClient = CompanionPreviewDataClient()
    private let apiCredentialValidator: APICredentialValidator
    private let launchScheduleProvider: LaunchScheduleProvider
    private var trackedFlightPollingTask: Task<Void, Never>?
    private var airlineLogoTask: Task<Void, Never>?
    private var resolvedFlights: [UUID: TrackedFlight] = [:]
    private var lastSyncedDeviceRevision: UInt32 =
        SharedConfigurationStore.load()?.companion.lastSyncedDeviceRevision ?? 0
    private var hasDeviceSnapshot = SharedConfigurationStore.load() != nil
    private var apiValidationGeneration: UInt = 0
    /// Identifies the USB package whose progress is allowed to update the UI.
    /// The serial client reports progress from a detached worker. Without a
    /// generation guard, callbacks already queued on the main actor can arrive
    /// after the package succeeds and put the Sync button back into a false
    /// "pushing" state.
    private var trackedFlightUSBProgressGeneration: UUID?
    /// Applies the same stale-callback protection to URLSession delegate
    /// progress. An upload may finish before its final delegate callback is
    /// delivered to the main actor.
    private var trackedFlightWiFiProgressGeneration: UUID?

    var selectedAgencyLaunches: [TrackedLaunch] {
        LaunchAgency.allCases.compactMap { agency in
            guard selectedLaunchAgencies.contains(agency) else {
                return nil
            }
            return nextLaunches
                .filter { $0.agency == agency }
                .min { $0.net < $1.net }
        }
    }

    init(
        flightProvider: FlightStatusProviding = StaticFlightStatusProvider(),
        mapRenderer: FlightMapRendering = RouteFrameMapRenderer(),
        geodesicMapRenderer: FlightMapRendering = MapKitFlightMapRenderer(),
        localRadarMapRenderer: LocalRadarMapRenderer = LocalRadarMapRenderer(),
        deviceClient: DeviceClient = DeviceClient(),
        openSkyDiagnosticClient: OpenSkyDiagnosticClient = OpenSkyDiagnosticClient(),
        apiCredentialValidator: APICredentialValidator =
            APICredentialValidator(),
        launchScheduleProvider: LaunchScheduleProvider =
            LaunchScheduleProvider(),
        resumeSavedMonitoring: Bool = true
    ) {
        self.staticFlightProvider = flightProvider
        self.mapRenderer = mapRenderer
        self.geodesicMapRenderer = geodesicMapRenderer
        self.localRadarMapRenderer = localRadarMapRenderer
        self.deviceClient = deviceClient
        self.openSkyDiagnosticClient = openSkyDiagnosticClient
        self.apiCredentialValidator = apiCredentialValidator
        self.launchScheduleProvider =
            launchScheduleProvider
        resolvedFlights = Self.loadResolvedFlights(for: flightPlans)
        if let savedActiveID = SharedConfigurationStore.load()?
            .companion.activeTrackedFlightId,
           flightPlans.contains(where: { $0.id == savedActiveID }) {
            activeFlightPlanID = savedActiveID
        }
        selectedRadarPreviewLocationKey = radarLocation(
            label: portableLocationLabel,
            metar: portableMetarStation
        )?.key ?? radarCityKeys.first ?? "SJC"
        refreshActiveFlightSelection()
        if let activePlan {
            trackedFlightNumber = activePlan.flightNumber
            trackedFlightDate = activePlan.departureDate
        }
        loadCachedPreviewAssets()
        refreshTrackedAirlineLogo()

        if flightPlans.isEmpty {
            clearTrackedFlightPresentation()
        }

        if resumeSavedMonitoring && UserDefaults.standard.bool(
            forKey: DefaultsKey.trackedFlightMonitoringActive.rawValue
        ) {
            Task { @MainActor [weak self] in
                await self?.resumeTrackedFlightMonitoring()
            }
        }
    }

    var flightAwareCredentialSummary: String {
        let key = flightAwareAPIKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if key.isEmpty {
            return airLabsAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                ? "Not set — add FlightAware or AirLabs for live tracked flights."
                : "Not set — tracked flights will use the AirLabs/OpenSky fallback."
        }
        return "Ready — FlightAware is the primary tracked-flight provider."
    }

    var nearbyAircraftCredentialSummary: String {
        switch nearbyAircraftProvider {
        case .openSky:
            let clientID = openSkyClientID.trimmingCharacters(in: .whitespacesAndNewlines)
            let clientSecret = openSkyClientSecret.trimmingCharacters(in: .whitespacesAndNewlines)
            if clientID.isEmpty && clientSecret.isEmpty {
                return "Anonymous mode — okay for first tests, lower OpenSky rate limits."
            }
            if clientID.isEmpty || clientSecret.isEmpty {
                return "Missing one OpenSky field — add both client_id and client_secret."
            }
            return "Ready — nearby aircraft can use authenticated OpenSky requests."
        }
    }

    var dockOrderedScreens: [SharedConfiguration.DockScreen] {
        dockSchedule.map(\.screen) +
            SharedConfiguration.DockScreen.allCases.filter { screen in
                !dockSchedule.contains(where: { $0.screen == screen })
            }
    }

    func dockIsEnabled(_ screen: SharedConfiguration.DockScreen) -> Bool {
        dockSchedule.contains(where: { $0.screen == screen })
    }

    func setDockScreen(
        _ screen: SharedConfiguration.DockScreen,
        enabled: Bool
    ) {
        if enabled {
            guard dockSchedule.count < 10,
                  !dockIsEnabled(screen) else { return }
            dockSchedule.append(
                .init(screen: screen, durationSeconds: 20)
            )
        } else {
            guard dockSchedule.count > 3 else { return }
            dockSchedule.removeAll(where: { $0.screen == screen })
        }
    }

    func setDockDuration(
        _ durationSeconds: Int,
        for screen: SharedConfiguration.DockScreen
    ) {
        guard let index = dockSchedule.firstIndex(
            where: { $0.screen == screen }
        ) else { return }
        dockSchedule[index].durationSeconds = min(
            300,
            max(5, durationSeconds)
        )
    }

    func moveDockScreen(
        _ screen: SharedConfiguration.DockScreen,
        by offset: Int
    ) {
        guard let index = dockSchedule.firstIndex(
            where: { $0.screen == screen }
        ) else { return }
        let destination = index + offset
        guard dockSchedule.indices.contains(destination) else { return }
        dockSchedule.swapAt(index, destination)
    }

    func moveDockScreen(
        _ screen: SharedConfiguration.DockScreen,
        to destination: Int
    ) {
        guard let source = dockSchedule.firstIndex(
            where: { $0.screen == screen }
        ) else { return }
        let bounded = min(max(0, destination), dockSchedule.count - 1)
        guard source != bounded else { return }
        let entry = dockSchedule.remove(at: source)
        dockSchedule.insert(entry, at: bounded)
    }

    var mainOrderedScreens: [SharedConfiguration.MainScreen] {
        mainScreenOrder + SharedConfiguration.MainScreen.allCases.filter {
            !mainScreenOrder.contains($0)
        }
    }

    func mainScreenIsEnabled(
        _ screen: SharedConfiguration.MainScreen
    ) -> Bool {
        mainScreenOrder.contains(screen)
    }

    func setMainScreen(
        _ screen: SharedConfiguration.MainScreen,
        enabled: Bool
    ) {
        if enabled {
            guard !mainScreenOrder.contains(screen) else { return }
            mainScreenOrder.append(screen)
        } else {
            guard mainScreenOrder.count > 1 else { return }
            mainScreenOrder.removeAll { $0 == screen }
        }
    }

    func moveMainScreen(
        _ screen: SharedConfiguration.MainScreen,
        to destination: Int
    ) {
        guard let source = mainScreenOrder.firstIndex(of: screen) else {
            return
        }
        let bounded = min(max(0, destination), mainScreenOrder.count - 1)
        guard source != bounded else { return }
        let value = mainScreenOrder.remove(at: source)
        mainScreenOrder.insert(value, at: bounded)
    }

    func restoreDisplayAndNavigationDefaults() {
        aircraftLabelsEnabled = false
        radarWedgeEnabled = true
        issFlagsEnabled = false
        weatherDisplayUnits = .celsius
        accentColor = .green
        customAccentColors = Self.defaultCustomAccentColors
        mainScreenOrder = SharedConfiguration.MainScreen.allCases
        dockSchedule = Self.defaultDockSchedule
    }

    func accentHex(for color: DeviceAccentColor) -> String {
        if color == .green {
            return "1EB83D"
        }
        return customAccentColors[color.rawValue]
            ?? Self.defaultCustomAccentColors[color.rawValue]
            ?? "FFFFFF"
    }

    func setAccentHex(_ hex: String, for color: DeviceAccentColor) {
        guard color != .green,
              let normalized = Self.normalizedRGBHex(hex) else { return }
        customAccentColors[color.rawValue] = normalized
    }

    var airLabsCredentialSummary: String {
        airLabsAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            ? "Optional — fallback for tracked flights and missing radar details."
            : "Ready — tracked-flight fallback and radar detail enrichment are available."
    }

    var apiCredentialFingerprint: String {
        [
            flightAwareAPIKey,
            openSkyClientID,
            openSkyClientSecret,
            airLabsAPIKey,
        ].joined(separator: "\u{1f}")
    }

    func markAPICredentialsForValidation() {
        apiValidationGeneration &+= 1
        flightAwareCredentialState = .checking
        openSkyCredentialState = .checking
        airLabsCredentialState = .checking
    }

    func validateAPICredentials() async {
        let generation = apiValidationGeneration
        let flightAwareKey = flightAwareAPIKey
        let openSkyID = openSkyClientID
        let openSkySecret = openSkyClientSecret
        let airLabsKey = airLabsAPIKey
        let validator = apiCredentialValidator

        await withTaskGroup(
            of: (APIProvider, APICredentialState).self
        ) { group in
            group.addTask {
                (
                    .flightAware,
                    await validator.validateFlightAware(
                        apiKey: flightAwareKey
                    )
                )
            }
            group.addTask {
                (
                    .openSky,
                    await validator.validateOpenSky(
                        clientID: openSkyID,
                        clientSecret: openSkySecret
                    )
                )
            }
            group.addTask {
                (
                    .airLabs,
                    await validator.validateAirLabs(apiKey: airLabsKey)
                )
            }

            for await (provider, state) in group {
                guard generation == apiValidationGeneration,
                      !Task.isCancelled else {
                    continue
                }
                switch provider {
                case .flightAware:
                    flightAwareCredentialState = state
                case .openSky:
                    openSkyCredentialState = state
                case .airLabs:
                    airLabsCredentialState = state
                }
            }
        }
    }

    func clearStoredAPIKeys() {
        flightAwareAPIKey = ""
        openSkyClientID = ""
        openSkyClientSecret = ""
        airLabsAPIKey = ""
    }

    func clearStoredPortableSecrets() {
        homeWiFiPassword = ""
        hotspotWiFiPassword = ""
    }

    func updatePortableDraft(
        activeProfile: DeviceWiFiProfile,
        homeSSID: String,
        homePassword: String,
        hotspotSSID: String,
        hotspotPassword: String,
        locationLabel: String,
        latitude: String,
        longitude: String,
        timezone: String,
        metarStation: String,
        radarHalfDegrees: String
    ) {
        activeDeviceWiFiProfile = activeProfile
        let trimmedHomeSSID = homeSSID.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedHomeSSID.isEmpty {
            homeWiFiSSID = trimmedHomeSSID
        }
        if !homePassword.isEmpty {
            homeWiFiPassword = homePassword
        }
        let trimmedHotspotSSID = hotspotSSID.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedHotspotSSID.isEmpty {
            hotspotWiFiSSID = trimmedHotspotSSID
        }
        if !hotspotPassword.isEmpty {
            hotspotWiFiPassword = hotspotPassword
        }
        portableLocationLabel = locationLabel
        portableLatitude = latitude
        portableLongitude = longitude
        portableTimezone = timezone
        portableMetarStation = metarStation
        portableRadarHalfDegrees = radarHalfDegrees
    }

    @discardableResult
    func refreshTrackedFlight(schedulePolling: Bool = true) async -> Bool {
        guard hasLiveTrackedFlightProvider else {
            trackedFlightTrackingActive = false
            flightStatusText = "Add a FlightAware or AirLabs key for live tracking."
            renderState = .failed(flightStatusText)
            return false
        }

        guard !flightPlans.isEmpty else {
            clearTrackedFlightPresentation()
            return false
        }
        refreshActiveFlightSelection()

        var refreshed = false
        // A completed flight can hand priority to the next scheduled flight.
        // Two passes resolve that handoff without querying the whole queue.
        for _ in 0..<2 {
            guard let plan = activePlan else { break }
            let planID = plan.id
            refreshed = await refreshFlightPlan(planID) || refreshed
            refreshActiveFlightSelection()
            if activeFlightPlanID == planID {
                break
            }
        }

        if schedulePolling && activePlan != nil {
            startTrackedFlightPolling()
        }
        return refreshed
    }

    func addFlightToSchedule() async {
        let number = Self.normalizedFlightNumber(trackedFlightNumber)
        guard !number.isEmpty else {
            flightStatusText = "Enter a flight number."
            return
        }
        trackedFlightNumber = number
        let day = Calendar.current.startOfDay(for: trackedFlightDate)
        let existingPlan = flightPlans.first {
            $0.flightNumber == number &&
                Calendar.current.isDate(
                    $0.departureDate,
                    inSameDayAs: day
                )
        }
        guard existingPlan != nil || flightPlans.count < 4 else {
            flightStatusText = "Four flights are already scheduled. Remove one before adding another."
            return
        }

        flightStatusText = "Looking up \(number)…"
        let provider = liveFlightProvider()
        if let legProvider = provider as? any FlightLegProviding {
            do {
                let candidates = try await legProvider.flightLegs(
                    for: number,
                    on: day
                )
                let ordered = Self.orderedFlightLegs(
                    candidates,
                    relativeTo: Date()
                )
                guard !ordered.isEmpty else {
                    handleFlightLookupFailure(number: number, date: day)
                    return
                }
                if ordered.count > 1 {
                    pendingFlightLegChoice = PendingFlightLegChoice(
                        marketingNumber: number,
                        departureDate: day,
                        candidates: ordered
                    )
                    flightStatusText =
                        "Choose the correct \(number) leg for \(Self.planDateFormatter.string(from: day))."
                    return
                }
                await scheduleFlightLeg(
                    number: number,
                    date: day,
                    selection: ordered[0].selection
                )
                return
            } catch FlightProviderError.noLiveFlight {
                handleFlightLookupFailure(number: number, date: day)
                return
            } catch {
                handleFlightLookupFailure(
                    number: number,
                    date: day,
                    message: error.localizedDescription
                )
                return
            }
        }

        // Retained for injected/static providers used by previews and tests.
        // It deliberately follows the same resolve-before-insert path so a
        // non-leg provider cannot bypass schedule conflict validation.
        await scheduleFlightLeg(number: number, date: day, selection: nil)
    }

    func chooseFlightLeg(_ candidate: FlightLegCandidate) async {
        guard let choice = pendingFlightLegChoice,
              choice.candidates.contains(candidate)
        else { return }
        pendingFlightLegChoice = nil
        await scheduleFlightLeg(
            number: choice.marketingNumber,
            date: choice.departureDate,
            selection: candidate.selection
        )
    }

    func dismissFlightLegChoice() {
        pendingFlightLegChoice = nil
        flightStatusText = "Flight leg selection cancelled."
    }

    func removeFlightFromSchedule(_ id: UUID) {
        flightPlans.removeAll { $0.id == id }
        resolvedFlights[id] = nil
        persistFlightPlans()
        refreshActiveFlightSelection()
        if flightPlans.isEmpty {
            clearTrackedFlightPresentation()
        }
    }

    var hasFlightToTrack: Bool {
        hasResolvedTrackedFlightPresentation
    }

    /// Populates the production Tracker workspace with deterministic, private
    /// documentation data. This never persists and is only available after a
    /// guide renderer explicitly opts into `isGuidePresentation`.
    func configureGuideTrackingPresentation() {
        guard isGuidePresentation else { return }

        let now = Date()
        let calendar = Calendar.current
        let startOfToday = calendar.startOfDay(for: now)
        let preparedAt = now.addingTimeInterval(-15 * 60)
        let identifiers = [
            "11111111-1111-4111-8111-111111111111",
            "22222222-2222-4222-8222-222222222222",
            "33333333-3333-4333-8333-333333333333",
            "44444444-4444-4444-8444-444444444444",
        ].compactMap(UUID.init(uuidString:))
        guard identifiers.count == 4 else { return }

        let departures = [
            now.addingTimeInterval(75 * 60),
            now.addingTimeInterval(18 * 3600),
            now.addingTimeInterval(32 * 3600),
            now.addingTimeInterval(47 * 3600),
        ]
        let durations: [TimeInterval] = [
            11 * 3600,
            2 * 3600,
            12 * 3600,
            2.5 * 3600,
        ]
        let routes = [
            ("YP111", "ICN", "SFO", "fa:YP111-guide"),
            ("WN982", "LAS", "SAN", "fa:WN982-guide"),
            ("OZ201", "LAX", "ICN", "fa:OZ201-guide"),
            ("AS806", "SFO", "SEA", "fa:AS806-guide"),
        ]

        flightPlans = routes.enumerated().map { index, route in
            PlannedFlight(
                id: identifiers[index],
                flightNumber: route.0,
                departureDate: calendar.startOfDay(for: departures[index]),
                addedAt: startOfToday.addingTimeInterval(Double(index)),
                scheduledDeparture: departures[index],
                estimatedArrival: departures[index]
                    .addingTimeInterval(durations[index]),
                lastRefreshed: preparedAt,
                statusSummary: index == 0
                    ? "Scheduled · active"
                    : "Scheduled · maps ready",
                mapsPreparedAt: preparedAt,
                deviceSlot: index,
                selectedOriginCode: route.1,
                selectedDestinationCode: route.2,
                selectedDeparture: departures[index],
                selectedProviderIdentifier: route.3
            )
        }

        var guideFlight = TrackedFlight.airPremiaYP111
        guideFlight.estimatedDeparture = departures[0]
        guideFlight.estimatedArrival = departures[0]
            .addingTimeInterval(durations[0])
        guideFlight.routeDistanceKilometers = 9_085
        guideFlight.originTerminal = "B"
        guideFlight.originGate = "55"
        guideFlight.destinationTerminal = "2"
        guideFlight.destinationGate = "G12"
        resolvedFlights = [identifiers[0]: guideFlight]
        activeFlightPlanID = identifiers[0]
        trackedFlight = guideFlight
        trackedFlightNumber = guideFlight.marketingNumber
        trackedFlightDate = flightPlans[0].departureDate
        flightStatusText =
            "Scheduled · four route packs prepared for the instrument"
    }

    func refreshFlightInSchedule(_ id: UUID) async {
        let refreshed = await refreshFlightPlan(id)
        refreshActiveFlightSelection()
        if let activeFlightPlanID,
           resolvedFlights[activeFlightPlanID] == nil {
            _ = await refreshTrackedFlight(schedulePolling: false)
        }
        if refreshed {
            await prepareFlightMaps(
                id,
                updatePreview: id == activeFlightPlanID
            )
        }
        startTrackedFlightPolling()
    }

    var activePlan: PlannedFlight? {
        guard let activeFlightPlanID else { return nil }
        return flightPlans.first { $0.id == activeFlightPlanID }
    }

    private var activeTrackedFlightDeviceSlot: Int {
        max(0, min(3, activePlan?.deviceSlot ?? 0))
    }

    /// A scheduled/resolved flight remains a valid device presentation even
    /// when a later provider refresh is temporarily unavailable. Live polling
    /// state must not be used as the firmware's "has a flight" flag.
    private var hasResolvedTrackedFlightPresentation: Bool {
        guard let activeFlightPlanID else { return false }
        return resolvedFlights[activeFlightPlanID] != nil
    }

    private func appendDraftFlightPlan(
        selection: FlightLegSelection?
    ) -> PlannedFlight? {
        let number = Self.normalizedFlightNumber(trackedFlightNumber)
        guard !number.isEmpty else { return nil }
        let day = Calendar.current.startOfDay(for: trackedFlightDate)
        if let existingIndex = flightPlans.firstIndex(where: {
            $0.flightNumber == number &&
                Calendar.current.isDate($0.departureDate, inSameDayAs: day)
        }) {
            if let selection {
                flightPlans[existingIndex].selectedOriginCode =
                    selection.originCode
                flightPlans[existingIndex].selectedDestinationCode =
                    selection.destinationCode
                flightPlans[existingIndex].selectedDeparture =
                    selection.scheduledDeparture
                flightPlans[existingIndex].selectedProviderIdentifier =
                    selection.providerIdentifier
                flightPlans[existingIndex].scheduledDeparture =
                    selection.scheduledDeparture
                flightPlans[existingIndex].mapsPreparedAt = nil
                flightPlans[existingIndex].statusSummary =
                    "Refreshing selected \(selection.originCode) → \(selection.destinationCode) leg"
                resolvedFlights[flightPlans[existingIndex].id] = nil
                persistFlightPlans()
            }
            return flightPlans[existingIndex]
        }

        let plan = PlannedFlight(
            flightNumber: number,
            departureDate: day,
            scheduledDeparture: selection?.scheduledDeparture,
            statusSummary: selection.map {
                "Refreshing selected \($0.originCode) → \($0.destinationCode) leg"
            } ?? "Waiting for first refresh",
            deviceSlot: firstAvailableFlightSlot(),
            selectedOriginCode: selection?.originCode,
            selectedDestinationCode: selection?.destinationCode,
            selectedDeparture: selection?.scheduledDeparture,
            selectedProviderIdentifier: selection?.providerIdentifier
        )
        flightPlans.append(plan)
        flightPlans.sort {
            if $0.departureDate == $1.departureDate {
                return $0.addedAt < $1.addedAt
            }
            return $0.departureDate < $1.departureDate
        }
        persistFlightPlans()
        return plan
    }

    private func scheduleFlightLeg(
        number: String,
        date: Date,
        selection: FlightLegSelection?
    ) async {
        trackedFlightNumber = number
        trackedFlightDate = date
        if let selection {
            flightStatusText =
                "Loading \(number) · \(selection.originCode) → \(selection.destinationCode)…"
        } else {
            flightStatusText = "Loading \(number)…"
        }
        let existingID = flightPlans.first {
            $0.flightNumber == number &&
                Calendar.current.isDate($0.departureDate, inSameDayAs: date)
        }?.id
        if let existingID,
           existingID == activeFlightPlanID,
           let existing = flightPlans.first(where: { $0.id == existingID }),
           !existing.isTerminal,
           existing.actualDeparture != nil ||
                existing.effectiveDeparture <= Date(),
           let selection,
           let currentSelection = existing.legSelection,
           selection != currentSelection {
            pendingFlightLegChoice = nil
            flightStatusText =
                "Cannot replace active \(existing.flightNumber) before it is reported landed or arrived. Remove it manually only if the provider status is wrong."
            renderState = .failed(flightStatusText)
            return
        }
        do {
            let flight = try await fetchTrackedFlight(
                number: number,
                date: date,
                selection: selection
            )
            let candidate = PlannedFlight(
                flightNumber: number,
                departureDate: date,
                scheduledDeparture: flight.scheduledDeparture ??
                    selection?.scheduledDeparture,
                actualDeparture: flight.actualDeparture,
                estimatedArrival: flight.estimatedArrival,
                actualArrival: flight.actualArrival,
                statusSummary: flight.liveStatusSummary,
                isComplete: flight.journeyPhase == "arrived"
            )
            if let conflict = PlannedFlightSchedule.conflict(
                for: candidate,
                among: flightPlans,
                excluding: existingID
            ) {
                pendingFlightLegChoice = nil
                flightStatusText = Self.flightConflictMessage(
                    candidateNumber: number,
                    candidate: candidate,
                    existing: conflict
                )
                renderState = .failed(flightStatusText)
                return
            }

            guard let plan = appendDraftFlightPlan(selection: selection),
                  let planIndex = flightPlans.firstIndex(where: {
                      $0.id == plan.id
                  })
            else { return }
            resolvedFlights[plan.id] = flight
            updatePlan(at: planIndex, with: flight)
            refreshActiveFlightSelection()
            if activeFlightPlanID == plan.id {
                activate(flight)
            } else if let activeFlightPlanID,
                      let activeFlight = resolvedFlights[activeFlightPlanID] {
                activate(activeFlight)
            }
            await prepareFlightMaps(
                plan.id,
                updatePreview: plan.id == activeFlightPlanID
            )
            startTrackedFlightPolling()
        } catch FlightProviderError.noLiveFlight {
            handleFlightLookupFailure(number: number, date: date)
        } catch {
            handleFlightLookupFailure(
                number: number,
                date: date,
                message: error.localizedDescription
            )
        }
    }

    private func fetchTrackedFlight(
        number: String,
        date: Date,
        selection: FlightLegSelection?
    ) async throws -> TrackedFlight {
        let provider = liveFlightProvider()
        if let legProvider = provider as? any FlightLegProviding {
            return try await legProvider.trackedFlight(
                for: number,
                on: date,
                matching: selection
            )
        }
        return try await provider.trackedFlight(for: number, on: date)
    }

    private func handleFlightLookupFailure(
        number: String,
        date: Date,
        message: String? = nil
    ) {
        pendingFlightLegChoice = nil
        let staleIDs = flightPlans.filter {
            $0.flightNumber == number &&
                Calendar.current.isDate($0.departureDate, inSameDayAs: date) &&
                resolvedFlights[$0.id] == nil
        }.map(\.id)
        if !staleIDs.isEmpty {
            flightPlans.removeAll { staleIDs.contains($0.id) }
            persistFlightPlans()
        }
        refreshActiveFlightSelection()
        if !hasResolvedTrackedFlightPresentation {
            resetTrackedFlightPreview()
        }
        let dateText = Self.planDateFormatter.string(from: date)
        flightStatusText = message.map {
            "Nothing found for \(number) on \(dateText): \($0)"
        } ?? "Nothing found for \(number) on \(dateText)."
        renderState = .failed(flightStatusText)
    }

    private func firstAvailableFlightSlot() -> Int {
        let used = Set(flightPlans.compactMap(\.deviceSlot))
        return (0..<4).first { !used.contains($0) } ?? 0
    }

    @discardableResult
    private func refreshFlightPlan(_ id: UUID) async -> Bool {
        guard let planIndex = flightPlans.firstIndex(where: { $0.id == id }) else {
            return false
        }
        let plan = flightPlans[planIndex]
        do {
            let flight = try await fetchTrackedFlight(
                number: plan.flightNumber,
                date: plan.departureDate,
                selection: plan.legSelection
            ).reconciledTerminalState(at: Date())
            resolvedFlights[id] = flight
            updatePlan(at: planIndex, with: flight)
            refreshActiveFlightSelection()

            if activeFlightPlanID == id {
                activate(flight)
            } else if let activeFlightPlanID,
                      let activeFlight = resolvedFlights[activeFlightPlanID] {
                activate(activeFlight)
            }
            return true
        } catch FlightProviderError.noLiveFlight {
            flightPlans[planIndex].lastRefreshed = Date()
            if let retainedFlight = resolvedFlights[id] {
                let now = Date()
                if retainedFlight.actualDeparture != nil,
                   let eta = retainedFlight.estimatedArrival,
                   now.timeIntervalSince(eta) >= 30 * 60 {
                    let arrived = retainedFlight.normalizedForTerminalState(
                        status: "Arrived",
                        at: now
                    )
                    resolvedFlights[id] = arrived
                    updatePlan(at: planIndex, with: arrived)
                    refreshActiveFlightSelection()
                    if activeFlightPlanID == id {
                        activate(arrived)
                    }
                    flightStatusText =
                        "Arrival inferred after ETA · provider no longer reports the active occurrence"
                    return true
                }
                flightPlans[planIndex].statusSummary =
                    retainedFlight.liveStatusSummary
                persistFlightPlans()
                refreshActiveFlightSelection()
                if activeFlightPlanID == id {
                    activate(retainedFlight)
                }
                flightStatusText =
                    "No fresh provider update · keeping \(retainedFlight.marketingNumber) active until a terminal status arrives"
                return false
            }
            flightPlans[planIndex].statusSummary =
                "Awaiting an explicit landed or arrived status"
            persistFlightPlans()
            refreshActiveFlightSelection()
            if !hasResolvedTrackedFlightPresentation {
                resetTrackedFlightPreview()
            }
            flightStatusText =
                "Nothing found for \(plan.flightNumber) on \(Self.planDateFormatter.string(from: plan.departureDate))."
            return false
        } catch {
            if let retainedFlight = resolvedFlights[id] {
                flightPlans[planIndex].lastRefreshed = Date()
                flightPlans[planIndex].statusSummary =
                    retainedFlight.liveStatusSummary
                persistFlightPlans()
                refreshActiveFlightSelection()
                if activeFlightPlanID == id {
                    activate(retainedFlight)
                }
                flightStatusText =
                    "Refresh unavailable · showing the last \(retainedFlight.dataSourceSummary) update"
                return false
            }
            flightPlans[planIndex].lastRefreshed = Date()
            flightPlans[planIndex].statusSummary = error.localizedDescription
            resolvedFlights[id] = nil
            persistFlightPlans()
            refreshActiveFlightSelection()
            if !hasResolvedTrackedFlightPresentation {
                resetTrackedFlightPreview()
            }
            renderState = .failed(
                "Could not refresh \(plan.flightNumber): \(error.localizedDescription)"
            )
            flightStatusText = error.localizedDescription
            return false
        }
    }

    private func updatePlan(at index: Int, with flight: TrackedFlight) {
        flightPlans[index].scheduledDeparture = flight.scheduledDeparture
        flightPlans[index].actualDeparture = flight.actualDeparture
        flightPlans[index].estimatedArrival = flight.estimatedArrival
        flightPlans[index].actualArrival = flight.actualArrival
        flightPlans[index].lastRefreshed = Date()
        flightPlans[index].statusSummary = flight.liveStatusSummary
        flightPlans[index].isComplete =
            flight.journeyPhase == "arrived" ||
            flight.actualArrival != nil ||
            flight.liveStatusSummary.localizedCaseInsensitiveContains(
                "cancelled"
            ) ||
            flight.liveStatusSummary.localizedCaseInsensitiveContains(
                "canceled"
            )
        persistFlightPlans()
    }

    private func activate(_ flight: TrackedFlight) {
        let displayFlight = flight.reconciledTerminalState(at: Date())
        trackedFlight = displayFlight
        trackedFlightTrackingActive =
            displayFlight.isActivelyTrackable ||
            displayFlight.journeyPhase == "arrived"
        routeProgress = displayFlight.journeyPhase == "arrived"
            ? 1
            : (displayFlight.routeProgress ?? 0)
        flightStatusText =
            "\(displayFlight.liveStatusSummary) · \(displayFlight.dataSourceSummary)"
    }

    private func refreshActiveFlightSelection() {
        let now = Date()
        let expiredIDs = Set(
            flightPlans
                .filter { $0.isExpiredAfterArrival(at: now) }
                .map(\.id)
        )
        if !expiredIDs.isEmpty {
            flightPlans.removeAll { expiredIDs.contains($0.id) }
            for id in expiredIDs {
                resolvedFlights[id] = nil
            }
            persistFlightPlans()
        }
        let selected = PlannedFlightPriority.select(
            from: flightPlans,
            currentPlanID: activeFlightPlanID,
            at: now
        )
        activeFlightPlanID = selected?.id
        if let id = selected?.id, let flight = resolvedFlights[id] {
            activate(flight)
        } else {
            resetTrackedFlightPreview()
            flightStatusText = "Nothing to track. Add a flight number and date to begin."
        }
        persistSharedConfiguration()
    }

    private func resetTrackedFlightPreview() {
        trackedFlightTrackingActive = false
        routeProgress = 0
        latestAirlineLogoImage = nil
        latestMapImage = nil
        latestGeodesicMapImage = nil
        latestRouteFrameMapImage = nil
        trackedFlightPollingText = "Nothing to track"
    }

    private func clearTrackedFlightPresentation() {
        activeFlightPlanID = nil
        trackedFlightTrackingActive = false
        trackedFlightPollingTask?.cancel()
        trackedFlightPollingTask = nil
        airlineLogoTask?.cancel()
        airlineLogoTask = nil
        resolvedFlights.removeAll()
        routeProgress = 0
        latestAirlineLogoImage = nil
        trackedFlightPollingText = "Nothing to track"
        flightStatusText = "Nothing to track. Add a flight number and date to begin."
        renderState = .idle
        UserDefaults.standard.set(
            false,
            forKey: DefaultsKey.trackedFlightMonitoringActive.rawValue
        )
    }

    private func startTrackedFlightPolling() {
        trackedFlightPollingTask?.cancel()
        guard hasLiveTrackedFlightProvider else {
            trackedFlightPollingText = "Polling off · no tracked-flight provider"
            return
        }

        trackedFlightPollingTask = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                guard let cadence = self.nextTrackedFlightPollCadence() else {
                    self.refreshActiveFlightSelection()
                    if self.activePlan != nil {
                        self.startTrackedFlightPolling()
                        return
                    }
                    self.trackedFlightPollingText = "Nothing to track · polling complete"
                    UserDefaults.standard.set(
                        false,
                        forKey: DefaultsKey.trackedFlightMonitoringActive.rawValue
                    )
                    return
                }

                self.trackedFlightPollingText = cadence.label
                do {
                    try await Task.sleep(
                        nanoseconds: UInt64(max(1, cadence.delay) * 1_000_000_000)
                    )
                } catch {
                    return
                }
                guard !Task.isCancelled else { return }

                if cadence.shouldFetch {
                    let previousPlanID = self.activeFlightPlanID
                    let refreshed = await self.refreshTrackedFlight(schedulePolling: false)
                    if refreshed {
                        if self.activePlan == nil {
                            await self.pushTrackedFlightStateOnly()
                        } else if previousPlanID != self.activeFlightPlanID,
                                  self.trackedFlightTrackingActive {
                            await self.pushTrackedFlightPackageForAutomaticHandoff()
                        } else if self.trackedFlightTrackingActive {
                            await self.pushTrackedFlightStateOnly()
                        }
                    }
                }
            }
        }
    }

    private func nextTrackedFlightPollCadence() -> (
        delay: TimeInterval,
        shouldFetch: Bool,
        label: String
    )? {
        if let plan = activePlan, resolvedFlights[plan.id] == nil {
            return (
                1,
                true,
                "Fetching \(plan.flightNumber)…"
            )
        }
        if trackedFlight.journeyPhase == "arrived" {
            guard let expiry = trackedFlight.postArrivalExpiryDate(),
                  expiry > Date()
            else { return nil }
            return (
                max(1, expiry.timeIntervalSinceNow),
                false,
                "Landed · available for two hours"
            )
        }
        if !trackedFlightTrackingActive {
            guard let plan = activePlan, !plan.isComplete else {
                return nil
            }
            let untilDeparture = plan.effectiveDeparture.timeIntervalSinceNow
            if untilDeparture > 24 * 3600 {
                let wait = min(
                    6 * 3600,
                    max(60, untilDeparture - 24 * 3600)
                )
                return (
                    wait,
                    false,
                    "Scheduled · waiting until the 24-hour window"
                )
            }
            if untilDeparture > 2 * 3600 {
                return (
                    3600,
                    true,
                    "Schedule pending · checking hourly"
                )
            }
            return (
                600,
                true,
                "Schedule pending · checking every 10 minutes"
            )
        }
        if trackedFlight.actualDeparture != nil {
            return (60, true, "Airborne · updates every minute")
        }

        guard let departure = trackedFlight.scheduledDeparture else {
            return (600, true, "Schedule pending · checking every 10 minutes")
        }
        let untilDeparture = departure.timeIntervalSinceNow
        if untilDeparture > 24 * 3600 {
            let wait = min(6 * 3600, max(60, untilDeparture - 24 * 3600))
            return (wait, false, "Outside 24 hours · no automatic API calls")
        }
        if untilDeparture > 2 * 3600 {
            return (3600, true, "Within 24 hours · updates hourly")
        }
        return (600, true, "Within 2 hours · updates every 10 minutes")
    }

    private func pushTrackedFlightStateOnly() async {
        do {
            guard hasResolvedTrackedFlightPresentation else {
                throw CompanionSyncError.unresolvedTrackedFlight
            }
            if let port = DeviceSerialDiscovery.preferredPort() {
                let data = try deviceClient.encodedState(
                    style: .cartographicGlass,
                    routeProgress: routeProgress,
                    flight: trackedFlight,
                    trackingActive: true,
                    deviceSlot: activeTrackedFlightDeviceSlot
                )
                try await usbConfigurationClient.pushState(
                    data: data,
                    port: port,
                    expectedFlightNumber: trackedFlight.marketingNumber
                )
                deviceReachabilityTransport = .usb
                deviceStatusText = "Flight state updated over USB-C"
            } else {
                try await deviceClient.pushState(
                    host: deviceHost,
                    style: .cartographicGlass,
                    routeProgress: routeProgress,
                    flight: trackedFlight,
                    trackingActive: true,
                    deviceSlot: activeTrackedFlightDeviceSlot
                )
                deviceStatusText = try await deviceClient.fetchStatus(host: deviceHost)
            }
        } catch {
            trackedFlightPollingText =
                "Live data updated · device push failed: \(error.localizedDescription)"
        }
    }

    private func pushTrackedFlightPackageForAutomaticHandoff() async {
        do {
            // App launch and the periodic monitor both pass through this path.
            // Do not resend a complete route pack when the device already has
            // the active flight's two maps and two flags; only the compact
            // flight state needs refreshing in that case.
            var routePackAlreadyPresent = false
            if DeviceSerialDiscovery.preferredPort() == nil,
               let snapshot = try? await deviceClient.fetchStatusSnapshot(
                   host: deviceHost
               ) {
                routePackAlreadyPresent =
                    snapshot.trackedFlightValid &&
                    snapshot.routeMapMask == 3 &&
                    snapshot.routeFlagMask == 3 &&
                    Self.normalizedFlightNumber(snapshot.trackedFlightNumber) ==
                        Self.normalizedFlightNumber(trackedFlight.marketingNumber)
            }

            let sentStateOverUSB = routePackAlreadyPresent
                ? false
                : try await pushTrackedFlightMapsAndFlags()
            if !sentStateOverUSB {
                if routePackAlreadyPresent {
                    syncProgress = 0.96
                    pushState = .pushing(
                        "Route maps already stored · updating flight state…"
                    )
                } else {
                    syncProgress = 0.97
                    pushState = .pushing(
                        "Wi-Fi transfer complete · sending screen state…"
                    )
                }
                try await deviceClient.pushState(
                    host: deviceHost,
                    style: .cartographicGlass,
                    routeProgress: routeProgress,
                    flight: trackedFlight,
                    trackingActive: true,
                    deviceSlot: activeTrackedFlightDeviceSlot
                )
            }
            syncProgress = nil
            pushState = .pushed(
                routePackAlreadyPresent
                    ? "Flight state updated · stored maps retained"
                    : "Tracked flight, maps, and flags synchronized"
            )
            trackedFlightPollingText =
                "Showing \(trackedFlight.marketingNumber) · previous flight completed"
        } catch {
            syncProgress = nil
            pushState = .failed(
                "Automatic flight handoff failed: \(error.localizedDescription)"
            )
            trackedFlightPollingText =
                "Flight handoff · device sync failed: \(error.localizedDescription)"
        }
    }

    private func resumeTrackedFlightMonitoring() async {
        guard hasLiveTrackedFlightProvider else {
            trackedFlightPollingText =
                "Saved flight · add FlightAware or AirLabs to resume updates"
            return
        }
        trackedFlightPollingText = "Resuming saved flight…"
        if await refreshTrackedFlight(schedulePolling: false) {
            await pushTrackedFlightPackageForAutomaticHandoff()
            startTrackedFlightPolling()
        }
    }

    func renderRouteMap() async {
        renderState = .rendering
        do {
            _ = try await renderRouteMapAsset()
        } catch {
            renderState = .failed("Map render failed: \(error.localizedDescription)")
        }
    }

    func renderLocalRadarMap() async {
        renderState = .rendering
        do {
            _ = try await renderLocalRadarMapAssets()
        } catch {
            renderState = .failed("Local radar map failed: \(error.localizedDescription)")
        }
    }

    func pushToDevice() async {
        pushState = .pushing(selectedStyle == .xdrRadar ? "Switching to stored radar…" : "Preparing tracked-flight map…")
        syncProgress = selectedStyle == .cartographicGlass ? 0.02 : nil
        do {
            if selectedStyle == .cartographicGlass && flightPlans.isEmpty {
                pushState = .pushing("Clearing the tracked flight…")
                let usbPort = DeviceSerialDiscovery.preferredPort()
                if let usbPort {
                    let clearState = try deviceClient.encodedState(
                        style: selectedStyle,
                        routeProgress: 0,
                        flight: trackedFlight,
                        trackingActive: false,
                        deviceSlot: 0
                    )
                    try await usbConfigurationClient.pushState(
                        data: clearState,
                        port: usbPort,
                        expectedClearedFlight: true
                    )
                } else {
                    try await deviceClient.clearTrackedFlight(
                        host: deviceHost,
                        style: selectedStyle
                    )
                }
                UserDefaults.standard.set(
                    false,
                    forKey: DefaultsKey.trackedFlightMonitoringActive.rawValue
                )
                trackedFlightPollingTask?.cancel()
                if usbPort == nil {
                    let snapshot = try await deviceClient.fetchStatusSnapshot(
                        host: deviceHost
                    )
                    guard !snapshot.trackedFlightValid else {
                        throw CompanionSyncError.trackedFlightDidNotClear
                    }
                    deviceStatusText = try await deviceClient.fetchStatus(
                        host: deviceHost
                    )
                } else {
                    deviceReachabilityTransport = .usb
                    deviceStatusText = "Tracked flight cleared over USB-C"
                }
                pushState = .pushed("Nothing to track · retained the latest route map")
                syncProgress = nil
                return
            }

            if selectedStyle == .cartographicGlass &&
                !hasResolvedTrackedFlightPresentation {
                throw CompanionSyncError.unresolvedTrackedFlight
            }

            var sentStateOverUSB = false
            if selectedStyle == .cartographicGlass {
                sentStateOverUSB = try await pushTrackedFlightMapsAndFlags()
            }

            if !sentStateOverUSB {
                if selectedStyle == .cartographicGlass {
                    syncProgress = 0.97
                    pushState = .pushing("Wi-Fi transfer complete · sending screen state…")
                } else {
                    pushState = .pushing("Sending screen state…")
                }
                try await deviceClient.pushState(
                    host: deviceHost,
                    style: selectedStyle,
                    routeProgress: routeProgress,
                    flight: trackedFlight,
                    trackingActive: true,
                    deviceSlot: activeTrackedFlightDeviceSlot
                )
            }
            if selectedStyle == .cartographicGlass {
                if !sentStateOverUSB {
                    syncProgress = 0.99
                    pushState = .pushing("Wi-Fi transfer complete · verifying device…")
                    let snapshot = try await deviceClient.fetchStatusSnapshot(
                        host: deviceHost
                    )
                    guard snapshot.trackedFlightValid,
                          snapshot.routeMapMask == 3,
                          Self.normalizedFlightNumber(
                            snapshot.trackedFlightNumber
                          ) == Self.normalizedFlightNumber(
                            trackedFlight.marketingNumber
                          )
                    else {
                        throw CompanionSyncError.incompleteTrackedFlightSync(
                            expectedFlight: trackedFlight.marketingNumber,
                            receivedFlight: snapshot.trackedFlightNumber,
                            mapMask: snapshot.routeMapMask
                        )
                    }
                }
                UserDefaults.standard.set(
                    true,
                    forKey: DefaultsKey.trackedFlightMonitoringActive.rawValue
                )
                startTrackedFlightPolling()
            }
            if sentStateOverUSB {
                deviceReachabilityTransport = .usb
                deviceStatusText = "Tracked flight, maps, and flags updated over USB-C"
                pushState = .pushed("Pushed \(selectedStyle.title) over USB-C")
            } else {
                deviceStatusText = try await deviceClient.fetchStatus(host: deviceHost)
                pushState = .pushed("Pushed \(selectedStyle.title) to \(deviceHost)")
            }
            syncProgress = nil
        } catch {
            FileHandle.standardError.write(
                Data("Tracked-flight sync failed: \(error.localizedDescription)\n".utf8)
            )
            pushState = .failed("Push failed: \(error.localizedDescription)")
            syncProgress = nil
        }
    }

    func syncScreenToDevice(style: InstrumentStyle) async {
        selectedStyle = style
        if style == .launch {
            await syncNextLaunchToDevice()
            return
        }
        if style == .cartographicGlass {
            if flightPlans.isEmpty {
                trackedFlightTrackingActive = false
                await pushToDevice()
                return
            }
            _ = await refreshTrackedFlight()
            guard hasResolvedTrackedFlightPresentation else {
                pushState = .failed(flightStatusText)
                return
            }
        }
        await pushToDevice()
    }

    /// Sends tracker maps, flags, and state as one verified operation. This
    /// deliberately bypasses general config sync so a Wi-Fi reconnect cannot
    /// interrupt the selected-flight transfer.
    func syncTrackedFlightsToDevice() async {
        selectedStyle = .cartographicGlass
        if flightPlans.isEmpty {
            trackedFlightTrackingActive = false
            await pushToDevice()
            return
        }

        pushState = .pushing("Preparing scheduled flights…")
        for plan in flightPlans where resolvedFlights[plan.id] == nil {
            _ = await refreshFlightPlan(plan.id)
        }
        refreshActiveFlightSelection()
        guard hasResolvedTrackedFlightPresentation else {
            pushState = .failed(flightStatusText)
            return
        }
        await pushToDevice()
    }

    @discardableResult
    func refreshNextLaunch() async -> Bool {
        launchStatusText =
            "Checking NASA, Roscosmos, and Starship…"
        do {
            let launches =
                try await launchScheduleProvider
                    .fetchNextLaunches()
            let filtered = launches.filter {
                selectedLaunchAgencies.contains($0.agency)
            }
            guard let launch = filtered.min(
                by: { $0.net < $1.net }
            ) else {
                throw LaunchScheduleError.noMatchingLaunch
            }
            nextLaunches = filtered
            nextLaunch = launch
            if let data = try? JSONEncoder().encode(
                launch
            ) {
                UserDefaults.standard.set(
                    data,
                    forKey:
                        DefaultsKey.nextLaunch.rawValue
                )
            }
            if let data = try? JSONEncoder().encode(
                filtered
            ) {
                UserDefaults.standard.set(
                    data,
                    forKey:
                        DefaultsKey.nextLaunches.rawValue
                )
            }
            let formatter = RelativeDateTimeFormatter()
            formatter.unitsStyle = .full
            launchStatusText =
                "\(launch.statusName) · " +
                formatter.localizedString(
                    for: launch.net,
                    relativeTo: Date()
                )
            return true
        } catch {
            if nextLaunch.id !=
                TrackedLaunch.preview.id {
                launchStatusText =
                    "Schedule unavailable · using the last saved launch"
                return true
            }
            launchStatusText =
                "Launch schedule failed: " +
                error.localizedDescription
            return false
        }
    }

    func syncNextLaunchToDevice() async {
        selectedStyle = .launch
        pushState = .pushing(
            "Refreshing the next launch…"
        )
        guard await refreshNextLaunch() else {
            pushState = .failed(launchStatusText)
            return
        }

        do {
            let snapshot =
                try await deviceClient
                    .fetchStatusSnapshot(
                        host: deviceHost
                    )
            let installedMask =
                snapshot.launchAssetMask ?? 0
            let assets: [(String, String)] = [
                ("launch_sls", "rga"),
                ("launch_falcon9", "rga"),
                ("launch_soyuz", "rga"),
                ("launch_starship", "rga"),
                ("launch_spacex", "rga"),
                ("launch_florida", "rgb"),
                ("launch_starbase", "rgb"),
                ("launch_baikonur", "rgb"),
            ]
            for (index, asset) in
                assets.enumerated()
            where
                installedMask & (1 << index) == 0 {
                pushState = .pushing(
                    "Installing launch artwork " +
                    "\(index + 1)/\(assets.count)…"
                )
                let url =
                    ProjectAssetCatalog.assetRoot
                        .appendingPathComponent(
                            "space/\(asset.0).\(asset.1)"
                        )
                let data = try Data(
                    contentsOf: url
                )
                try await deviceClient
                    .pushSpaceAsset(
                        host: deviceHost,
                        name: asset.0,
                        data: data
                    )
            }

            pushState = .pushing(
                "Sending \(nextLaunch.missionName)…"
            )
            try await deviceClient.pushLaunchState(
                host: deviceHost,
                launch: nextLaunch,
                launches: nextLaunches
            )
            deviceStatusText =
                try await deviceClient.fetchStatus(
                    host: deviceHost
                )
            pushState = .pushed(
                "Launch screens ready on \(deviceHost)"
            )
        } catch {
            pushState = .failed(
                "Launch sync failed: " +
                error.localizedDescription
            )
        }
    }

    func pushLocalRadarMapToDevice() async {
        pushState = .pushing("Preparing all city radar map packs…")
        do {
            selectedStyle = .xdrRadar
            try await pushAllLocalRadarMaps()
            pushState = .pushing("Sending radar screen state…")
            try await deviceClient.pushState(
                host: deviceHost,
                style: .xdrRadar,
                routeProgress: routeProgress,
                flight: trackedFlight
            )
            deviceStatusText = try await deviceClient.fetchStatus(host: deviceHost)
            pushState = .pushed("Pushed all city radar maps to \(deviceHost)")
        } catch {
            pushState = .failed("City radar map push failed: \(error.localizedDescription)")
        }
    }

    func refreshLiveAndPushToDevice() async {
        guard await refreshTrackedFlight() else {
            pushState = .failed(flightStatusText)
            return
        }
        await pushToDevice()
    }

    func refreshDeviceStatus(announce: Bool = true) async {
        guard !deviceStatusRefreshInFlight else { return }
        deviceStatusRefreshInFlight = true
        defer { deviceStatusRefreshInFlight = false }
        let connectedUSBPort = DeviceSerialDiscovery.preferredPort()

        do {
            let snapshot = try await deviceClient.fetchStatusSnapshot(host: deviceHost)
            applyDeviceStatus(snapshot)
            if let connectedUSBPort {
                deviceReachabilityTransport = .usb
                deviceStatusText =
                    "Device connected over USB-C at \(connectedUSBPort)"
            } else {
                deviceStatusText =
                    "Device reachable at \(snapshot.ip ?? deviceHost)"
            }
            if announce {
                pushState = .pushed(
                    connectedUSBPort == nil
                        ? "Device reachable over Wi-Fi"
                        : "Device connected over USB-C"
                )
            }
            return
        } catch let wifiError {
            guard let port = connectedUSBPort else {
                deviceReachable = false
                deviceReachabilityTransport = .offline
                if announce {
                    pushState = .failed(
                        "Status failed: \(wifiError.localizedDescription)"
                    )
                }
                return
            }

            // Enumeration is the authoritative signal that the tracker is
            // physically connected. The firmware may be busy rendering or
            // networking and therefore not answer an ASTCFG probe promptly;
            // that must not make a plugged-in tracker appear Offline.
            deviceReachable = true
            deviceReachabilityTransport = .usb
            deviceStatusText = "Device connected over USB-C at \(port)"
            if announce {
                pushState = .pushed("Device connected over USB-C")
            }
        }
    }

    func selectRadarLocation(_ location: RadarLocationPreset) {
        selectedRadarPreviewLocationKey = location.key
        companionAircraftSnapshot = nil
        companionWeatherSnapshot = nil
        latestLocalRadarMapImage = nil
        portableLatitude = "\(location.coordinate.latitude)"
        portableLongitude = "\(location.coordinate.longitude)"
        portableTimezone = location.timezone
        portableMetarStation = location.metar
        portableRadarHalfDegrees = location.radarHalfDegrees
        portableLocationLabel = location.title
        Task { @MainActor [weak self] in
            guard let self else { return }
            async let map: Void = self.prepareCompanionRadarPreview()
            async let aircraft: Void = self.refreshCompanionAircraft()
            async let weather: Void = self.refreshCompanionWeather()
            _ = await (map, aircraft, weather)
        }
    }

    func prepareCompanionRadarPreview() async {
        loadCachedLocalRadarMap()
        guard latestLocalRadarMapImage == nil,
              let location = radarLocation(for: selectedRadarLocationKey) else {
            return
        }
        do {
            _ = try await renderLocalRadarMapAsset(
                location: location,
                radiusMiles: 20
            )
        } catch {
            if latestLocalRadarMapImage == nil {
                renderState = .failed(error.localizedDescription)
            }
        }
    }

    func refreshCompanionAircraft() async {
        guard let location = radarLocation(for: selectedRadarLocationKey) else {
            companionAircraftSnapshot = nil
            companionAircraftStatusText = "Select a Radar city"
            return
        }
        let requestedLocationKey = location.key
        do {
            let snapshot = try await previewDataClient.fetchAircraft(
                location: location,
                radiusMiles: radarRadiusMiles,
                clientID: openSkyClientID,
                clientSecret: openSkyClientSecret
            )
            guard selectedRadarLocationKey.caseInsensitiveCompare(
                requestedLocationKey
            ) == .orderedSame else {
                return
            }
            companionAircraftSnapshot = snapshot
            companionAircraftStatusText = snapshot.aircraftValid
                ? "Live · \(snapshot.aircraft.count) aircraft"
                : "Device is refreshing aircraft"
        } catch {
            guard selectedRadarLocationKey.caseInsensitiveCompare(
                requestedLocationKey
            ) == .orderedSame else {
                return
            }
            companionAircraftSnapshot = nil
            companionAircraftStatusText =
                "Aircraft unavailable: \(error.localizedDescription)"
        }
    }

    func refreshCompanionWeather() async {
        guard let location = radarLocation(for: selectedRadarLocationKey) else {
            companionWeatherSnapshot = nil
            companionWeatherStatusText = "Select a Weather city"
            return
        }
        let requestedLocationKey = location.key
        do {
            let snapshot = try await previewDataClient.fetchWeather(
                location: location
            )
            guard selectedRadarLocationKey.caseInsensitiveCompare(
                requestedLocationKey
            ) == .orderedSame else {
                return
            }
            companionWeatherSnapshot = snapshot
            companionWeatherStatusText = snapshot.weatherValid
                ? "Live · \(snapshot.locationKey)"
                : "Device is refreshing weather"
        } catch {
            guard selectedRadarLocationKey.caseInsensitiveCompare(
                requestedLocationKey
            ) == .orderedSame else {
                return
            }
            companionWeatherSnapshot = nil
            companionWeatherStatusText =
                "Weather unavailable: \(error.localizedDescription)"
        }
    }

    func setLaunchAgency(_ agency: LaunchAgency, enabled: Bool) {
        if enabled {
            selectedLaunchAgencies.insert(agency)
        } else if selectedLaunchAgencies.count > 1 {
            selectedLaunchAgencies.remove(agency)
        }
        Task { @MainActor [weak self] in
            _ = await self?.refreshNextLaunch()
        }
    }

    func recordCityArtworkChange() {
        cityArtworkRevision &+= 1
    }

    func installCityIcon(from url: URL, for city: RadarLocationPreset) async {
        guard let image = NSImage(contentsOf: url) else {
            pushState = .failed("The selected city icon could not be opened")
            return
        }
        do {
            try ProjectAssetCatalog.installCityImage(image, for: city.key)
            cityArtworkRevision &+= 1
            let data = try ImageWriter.rgb565AlphaData(
                from: image,
                width: 128,
                height: 128
            )
            try await deviceClient.pushWeatherCityIcon(
                host: deviceHost,
                data: data,
                locationKey: city.key
            )
            deviceLastSyncedAt = Date()
            pushState = .pushed("Updated \(city.title) artwork")
        } catch {
            pushState = .failed("City icon update failed: \(error.localizedDescription)")
        }
    }

    private func applyDeviceStatus(_ snapshot: DeviceClient.StatusSnapshot) {
        deviceReachable = snapshot.ok ?? true
        deviceReachabilityTransport = deviceReachable ? .wifi : .offline
        if snapshot.batteryAvailable == true,
           snapshot.batteryPresent == true,
           let percent = snapshot.batteryPercent,
           (0...100).contains(percent) {
            deviceBatteryPercent = percent
            deviceBatteryCharging = snapshot.batteryCharging ?? false
        } else {
            deviceBatteryPercent = nil
            deviceBatteryCharging = false
        }
        if let unix = snapshot.lastPushUnix, unix > 0 {
            deviceLastSyncedAt = Date(timeIntervalSince1970: TimeInterval(unix))
        }
    }

    func refreshDeviceLiveData() async {
        pushState = .pushing("Requesting ESP32 live data refresh…")
        do {
            _ = try await deviceClient.refreshDeviceData(host: deviceHost)
            try await Task.sleep(nanoseconds: 900_000_000)
            deviceStatusText = try await deviceClient.fetchStatus(host: deviceHost)
            pushState = .pushed("Requested ESP32 live data refresh")
        } catch {
            pushState = .failed("Refresh failed: \(error.localizedDescription)")
        }
    }

    @discardableResult
    func pushPortableConfigToDevice() async -> Bool {
        configPushState = .pushing("Synchronizing config with ESP32…")
        configPushRequiresReconnect = false
        deviceStatusText = "Reading device config from \(deviceHost)…"
        do {
            /* FlightAware stays Mac-only, so the device receives the result
             * of a real no-cost authorization check rather than a proxy for
             * "the field is non-empty." OpenSky and AirLabs are rechecked at
             * the same time and again directly by the portable device. */
            markAPICredentialsForValidation()
            await validateAPICredentials()

            /* A physical USB-C connection is authoritative. Do not probe
             * Wi-Fi first when the board is attached: the USB/JTAG channel is
             * faster, local, and is the only deterministic transport while a
             * Wi-Fi profile itself is being changed. */
            let usbPort = DeviceSerialDiscovery.preferredPort()
            let fetched: SharedConfiguration
            if let usbPort {
                configPushState = .pushing(
                    "Reading device config over USB-C…"
                )
                deviceStatusText = "Reading device config from \(usbPort)…"
                fetched = try await usbConfigurationClient.fetchConfig(
                    port: usbPort
                )
            } else {
                fetched = try await deviceClient.fetchConfig(
                    host: deviceHost
                )
            }
            let deviceChanged = !hasDeviceSnapshot ||
                fetched.device.revision != lastSyncedDeviceRevision
            if deviceChanged {
                applyDeviceOwnedConfiguration(fetched.device)
            }

            let homeSSID = homeWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines)
            let hotspotSSID = hotspotWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines)
            let homePassword = homeWiFiPassword
            let hotspotPassword = hotspotWiFiPassword
            let locationPreset = radarLocation(
                label: portableLocationLabel,
                metar: portableMetarStation
            )
            let openSkyID = openSkyClientID.trimmingCharacters(
                in: .whitespacesAndNewlines
            )
            let openSkySecret = openSkyClientSecret.trimmingCharacters(
                in: .whitespacesAndNewlines
            )

            let companion = sharedCompanionConfiguration(
                deviceRevision: fetched.device.revision
            )
            let payload = DeviceClient.DeviceConfigPayload(
                schemaVersion: SharedConfiguration.schemaVersion,
                baseDeviceRevision: fetched.device.revision,
                device: .init(
                    activeWifiProfile: activeDeviceWiFiProfile.rawValue,
                    wifiProfiles: .init(
                        home: .init(
                            ssid: homeSSID.isEmpty ? nil : homeSSID,
                            password: homePassword.isEmpty ? nil : homePassword
                        ),
                        hotspot: .init(
                            ssid: hotspotSSID.isEmpty ? nil : hotspotSSID,
                            password: hotspotPassword.isEmpty ? nil : hotspotPassword
                        )
                    ),
                    location: .init(
                        key: locationPreset?.key,
                        label: locationPreset?.title ?? portableLocationLabel.trimmingCharacters(in: .whitespacesAndNewlines),
                        lat: locationPreset.map { "\($0.coordinate.latitude)" } ?? portableLatitude.trimmingCharacters(in: .whitespacesAndNewlines),
                        lon: locationPreset.map { "\($0.coordinate.longitude)" } ?? portableLongitude.trimmingCharacters(in: .whitespacesAndNewlines),
                        timezone: locationPreset?.timezone ?? portableTimezone.trimmingCharacters(in: .whitespacesAndNewlines),
                        metar: locationPreset?.metar ?? portableMetarStation.trimmingCharacters(in: .whitespacesAndNewlines).uppercased(),
                        radarHalfDegrees: locationPreset?.radarHalfDegrees ?? portableRadarHalfDegrees.trimmingCharacters(in: .whitespacesAndNewlines)
                    ),
                    display: .init(
                        aircraftLabels: aircraftLabelsEnabled,
                        radarWedge: radarWedgeEnabled,
                        issFlags: issFlagsEnabled,
                        weatherUnits: weatherDisplayUnits.rawValue,
                        accentColor: accentColor.rawValue,
                        accentColors: customAccentColors
                    ),
                    radarRadiusMiles: radarRadiusMiles
                ),
                credentials: .init(
                    openSky: .init(
                        enabled: true,
                        clientId: openSkyID.isEmpty ? nil : openSkyID,
                        clientSecret: openSkySecret.isEmpty
                            ? nil
                            : openSkySecret
                    ),
                    airLabs: .init(
                        apiKey: airLabsAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                            ? nil
                            : airLabsAPIKey.trimmingCharacters(in: .whitespacesAndNewlines)
                    ),
                    flightAware: .init(
                        available: flightAwareCredentialState.isValid
                    )
                ),
                companion: companion
            )

            let response: SharedConfiguration
            if let usbPort {
                configPushState = .pushing("Synchronizing config over USB-C…")
                response = try await usbConfigurationClient.pushConfig(
                    payload: payload,
                    port: usbPort
                )
            } else {
                response = try await deviceClient.pushConfig(
                    host: deviceHost,
                    payload: payload
                )
            }
            if response.conflict {
                applyDeviceOwnedConfiguration(response.device)
            }
            lastSyncedDeviceRevision = response.device.revision
            hasDeviceSnapshot = true
            if usbPort == nil {
                let status = try await deviceClient.fetchStatusSnapshot(
                    host: deviceHost
                )
                if let mask = status.spaceAssetFileMask,
                   mask & (1 << 15) == 0 {
                    configPushState = .pushing(
                        "Installing ISS partner flags…"
                    )
                    let url = ProjectAssetCatalog.assetRoot
                        .appendingPathComponent(
                            "space/iss_flags.rga"
                        )
                    try await deviceClient.pushSpaceAsset(
                        host: deviceHost,
                        name: "iss_flags",
                        data: Data(contentsOf: url)
                    )
                }
            }
            var saved = response
            saved.companion = sharedCompanionConfiguration(
                deviceRevision: response.device.revision
            )
            try SharedConfigurationStore.save(saved)
            let transport = usbPort == nil ? "Wi‑Fi" : "USB-C"
            deviceStatusText = "Config synchronized over \(transport) · device revision \(response.device.revision)"
            deviceReachable = true
            deviceReachabilityTransport = usbPort == nil ? .wifi : .usb
            deviceLastSyncedAt = Date()
            configPushRequiresReconnect = response.reconnecting
            if configPushRequiresReconnect {
                configPushState = .pushed("Config accepted; ESP32 is reconnecting Wi‑Fi")
            } else if response.conflict {
                configPushState = .pushed("Device settings refreshed; the Mac flight schedule and clock cities remained authoritative")
            } else {
                configPushState = .pushed("Device and companion config synchronized over \(transport)")
            }
            return true
        } catch {
            configPushState = .failed("Config push failed: \(error.localizedDescription)")
            configPushRequiresReconnect = false
            return false
        }
    }

    @discardableResult
    func pushPortableConfigToDevice(
        activeProfile: DeviceWiFiProfile,
        homeSSID: String,
        homePassword: String,
        hotspotSSID: String,
        hotspotPassword: String,
        locationLabel: String,
        latitude: String,
        longitude: String,
        timezone: String,
        metarStation: String,
        radarHalfDegrees: String
    ) async -> Bool {
        updatePortableDraft(
            activeProfile: activeProfile,
            homeSSID: homeSSID,
            homePassword: homePassword,
            hotspotSSID: hotspotSSID,
            hotspotPassword: hotspotPassword,
            locationLabel: locationLabel,
            latitude: latitude,
            longitude: longitude,
            timezone: timezone,
            metarStation: metarStation,
            radarHalfDegrees: radarHalfDegrees
        )
        return await pushPortableConfigToDevice()
    }

    func testOpenSkyNearbyAircraft(
        latitude: String,
        longitude: String,
        radarHalfDegrees: String
    ) async {
        nearbyAircraftTestText = "Testing OpenSky…"
        do {
            let result = try await openSkyDiagnosticClient.testNearby(
                clientID: openSkyClientID,
                clientSecret: openSkyClientSecret,
                latitude: latitude,
                longitude: longitude,
                halfDegrees: radarHalfDegrees
            )
            nearbyAircraftTestText = result.summary
        } catch {
            nearbyAircraftTestText = "OpenSky test failed: \(error.localizedDescription)"
        }
    }

    /// Returns true when the compact tracked-flight state was included in a
    /// complete USB-C package. Wi-Fi callers still send state separately.
    private func pushTrackedFlightMapsAndFlags() async throws -> Bool {
        let packages = try await preparedFlightPackagesForPush()
        guard !packages.isEmpty else {
            throw CocoaError(.fileNoSuchFile)
        }

        // Normal app operation always prefers the direct USB-C transport. The
        // environment override exists only so the release/test CLI can verify
        // the identical package over Wi-Fi while a board remains plugged in.
        let forceWiFiForDiagnostics = ProcessInfo.processInfo.environment[
            "AIRSPACE_SYNC_TRANSPORT"
        ]?.caseInsensitiveCompare("wifi") == .orderedSame
        if !forceWiFiForDiagnostics,
           let port = DeviceSerialDiscovery.preferredPort() {
            let progressGeneration = UUID()
            trackedFlightUSBProgressGeneration = progressGeneration
            defer {
                if trackedFlightUSBProgressGeneration == progressGeneration {
                    trackedFlightUSBProgressGeneration = nil
                }
            }
            syncProgress = 0.10
            var assets: [USBConfigurationClient.AssetPayload] = []
            for package in packages {
                pushState = .pushing(
                    "Packing \(package.flight.marketingNumber) maps \(package.slot + 1)/4 for USB-C…"
                )
                assets.append(
                    .init(
                        name: "route_globe_\(package.slot)",
                        data: package.globe
                    )
                )
                assets.append(
                    .init(
                        name: "route_frame_\(package.slot)",
                        data: package.frame
                    )
                )
                assets.append(
                    .init(
                        name: "flag_origin_\(package.slot)",
                        data: package.originFlag
                    )
                )
                assets.append(
                    .init(
                        name: "flag_destination_\(package.slot)",
                        data: package.destinationFlag
                    )
                )
            }
            let state = try deviceClient.encodedState(
                style: .cartographicGlass,
                routeProgress: routeProgress,
                flight: trackedFlight,
                trackingActive: true,
                deviceSlot: activeTrackedFlightDeviceSlot
            )
            pushState = .pushing(
                "Sending \(packages.count) tracked-flight pack\(packages.count == 1 ? "" : "s") over USB-C…"
            )
            try await usbConfigurationClient.pushPackage(
                assets: assets,
                state: state,
                port: port,
                expectedFlightNumber: trackedFlight.marketingNumber,
                progress: { [weak self] progress in
                    Task { @MainActor [weak self] in
                        guard let self,
                              self.trackedFlightUSBProgressGeneration ==
                                progressGeneration
                        else {
                            return
                        }
                        let percent = Int((progress.fraction * 100).rounded())
                        self.syncProgress = 0.10 + progress.fraction * 0.85
                        self.pushState = .pushing(
                            "USB-C \(percent)% · " +
                                "asset \(progress.assetIndex)/\(progress.assetCount)"
                        )
                    }
                },
                finalizing: { [weak self] in
                    Task { @MainActor [weak self] in
                        guard let self,
                              self.trackedFlightUSBProgressGeneration ==
                                progressGeneration
                        else {
                            return
                        }
                        self.syncProgress = 0.98
                        self.pushState = .pushing(
                            "USB-C transfer complete · verifying device…"
                        )
                    }
                }
            )
            trackedFlightUSBProgressGeneration = nil
            syncProgress = 0.98
            pushState = .pushing("USB-C transfer complete · finalizing…")
            deviceReachabilityTransport = .usb
            return true
        }

        let progressGeneration = UUID()
        trackedFlightWiFiProgressGeneration = progressGeneration
        defer {
            if trackedFlightWiFiProgressGeneration == progressGeneration {
                trackedFlightWiFiProgressGeneration = nil
            }
        }
        let totalAssetCount = packages.count * 4
        let totalBytes = packages.reduce(Int64(0)) { partial, package in
            partial + Int64(
                package.globe.count + package.frame.count +
                    package.originFlag.count + package.destinationFlag.count
            )
        }
        var completedBytes: Int64 = 0
        var assetIndex = 0

        func uploadProgress(
            flightNumber: String,
            label: String,
            baseBytes: Int64,
            assetBytes: Int64,
            index: Int
        ) -> @Sendable (Int64, Int64) -> Void {
            { [weak self] sentBytes, _ in
                let sent = min(max(sentBytes, 0), assetBytes)
                let aggregate = baseBytes + sent
                let fraction = totalBytes > 0
                    ? min(max(Double(aggregate) / Double(totalBytes), 0), 1)
                    : 1
                Task { @MainActor [weak self] in
                    guard let self,
                          self.trackedFlightWiFiProgressGeneration ==
                            progressGeneration
                    else {
                        return
                    }
                    let percent = Int((fraction * 100).rounded())
                    self.syncProgress = 0.10 + fraction * 0.85
                    self.pushState = .pushing(
                        "Wi-Fi \(percent)% · asset \(index)/\(totalAssetCount) · " +
                            "\(flightNumber) \(label)"
                    )
                }
            }
        }

        do {
            syncProgress = 0.06
            pushState = .pushing("Preparing tracked-flight storage over Wi-Fi…")
            try await deviceClient.prepareTrackedFlightPackage(host: deviceHost)
            for package in packages {
                assetIndex += 1
                let globeBase = completedBytes
                try await deviceClient.pushMap(
                    host: deviceHost,
                    rgb565: package.globe,
                    view: .globe,
                    slot: package.slot,
                    progress: uploadProgress(
                        flightNumber: package.flight.marketingNumber,
                        label: "main map",
                        baseBytes: globeBase,
                        assetBytes: Int64(package.globe.count),
                        index: assetIndex
                    )
                )
                completedBytes += Int64(package.globe.count)

                assetIndex += 1
                let frameBase = completedBytes
                try await deviceClient.pushMap(
                    host: deviceHost,
                    rgb565: package.frame,
                    view: .frame,
                    slot: package.slot,
                    progress: uploadProgress(
                        flightNumber: package.flight.marketingNumber,
                        label: "straight map",
                        baseBytes: frameBase,
                        assetBytes: Int64(package.frame.count),
                        index: assetIndex
                    )
                )
                completedBytes += Int64(package.frame.count)

                assetIndex += 1
                let originFlagBase = completedBytes
                try await deviceClient.pushRouteFlag(
                    host: deviceHost,
                    rgb565: package.originFlag,
                    endpoint: .origin,
                    slot: package.slot,
                    progress: uploadProgress(
                        flightNumber: package.flight.marketingNumber,
                        label: "origin flag",
                        baseBytes: originFlagBase,
                        assetBytes: Int64(package.originFlag.count),
                        index: assetIndex
                    )
                )
                completedBytes += Int64(package.originFlag.count)

                assetIndex += 1
                let destinationFlagBase = completedBytes
                try await deviceClient.pushRouteFlag(
                    host: deviceHost,
                    rgb565: package.destinationFlag,
                    endpoint: .destination,
                    slot: package.slot,
                    progress: uploadProgress(
                        flightNumber: package.flight.marketingNumber,
                        label: "destination flag",
                        baseBytes: destinationFlagBase,
                        assetBytes: Int64(package.destinationFlag.count),
                        index: assetIndex
                    )
                )
                completedBytes += Int64(package.destinationFlag.count)
            }
            trackedFlightWiFiProgressGeneration = nil
            syncProgress = 0.96
            pushState = .pushing(
                "Wi-Fi transfer complete · updating device state…"
            )
        } catch {
            trackedFlightWiFiProgressGeneration = nil
            let snapshot = try await deviceClient.fetchStatusSnapshot(
                host: deviceHost
            )
            guard snapshot.routeMapMask == 3,
                  snapshot.routeFlagMask == 3,
                  snapshot.trackedFlightNumber
                    .caseInsensitiveCompare(trackedFlight.marketingNumber)
                    == .orderedSame
            else {
                throw error
            }
            pushState = .pushing(
                "Map refresh unavailable · retaining this flight’s stored maps…"
            )
        }
        return false
    }

    private struct PreparedFlightPackage {
        let slot: Int
        let flight: TrackedFlight
        let globe: Data
        let frame: Data
        let originFlag: Data
        let destinationFlag: Data
    }

    private func preparedFlightPackagesForPush() async throws -> [PreparedFlightPackage] {
        var packages: [PreparedFlightPackage] = []
        for (planIndex, plan) in flightPlans.enumerated() {
            guard let flight = resolvedFlights[plan.id] else { continue }
            pushState = .pushing(
                "Checking cached maps \(planIndex + 1)/\(flightPlans.count)…"
            )
            let cachedAssets: () -> (ImageWriter.DeviceImageAsset, ImageWriter.DeviceImageAsset)? = {
                guard plan.mapsPreparedAt != nil else { return nil }
                let stem = self.flightAssetStem(
                    plan: plan,
                    flightNumber: flight.marketingNumber
                )
                guard
                    let globe = try? ImageWriter.existingDeviceImage(
                        name: "\(stem)_geodesic_map_466"
                    ),
                    let frame = try? ImageWriter.existingDeviceImage(
                        name: "\(stem)_route_map_466"
                    )
                else { return nil }
                return (globe, frame)
            }
            if cachedAssets() == nil {
                let renderStartedAt = Date()
                pushState = .pushing(
                    "Rendering missing maps \(planIndex + 1)/\(flightPlans.count)…"
                )
                await prepareFlightMaps(
                    plan.id,
                    updatePreview: plan.id == activeFlightPlanID
                )
                let elapsed = Date().timeIntervalSince(renderStartedAt)
                FileHandle.standardError.write(
                    Data(
                        String(
                            format: "Rendered %@ map pair in %.3f s\n",
                            flight.marketingNumber,
                            elapsed
                        ).utf8
                    )
                )
            }
            guard let refreshedPlan = flightPlans.first(where: { $0.id == plan.id }) else {
                continue
            }
            let stem = flightAssetStem(
                plan: refreshedPlan,
                flightNumber: flight.marketingNumber
            )
            let globeAsset = try ImageWriter.existingDeviceImage(
                name: "\(stem)_geodesic_map_466"
            )
            let frameAsset = try ImageWriter.existingDeviceImage(
                name: "\(stem)_route_map_466"
            )
            packages.append(
                .init(
                    slot: max(0, min(3, refreshedPlan.deviceSlot ?? 0)),
                    flight: flight,
                    globe: try Data(contentsOf: globeAsset.rgb565URL),
                    frame: try Data(contentsOf: frameAsset.rgb565URL),
                    originFlag: try FlagRenderer.rgb565Flag(
                        countryCode: flight.origin.countryCode
                    ),
                    destinationFlag: try FlagRenderer.rgb565Flag(
                        countryCode: flight.destination.countryCode
                    )
                )
            )
            syncProgress = 0.02 +
                (Double(planIndex + 1) / Double(max(1, flightPlans.count))) * 0.08
        }
        return packages
    }

    /// Keeps the Mac digital twin current as soon as a flight lookup succeeds.
    /// Device upload is deliberately separate so rendering cannot leave the
    /// preview showing a previous flight when the tracker is offline.
    private func refreshTrackedFlightPreviewMaps() async {
        guard let activeFlightPlanID,
              let flight = resolvedFlights[activeFlightPlanID] else {
            return
        }
        if trackedFlight.marketingNumber.caseInsensitiveCompare(
            flight.marketingNumber
        ) != .orderedSame {
            activate(flight)
        }
        await prepareFlightMaps(activeFlightPlanID, updatePreview: true)
    }

    /// Builds both projection assets for one scheduled flight. Readiness is
    /// recorded only after both maps and both endpoint flags can be encoded.
    /// This is intentionally independent of the currently displayed flight so
    /// the four-flight queue can be prepared without making the preview jump.
    private func prepareFlightMaps(
        _ planID: UUID,
        updatePreview: Bool
    ) async {
        guard
            let planIndex = flightPlans.firstIndex(where: { $0.id == planID }),
            let flight = resolvedFlights[planID]
        else { return }
        let plan = flightPlans[planIndex]

        renderState = .rendering
        do {
            let progress = max(0, min(1, flight.routeProgress ?? 0))
            async let geodesicImage = geodesicMapRenderer.renderMap(
                for: flight,
                progress: progress,
                size: .init(width: 466, height: 466)
            )
            async let frameImage = mapRenderer.renderMap(
                for: flight,
                progress: progress,
                size: .init(width: 466, height: 466)
            )
            let (geodesic, frame) = try await (geodesicImage, frameImage)
            let stem = flightAssetStem(
                plan: plan,
                flightNumber: flight.marketingNumber
            )
            let globeAsset = try ImageWriter.writeDeviceImage(
                geodesic,
                name: "\(stem)_geodesic_map_466"
            )
            _ = try ImageWriter.writeDeviceImage(
                frame,
                name: "\(stem)_route_map_466"
            )
            _ = try FlagRenderer.rgb565Flag(
                countryCode: flight.origin.countryCode
            )
            _ = try FlagRenderer.rgb565Flag(
                countryCode: flight.destination.countryCode
            )

            guard let completedPlanIndex = flightPlans.firstIndex(where: {
                $0.id == planID
            }) else {
                return
            }
            flightPlans[completedPlanIndex].mapsPreparedAt = Date()
            if flightPlans[completedPlanIndex].deviceSlot == nil {
                flightPlans[completedPlanIndex].deviceSlot = plan.deviceSlot ??
                    firstAvailableFlightSlot()
            }
            persistFlightPlans()
            if updatePreview {
                latestGeodesicMapImage = geodesic
                latestRouteFrameMapImage = frame
                latestMapImage = geodesic
                latestDeviceAsset = globeAsset
            }
            renderState = .rendered(globeAsset.pngURL)
        } catch {
            if let failedPlanIndex = flightPlans.firstIndex(where: {
                $0.id == planID
            }) {
                flightPlans[failedPlanIndex].mapsPreparedAt = nil
                persistFlightPlans()
            }
            renderState = .failed(
                "Map preparation failed: \(error.localizedDescription)"
            )
        }
    }

    private func flightAssetStem(
        plan: PlannedFlight,
        flightNumber: String
    ) -> String {
        let number = flightNumber.uppercased().filter {
            $0.isLetter || $0.isNumber
        }
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.dateFormat = "yyyyMMdd"
        let slot = max(0, min(3, plan.deviceSlot ?? 0))
        return "\(number)_\(formatter.string(from: plan.departureDate))_s\(slot)"
    }

    private func renderRouteMapAsset() async throws -> ImageWriter.DeviceImageAsset {
        renderState = .rendering
        let image = try await mapRenderer.renderMap(
            for: trackedFlight,
            progress: routeProgress,
            size: .init(width: 466, height: 466)
        )
        let asset = try ImageWriter.writeDeviceImage(image, name: "\(trackedFlight.marketingNumber)_route_map_466")
        latestMapImage = image
        latestRouteFrameMapImage = image
        latestDeviceAsset = asset
        renderState = .rendered(asset.pngURL)
        return asset
    }

    private func renderGeodesicMapAsset() async throws -> ImageWriter.DeviceImageAsset {
        renderState = .rendering
        let image = try await geodesicMapRenderer.renderMap(
            for: trackedFlight,
            progress: routeProgress,
            size: .init(width: 466, height: 466)
        )
        let asset = try ImageWriter.writeDeviceImage(
            image,
            name: "\(trackedFlight.marketingNumber)_geodesic_map_466"
        )
        latestMapImage = image
        latestGeodesicMapImage = image
        latestDeviceAsset = asset
        renderState = .rendered(asset.pngURL)
        return asset
    }

    private func renderLocalRadarMapAsset() async throws -> ImageWriter.DeviceImageAsset {
        try await renderLocalRadarMapAsset(radiusMiles: 20)
    }

    private func renderLocalRadarMapAsset(radiusMiles: Double) async throws -> ImageWriter.DeviceImageAsset {
        let location = radarLocation(for: selectedRadarLocationKey) ??
            RadarLocationPreset.all[0]
        return try await renderLocalRadarMapAsset(
            location: location,
            radiusMiles: radiusMiles
        )
    }

    private func renderLocalRadarMapAsset(location: RadarLocationPreset, radiusMiles: Double) async throws -> ImageWriter.DeviceImageAsset {
        renderState = .rendering
        let image = try await localRadarMapRenderer.renderMap(
            center: location.coordinate,
            radiusMiles: radiusMiles,
            size: .init(width: 466, height: 466)
        )
        let asset = try ImageWriter.writeDeviceImage(image, name: "\(location.key)_radar_map_\(Int(radiusMiles))mi_466")
        latestMapImage = image
        if location.key.caseInsensitiveCompare(selectedRadarLocationKey) == .orderedSame,
           abs(radiusMiles - 20) < 0.1 {
            latestLocalRadarMapImage = image
        }
        latestDeviceAsset = asset
        renderState = .rendered(asset.pngURL)
        return asset
    }

    private var selectedRadarLocationKey: String {
        if radarLocation(for: selectedRadarPreviewLocationKey) != nil {
            return selectedRadarPreviewLocationKey
        }
        return radarLocation(
            label: portableLocationLabel,
            metar: portableMetarStation
        )?.key ?? radarCityKeys.first ?? "SJC"
    }

    private var previewAssetDirectory: URL {
        let pictures = FileManager.default.urls(
            for: .picturesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.homeDirectoryForCurrentUser
        return pictures.appendingPathComponent(
            "AirspaceInstrument",
            isDirectory: true
        )
    }

    private func loadCachedPreviewAssets() {
        loadCachedLocalRadarMap()
        loadCachedTrackedFlightMaps()
    }

    private func loadCachedLocalRadarMap() {
        let locationKey = selectedRadarLocationKey
        let candidates = [
            "\(locationKey)_radar_map_20mi_466.png",
            "\(portableLocationLabel.replacingOccurrences(of: " ", with: ""))_radar_map_20mi_466.png",
            "\(portableLocationLabel.replacingOccurrences(of: " ", with: ""))_radar_map_466.png",
        ]
        latestLocalRadarMapImage = candidates.lazy
            .compactMap {
                NSImage(
                    contentsOf: self.previewAssetDirectory
                        .appendingPathComponent($0)
                )
            }
            .first
    }

    private func loadCachedTrackedFlightMaps() {
        let number = trackedFlight.marketingNumber
            .uppercased()
            .filter { $0.isLetter || $0.isNumber }
        guard !number.isEmpty else { return }
        let scheduledStem = activePlan.map {
            flightAssetStem(plan: $0, flightNumber: number)
        }
        let globeCandidates = [
            scheduledStem.map { "\($0)_geodesic_map_466.png" },
            "\(number)_geodesic_map_466.png",
        ].compactMap { $0 }
        let frameCandidates = [
            scheduledStem.map { "\($0)_route_map_466.png" },
            "\(number)_route_map_466.png",
        ].compactMap { $0 }
        latestGeodesicMapImage = globeCandidates.lazy.compactMap {
            NSImage(contentsOf: self.previewAssetDirectory.appendingPathComponent($0))
        }.first
        latestRouteFrameMapImage = frameCandidates.lazy.compactMap {
            NSImage(contentsOf: self.previewAssetDirectory.appendingPathComponent($0))
        }.first
        latestMapImage =
            latestGeodesicMapImage ?? latestRouteFrameMapImage
    }

    private func refreshTrackedAirlineLogo() {
        airlineLogoTask?.cancel()
        let code = ProjectAssetCatalog.normalizedAirlineCode(
            String(
                trackedFlight.marketingNumber.prefix {
                    $0.isLetter
                }
            )
        )
        latestAirlineLogoImage = nil
        guard !code.isEmpty else { return }
        airlineLogoTask = Task { @MainActor [weak self] in
            let image = await ProjectAssetCatalog.airlineLogo(
                for: code
            )
            guard
                !Task.isCancelled,
                let self,
                ProjectAssetCatalog.normalizedAirlineCode(
                    String(
                        self.trackedFlight.marketingNumber.prefix {
                            $0.isLetter
                        }
                    )
                ) == code
            else {
                return
            }
            self.latestAirlineLogoImage = image
        }
    }

    private func renderLocalRadarMapAssets() async throws -> [Double: ImageWriter.DeviceImageAsset] {
        var assets: [Double: ImageWriter.DeviceImageAsset] = [:]
        for radiusMiles in LocalRadarMapRenderer.supportedRadiusMiles {
            assets[radiusMiles] = try await renderLocalRadarMapAsset(radiusMiles: radiusMiles)
        }
        return assets
    }

    private func pushAllLocalRadarMaps() async throws {
        let locations = configuredRadarLocations
        let total = locations.count * LocalRadarMapRenderer.supportedRadiusMiles.count
        var completed = 0
        for location in locations {
            for radiusMiles in LocalRadarMapRenderer.supportedRadiusMiles {
                let asset = try await renderLocalRadarMapAsset(location: location, radiusMiles: radiusMiles)
                let rgb565 = try Data(contentsOf: asset.rgb565URL)
                completed += 1
                pushState = .pushing("Uploading \(location.key) \(Int(radiusMiles)) mi radar map \(completed)/\(total)…")
                try await deviceClient.pushRadarMap(host: deviceHost, rgb565: rgb565, rangeMiles: radiusMiles, locationKey: location.key)
                try await Task.sleep(nanoseconds: 350_000_000)
            }
        }
    }

    private var hasLiveTrackedFlightProvider: Bool {
        !flightAwareAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ||
            !airLabsAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    private func liveFlightProvider() -> FlightStatusProviding {
        let flightAwareKey = flightAwareAPIKey.trimmingCharacters(
            in: .whitespacesAndNewlines
        )
        let airLabsKey = airLabsAPIKey.trimmingCharacters(
            in: .whitespacesAndNewlines
        )
        if flightAwareKey.isEmpty {
            guard !airLabsKey.isEmpty else { return staticFlightProvider }
            return AirLabsOpenSkyFlightStatusProvider(
                apiKey: airLabsKey,
                openSkyClientID: openSkyClientID,
                openSkyClientSecret: openSkyClientSecret
            )
        }
        let flightAwarePrimary = FlightAwareFlightStatusProvider(
            apiKey: flightAwareKey
        )
        let airLabsFallback: (any FlightStatusProviding)? = airLabsKey.isEmpty
            ? nil
            : AirLabsOpenSkyFlightStatusProvider(
                apiKey: airLabsKey,
                openSkyClientID: openSkyClientID,
                openSkyClientSecret: openSkyClientSecret
            )
        return FallbackFlightStatusProvider(
            primary: flightAwarePrimary,
            fallback: airLabsFallback,
            telemetryProvider: OpenSkyTrackedFlightTelemetryProvider(
                clientID: openSkyClientID,
                clientSecret: openSkyClientSecret
            )
        )
    }

    private enum CredentialAccount: String {
        case flightAwareAeroAPIKey = "flightaware.aeroapi.key"
        case openSkyClientID = "opensky.client.id"
        case openSkyClientSecret = "opensky.client.secret"
        case airLabsAPIKey = "airlabs.api.key"
        case homeWiFiPassword = "wifi.home.password"
        case hotspotWiFiPassword = "wifi.hotspot.password"
    }

    private enum DefaultsKey: String {
        case nearbyAircraftProvider = "nearbyAircraftProvider"
        case activeDeviceWiFiProfile = "activeDeviceWiFiProfile"
        case homeWiFiSSID = "homeWiFiSSID"
        case hotspotWiFiSSID = "hotspotWiFiSSID"
        case portableLocationLabel = "portableLocationLabel"
        case portableLatitude = "portableLatitude"
        case portableLongitude = "portableLongitude"
        case portableTimezone = "portableTimezone"
        case portableMetarStation = "portableMetarStation"
        case portableRadarHalfDegrees = "portableRadarHalfDegrees"
        case aircraftLabelsEnabled = "aircraftLabelsEnabled"
        case radarWedgeEnabled = "radarWedgeEnabled"
        case issFlagsEnabled = "issFlagsEnabled"
        case weatherDisplayUnits = "weatherDisplayUnits"
        case accentColor = "accentColor"
        case customAccentColors = "customAccentColors.v1"
        case clockCityKeys = "clockCityKeys.v1"
        case radarCityKeys = "radarCityKeys.v1"
        case customRadarLocations = "customRadarLocations.v1"
        case launchAgencies = "launchAgencies.v1"
        case dockSchedule = "dockSchedule.v1"
        case mainScreenOrder = "mainScreenOrder.v1"
        case trackedFlightNumber = "trackedFlightNumber"
        case trackedFlightDate = "trackedFlightDate"
        case trackedFlightPlans = "trackedFlightPlans.v1"
        case resolvedTrackedFlights = "resolvedTrackedFlights.v1"
        case trackedFlightMonitoringActive = "trackedFlightMonitoringActive"
        case deviceHost = "deviceHost"
        case nextLaunch = "nextLaunch.v1"
        case nextLaunches = "nextLaunches.v1"
    }

    private static let credentialStore =
        LocalCredentialStore(service: "AirspaceCompanion")
    private static let planDateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .none
        return formatter
    }()
    private static let conflictTimeFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "MMM d, HH:mm"
        return formatter
    }()

    private static func flightConflictMessage(
        candidateNumber: String,
        candidate: PlannedFlight,
        existing: PlannedFlight
    ) -> String {
        let newStart = conflictTimeFormatter.string(
            from: candidate.effectiveDeparture
        )
        let newEnd = conflictTimeFormatter.string(
            from: candidate.effectiveArrival
        )
        let oldStart = conflictTimeFormatter.string(
            from: existing.effectiveDeparture
        )
        let oldEnd = conflictTimeFormatter.string(
            from: existing.effectiveArrival
        )
        return "Cannot add \(candidateNumber) (\(newStart)–\(newEnd)): " +
            "it overlaps \(existing.flightNumber) (\(oldStart)–\(oldEnd)). " +
            "Only one tracked flight can be active."
    }

    private func persistFlightPlans() {
        guard let data = try? JSONEncoder().encode(flightPlans) else { return }
        UserDefaults.standard.set(
            data,
            forKey: DefaultsKey.trackedFlightPlans.rawValue
        )
        persistResolvedFlights()
        persistSharedConfiguration()
    }

    private func persistResolvedFlights() {
        let planIDs = Set(flightPlans.map(\.id))
        let retained = resolvedFlights.reduce(into: [String: TrackedFlight]()) {
            result, entry in
            guard planIDs.contains(entry.key) else { return }
            result[entry.key.uuidString] = entry.value
        }
        guard let data = try? JSONEncoder().encode(retained) else { return }
        UserDefaults.standard.set(
            data,
            forKey: DefaultsKey.resolvedTrackedFlights.rawValue
        )
    }

    private func applyDeviceOwnedConfiguration(
        _ device: SharedConfiguration.Device
    ) {
        if let profile = DeviceWiFiProfile(
            rawValue: device.activeWifiProfile
        ) {
            activeDeviceWiFiProfile = profile
        }
        /* Profile selection is available on the instrument; profile names and
         * credentials are configured only by the companion. Do not replace a
         * pending SSID edit while refreshing the device-owned fields. */
        portableLocationLabel = device.location.label
        portableLatitude = device.location.lat
        portableLongitude = device.location.lon
        portableTimezone = device.location.timezone
        portableMetarStation = device.location.metar
        portableRadarHalfDegrees = device.location.radarHalfDegrees
        if let location = radarLocation(
            label: device.location.label,
            metar: device.location.metar
        ) {
            selectedRadarPreviewLocationKey = location.key
        }
        aircraftLabelsEnabled = device.display.aircraftLabels
        radarWedgeEnabled = device.display.radarWedge
        issFlagsEnabled = device.display.issFlags ?? issFlagsEnabled
        if let units = device.display.weatherUnits.flatMap(
            WeatherDisplayUnits.init(rawValue:)
        ) {
            /* Units are selected on the instrument, so its value wins when
             * both sides changed since the last shared revision. */
            weatherDisplayUnits = units
        }
        accentColor = DeviceAccentColor(
            rawValue: device.display.accentColor
        ) ?? .green
        /* The selected accent is adjustable on the instrument, so the device
         * owns that choice. The editable orange/cyan/red RGB palette exists
         * only in the companion and must survive the pre-push device refresh.
         * Applying the device copy here used to silently replace a freshly
         * edited color before the outgoing payload was assembled. */
        radarRadiusMiles = [10, 20, 50].contains(
            device.radarRadiusMiles
        ) ? device.radarRadiusMiles : 20
        lastSyncedDeviceRevision = device.revision
    }

    private func sharedCompanionConfiguration(
        deviceRevision: UInt32
    ) -> SharedConfiguration.Companion {
        SharedConfiguration.Companion(
            radarCities: Self.normalizeRadarCityKeys(radarCityKeys),
            radarLocations: configuredRadarLocations.map(\.definition),
            clockCities: Self.normalizeClockCityKeys(clockCityKeys),
            launchAgencies: selectedLaunchAgencies.map(\.rawValue).sorted(),
            dockSchedule: dockSchedule,
            mainScreens: mainScreenOrder,
            trackedFlights: flightPlans.map(
                SharedConfiguration.TrackedFlightPlan.init
            ),
            activeTrackedFlightId: activeFlightPlanID,
            lastSyncedDeviceRevision: deviceRevision
        )
    }

    private func sharedDeviceConfiguration() -> SharedConfiguration.Device {
        let preset = radarLocation(
            label: portableLocationLabel,
            metar: portableMetarStation
        )
        return SharedConfiguration.Device(
            revision: lastSyncedDeviceRevision,
            activeWifiProfile: activeDeviceWiFiProfile.rawValue,
            wifiProfiles: .init(
                home: .init(
                    ssid: homeWiFiSSID,
                    hasPassword: !homeWiFiPassword.isEmpty
                ),
                hotspot: .init(
                    ssid: hotspotWiFiSSID,
                    hasPassword: !hotspotWiFiPassword.isEmpty
                )
            ),
            location: .init(
                key: preset?.key ?? portableLocationLabel,
                label: preset?.title ?? portableLocationLabel,
                lat: preset.map { "\($0.coordinate.latitude)" } ?? portableLatitude,
                lon: preset.map { "\($0.coordinate.longitude)" } ?? portableLongitude,
                timezone: preset?.timezone ?? portableTimezone,
                metar: preset?.metar ?? portableMetarStation,
                radarHalfDegrees: preset?.radarHalfDegrees ?? portableRadarHalfDegrees
            ),
            display: .init(
                aircraftLabels: aircraftLabelsEnabled,
                radarWedge: radarWedgeEnabled,
                issFlags: issFlagsEnabled,
                weatherUnits: weatherDisplayUnits.rawValue,
                accentColor: accentColor.rawValue,
                accentColors: customAccentColors
            ),
            radarRadiusMiles: radarRadiusMiles,
            services: .init(
                openSkyReady: !openSkyClientID.isEmpty &&
                    !openSkyClientSecret.isEmpty,
                airLabsReady: !airLabsAPIKey.isEmpty,
                flightAwareReady: !flightAwareAPIKey.isEmpty
            )
        )
    }

    private func persistSharedConfiguration() {
        let configuration = SharedConfiguration(
            schemaVersion: SharedConfiguration.schemaVersion,
            savedAtUnix: Int64(Date().timeIntervalSince1970.rounded()),
            reconnecting: false,
            conflict: false,
            ownership: .standard,
            device: sharedDeviceConfiguration(),
            companion: sharedCompanionConfiguration(
                deviceRevision: lastSyncedDeviceRevision
            )
        )
        try? SharedConfigurationStore.save(configuration)
    }

    private static func loadFlightPlans() -> [PlannedFlight] {
        if let shared = SharedConfigurationStore.load() {
            return shared.companion.trackedFlights
                .map(\.plannedFlight)
                .sorted {
                    if $0.departureDate == $1.departureDate {
                        return $0.addedAt < $1.addedAt
                    }
                    return $0.departureDate < $1.departureDate
                }
        }
        if let data = UserDefaults.standard.data(
            forKey: DefaultsKey.trackedFlightPlans.rawValue
        ),
           let plans = try? JSONDecoder().decode([PlannedFlight].self, from: data) {
            return plans.sorted {
                if $0.departureDate == $1.departureDate {
                    return $0.addedAt < $1.addedAt
                }
                return $0.departureDate < $1.departureDate
            }
        }

        let number = normalizedFlightNumber(
            UserDefaults.standard.string(
                forKey: DefaultsKey.trackedFlightNumber.rawValue
            ) ?? "YP111"
        )
        guard !number.isEmpty else { return [] }
        return [
            PlannedFlight(
                flightNumber: number,
                departureDate: loadDateDefault(.trackedFlightDate)
            )
        ]
    }

    private static func loadResolvedFlights(
        for plans: [PlannedFlight]
    ) -> [UUID: TrackedFlight] {
        guard let data = UserDefaults.standard.data(
            forKey: DefaultsKey.resolvedTrackedFlights.rawValue
        ),
        let cached = try? JSONDecoder().decode(
            [String: TrackedFlight].self,
            from: data
        ) else {
            return [:]
        }

        let plansByID = Dictionary(uniqueKeysWithValues: plans.map {
            ($0.id, $0)
        })
        return cached.reduce(into: [UUID: TrackedFlight]()) {
            result, entry in
            guard let id = UUID(uuidString: entry.key),
                  let plan = plansByID[id],
                  normalizedFlightNumber(entry.value.marketingNumber) ==
                    normalizedFlightNumber(plan.flightNumber)
            else {
                return
            }
            if let selection = plan.legSelection {
                guard entry.value.origin.code.uppercased() ==
                        selection.originCode.uppercased(),
                      entry.value.destination.code.uppercased() ==
                        selection.destinationCode.uppercased()
                else {
                    return
                }
                if let cachedDeparture =
                    entry.value.scheduledDeparture ??
                    entry.value.estimatedDeparture {
                    guard abs(cachedDeparture.timeIntervalSince(
                        selection.scheduledDeparture
                    )) < 2 * 3600 else {
                        return
                    }
                }
            }
            result[id] = entry.value
        }
    }

    private static func loadNextLaunch() -> TrackedLaunch {
        guard
            let data = UserDefaults.standard.data(
                forKey:
                    DefaultsKey.nextLaunch.rawValue
            ),
            let launch = try? JSONDecoder().decode(
                TrackedLaunch.self,
                from: data
            )
        else {
            return .preview
        }
        return launch
    }

    private static func loadNextLaunches() -> [TrackedLaunch] {
        guard
            let data = UserDefaults.standard.data(
                forKey:
                    DefaultsKey.nextLaunches.rawValue
            ),
            let launches = try? JSONDecoder().decode(
                [TrackedLaunch].self,
                from: data
            ),
            !launches.isEmpty
        else {
            return [loadNextLaunch()]
        }
        return launches.sorted { $0.net < $1.net }
    }

    private static func normalizedFlightNumber(_ value: String) -> String {
        FlightNumberIdentity.canonical(value)
    }

    private static func orderedFlightLegs(
        _ candidates: [FlightLegCandidate],
        relativeTo now: Date
    ) -> [FlightLegCandidate] {
        candidates.sorted { lhs, rhs in
            if lhs.isComplete != rhs.isComplete {
                return !lhs.isComplete
            }
            let lhsUpcoming = lhs.scheduledDeparture >=
                now.addingTimeInterval(-30 * 60)
            let rhsUpcoming = rhs.scheduledDeparture >=
                now.addingTimeInterval(-30 * 60)
            if lhsUpcoming != rhsUpcoming { return lhsUpcoming }
            return lhs.scheduledDeparture < rhs.scheduledDeparture
        }
    }

    private static func loadCredential(_ account: CredentialAccount, environmentKeys: [String]) -> String {
        for key in environmentKeys {
            if let value = ProcessInfo.processInfo.environment[key],
               !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                return value
            }
        }

        if ProcessInfo.processInfo.environment[
            "AIRSPACE_COMPANION_SKIP_KEYCHAIN"
        ] == "1" {
            return ""
        }

        if let stored = credentialStore.string(for: account.rawValue),
           !stored.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return stored
        }

        return ""
    }

    private static func storeCredential(_ value: String, account: CredentialAccount) {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            credentialStore.remove(for: account.rawValue)
        } else {
            credentialStore.set(trimmed, for: account.rawValue)
        }
    }

    private static func loadDefault(_ key: DefaultsKey, fallback: String) -> String {
        UserDefaults.standard.string(forKey: key.rawValue) ?? fallback
    }

    private static func storeDefault(_ value: String, key: DefaultsKey) {
        UserDefaults.standard.set(value, forKey: key.rawValue)
    }

    private static func loadDateDefault(_ key: DefaultsKey) -> Date {
        let stored = UserDefaults.standard.double(forKey: key.rawValue)
        if stored > 0 {
            return Date(timeIntervalSince1970: stored)
        }
        return Calendar.current.startOfDay(for: Date())
    }

    private static func loadNearbyAircraftProvider() -> NearbyAircraftProvider {
        guard let rawValue = UserDefaults.standard.string(forKey: DefaultsKey.nearbyAircraftProvider.rawValue),
              let provider = NearbyAircraftProvider(rawValue: rawValue) else {
            return .openSky
        }
        return provider
    }

    private static func loadDeviceWiFiProfile() -> DeviceWiFiProfile {
        if let rawValue = loadSharedDevice()?.activeWifiProfile,
           let profile = DeviceWiFiProfile(rawValue: rawValue) {
            return profile
        }
        guard let rawValue = UserDefaults.standard.string(forKey: DefaultsKey.activeDeviceWiFiProfile.rawValue),
              let profile = DeviceWiFiProfile(rawValue: rawValue) else {
            return .home
        }
        return profile
    }

    private static func loadAccentColor() -> DeviceAccentColor {
        let rawValue = loadSharedDevice()?.display.accentColor ??
            UserDefaults.standard.string(forKey: DefaultsKey.accentColor.rawValue)
        guard let rawValue else {
            return .green
        }
        if rawValue == "teal" {
            return .red
        }
        return DeviceAccentColor(rawValue: rawValue) ?? .green
    }

    private static let defaultCustomAccentColors: [String: String] = [
        DeviceAccentColor.orange.rawValue: "D47A17",
        DeviceAccentColor.cyan.rawValue: "00A3BF",
        DeviceAccentColor.red.rawValue: "BC252F",
    ]

    private static func normalizedRGBHex(_ value: String) -> String? {
        let trimmed = value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .trimmingCharacters(in: CharacterSet(charactersIn: "#"))
            .uppercased()
        guard trimmed.count == 6,
              UInt32(trimmed, radix: 16) != nil else { return nil }
        return trimmed
    }

    private static func normalizeCustomAccentColors(
        _ colors: [String: String]
    ) -> [String: String] {
        var result = defaultCustomAccentColors
        for color in [
            DeviceAccentColor.orange,
            DeviceAccentColor.cyan,
            DeviceAccentColor.red,
        ] {
            if let value = colors[color.rawValue],
               let normalized = normalizedRGBHex(value) {
                result[color.rawValue] = normalized
            }
        }
        return result
    }

    private static func loadCustomAccentColors() -> [String: String] {
        if let colors = loadSharedDevice()?.display.accentColors {
            return normalizeCustomAccentColors(colors)
        }
        guard let data = UserDefaults.standard.data(
            forKey: DefaultsKey.customAccentColors.rawValue
        ), let colors = try? JSONDecoder().decode(
            [String: String].self,
            from: data
        ) else {
            return defaultCustomAccentColors
        }
        return normalizeCustomAccentColors(colors)
    }

    private static func loadClockCityKeys() -> [String] {
        let stored = SharedConfigurationStore.load()?.companion.clockCities ??
            UserDefaults.standard.stringArray(
            forKey: DefaultsKey.clockCityKeys.rawValue
        ) ?? RadarLocationPreset.defaultClockCityKeys
        return normalizeClockCityKeys(
            stored,
            allowedKeys: Set(loadRadarCityKeys())
        )
    }

    private static func loadRadarCityKeys() -> [String] {
        let stored = SharedConfigurationStore.load()?.companion.radarCities ??
            UserDefaults.standard.stringArray(
                forKey: DefaultsKey.radarCityKeys.rawValue
            ) ?? RadarLocationPreset.all.map(\.key)
        return normalizeRadarCityKeys(stored)
    }

    private static func loadCustomRadarLocations()
        -> [SharedConfiguration.LocationDefinition] {
        if let shared = SharedConfigurationStore.load()?
            .companion.radarLocations {
            return normalizeCustomRadarLocations(shared)
        }
        guard let data = UserDefaults.standard.data(
            forKey: DefaultsKey.customRadarLocations.rawValue
        ), let stored = try? JSONDecoder().decode(
            [SharedConfiguration.LocationDefinition].self,
            from: data
        ) else {
            return []
        }
        return normalizeCustomRadarLocations(stored)
    }

    private static let defaultDockSchedule:
        [SharedConfiguration.DockScheduleEntry] = [
            .init(screen: .radar, durationSeconds: 20),
            .init(screen: .tracker, durationSeconds: 20),
            .init(screen: .weather, durationSeconds: 10),
            .init(screen: .clock, durationSeconds: 20),
            .init(screen: .eclipse, durationSeconds: 10),
            .init(screen: .moon, durationSeconds: 5),
            .init(screen: .iss, durationSeconds: 20),
            .init(screen: .launchNasa, durationSeconds: 20),
            .init(screen: .launchRoscosmos, durationSeconds: 20),
            .init(screen: .launchSpacex, durationSeconds: 20),
        ]
    private static func loadDockSchedule()
        -> [SharedConfiguration.DockScheduleEntry] {
        let stored: [SharedConfiguration.DockScheduleEntry]?
        if let shared = SharedConfigurationStore.load()?
            .companion.dockSchedule {
            stored = shared
        } else if let data = UserDefaults.standard.data(
            forKey: DefaultsKey.dockSchedule.rawValue
        ) {
            stored = try? JSONDecoder().decode(
                [SharedConfiguration.DockScheduleEntry].self,
                from: data
            )
        } else {
            stored = nil
        }
        guard let stored,
              (3...10).contains(stored.count) else {
            return defaultDockSchedule
        }
        var seen = Set<SharedConfiguration.DockScreen>()
        let normalized = stored.compactMap { entry ->
            SharedConfiguration.DockScheduleEntry? in
            guard seen.insert(entry.screen).inserted else { return nil }
            return .init(
                screen: entry.screen,
                durationSeconds: min(300, max(5, entry.durationSeconds))
            )
        }
        return (3...10).contains(normalized.count)
            ? normalized
            : defaultDockSchedule
    }

    private static func loadMainScreenOrder()
        -> [SharedConfiguration.MainScreen] {
        let stored: [SharedConfiguration.MainScreen]?
        if let shared = SharedConfigurationStore.load()?
            .companion.mainScreens {
            stored = shared
        } else if let data = UserDefaults.standard.data(
            forKey: DefaultsKey.mainScreenOrder.rawValue
        ) {
            stored = try? JSONDecoder().decode(
                [SharedConfiguration.MainScreen].self,
                from: data
            )
        } else {
            stored = nil
        }
        var result: [SharedConfiguration.MainScreen] = []
        for screen in stored ?? SharedConfiguration.MainScreen.allCases {
            if !result.contains(screen) {
                result.append(screen)
            }
        }
        return result.isEmpty
            ? SharedConfiguration.MainScreen.allCases
            : result
    }

    private static func normalizeRadarCityKeys(_ stored: [String]) -> [String] {
        var result: [String] = []
        for rawKey in stored {
            let key = rawKey.trimmed.uppercased()
            if locationKeyIsValid(key), !result.contains(key), result.count < 8 {
                result.append(key)
            }
        }
        return result.isEmpty ? ["SJC"] : result
    }

    private static func normalizeClockCityKeys(
        _ stored: [String],
        allowedKeys: Set<String>? = nil
    ) -> [String] {
        var result: [String] = []
        for rawKey in stored {
            let key = rawKey.trimmed.uppercased()
            if locationKeyIsValid(key),
               (allowedKeys == nil || allowedKeys!.contains(key)),
               !result.contains(key),
               result.count < 5 {
                result.append(key)
            }
        }
        if !result.isEmpty {
            return result
        }
        if let allowedKeys,
           let firstAllowed = RadarLocationPreset.all.first(
               where: { allowedKeys.contains($0.key) }
           )?.key {
            return [firstAllowed]
        }
        return ["SJC"]
    }

    private static func normalizeCustomRadarLocations(
        _ stored: [SharedConfiguration.LocationDefinition]
    ) -> [SharedConfiguration.LocationDefinition] {
        let builtIns = Set(RadarLocationPreset.all.map { $0.key })
        var seen = Set<String>()
        return stored.compactMap { raw in
            let key = raw.key.trimmed.uppercased()
            guard locationKeyIsValid(key),
                  !builtIns.contains(key),
                  seen.insert(key).inserted,
                  let lat = Double(raw.lat), (-90...90).contains(lat),
                  let lon = Double(raw.lon), (-180...180).contains(lon) else {
                return nil
            }
            return .init(
                key: key,
                label: raw.label.trimmed.isEmpty ? key : raw.label.trimmed,
                lat: String(
                    format: "%.6f",
                    locale: Locale(identifier: "en_US_POSIX"),
                    lat
                ),
                lon: String(
                    format: "%.6f",
                    locale: Locale(identifier: "en_US_POSIX"),
                    lon
                ),
                timezone: raw.timezone.trimmed.isEmpty ? "UTC0" : raw.timezone.trimmed,
                metar: raw.metar.trimmed.uppercased(),
                radarHalfDegrees: raw.radarHalfDegrees.trimmed.isEmpty
                    ? "1.55"
                    : raw.radarHalfDegrees.trimmed
            )
        }
    }

    private static func locationKeyIsValid(_ key: String) -> Bool {
        (2...7).contains(key.count) && key.allSatisfy {
            $0.isASCII && ($0.isLetter || $0.isNumber)
        }
    }

    private static func locationFieldIsSafe(
        _ value: String,
        maximumLength: Int
    ) -> Bool {
        value.count <= maximumLength &&
            !value.contains("|") &&
            !value.contains("\n") &&
            !value.contains("\r")
    }

    private static func loadLaunchAgencies() -> Set<LaunchAgency> {
        let rawValues = SharedConfigurationStore.load()?.companion.launchAgencies ??
            UserDefaults.standard.stringArray(
                forKey: DefaultsKey.launchAgencies.rawValue
            ) ?? LaunchAgency.allCases.map(\.rawValue)
        let result = Set(rawValues.compactMap(LaunchAgency.init(rawValue:)))
        return result.isEmpty ? Set(LaunchAgency.allCases) : result
    }

    private static func loadSharedDevice() -> SharedConfiguration.Device? {
        SharedConfigurationStore.load()?.device
    }

    private static func loadBoolDefault(_ key: DefaultsKey, fallback: Bool) -> Bool {
        guard UserDefaults.standard.object(forKey: key.rawValue) != nil else {
            return fallback
        }
        return UserDefaults.standard.bool(forKey: key.rawValue)
    }
}

private extension String {
    var trimmed: String {
        trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

private enum CompanionSyncError: LocalizedError {
    case incompleteTrackedFlightSync(
        expectedFlight: String,
        receivedFlight: String,
        mapMask: Int
    )
    case trackedFlightDidNotClear
    case unresolvedTrackedFlight

    var errorDescription: String? {
        switch self {
        case .incompleteTrackedFlightSync(
            let expectedFlight,
            let receivedFlight,
            let mapMask
        ):
            let actualFlight = receivedFlight.isEmpty ? "none" : receivedFlight
            return "The device did not retain the requested flight \(expectedFlight) (received \(actualFlight), map mask \(mapMask)); Sync stopped before reporting success."
        case .trackedFlightDidNotClear:
            return "The device kept its previous tracked flight after the clear command."
        case .unresolvedTrackedFlight:
            return "The scheduled flight has not resolved yet, so its device state cannot be verified. Refresh the flight and try Sync again."
        }
    }
}
