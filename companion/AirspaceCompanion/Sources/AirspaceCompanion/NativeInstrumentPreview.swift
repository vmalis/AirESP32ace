// AirESP32ace
// Created by VM, August 2026

import AppKit
import Darwin
import Foundation
import QuartzCore
import SwiftUI

struct NativeInstrumentPreview: NSViewRepresentable {
    let page: DevicePreviewPage
    let flight: TrackedFlight
    let localRadarMapImage: NSImage?
    let geodesicMapImage: NSImage?
    let routeFrameMapImage: NSImage?
    let airlineLogoImage: NSImage?
    let routeProgress: Double
    let locationName: String
    var locationKey: String = "SJC"
    let accent: DeviceAccentColor
    var clockCityKeys: [String] = []
    let weatherUnits: WeatherDisplayUnits
    let launch: TrackedLaunch
    var liveAircraft: DeviceClient.AircraftSnapshot? = nil
    var liveWeather: DeviceClient.WeatherSnapshot? = nil

    static var isAvailable: Bool {
        NativeInstrumentRenderer.shared.isAvailable
    }

    static func snapshot(
        page: DevicePreviewPage,
        flight: TrackedFlight,
        localRadarMapImage: NSImage?,
        geodesicMapImage: NSImage?,
        routeFrameMapImage: NSImage?,
        airlineLogoImage: NSImage?,
        routeProgress: Double,
        locationName: String,
        locationKey: String = "SJC",
        accent: DeviceAccentColor,
        clockCityKeys: [String] = [],
        weatherUnits: WeatherDisplayUnits = .celsius,
        launch: TrackedLaunch,
        liveWeather: DeviceClient.WeatherSnapshot? = nil,
        trackedTimePage: Int? = nil
    ) -> NSImage? {
        let maps: [(Int32, NSImage?)] = [
            (0, localRadarMapImage),
            (1, geodesicMapImage),
            (2, routeFrameMapImage),
        ]
        for (slot, image) in maps {
            if let image,
               let data = try? ImageWriter.rgb565Data(
                from: image,
                width: 466,
                height: 466
               ) {
                NativeInstrumentRenderer.shared.setMap(
                    data,
                    slot: slot
                )
            } else {
                NativeInstrumentRenderer.shared.setMap(
                    nil,
                    slot: slot
                )
            }
        }
        NativeInstrumentRenderer.shared.setAirlineLogo(
            airlineLogoImage,
            code: String(
                flight.marketingNumber.prefix {
                    $0.isLetter
                }
            )
        )
        NativeInstrumentRenderer.shared.setRouteFlags(
            for: flight
        )
        NativeInstrumentRenderer.shared.setRoutePoints(
            for: flight
        )
        NativeInstrumentRenderer.shared.configure(
            page: page,
            flight: flight,
            routeProgress: routeProgress,
            locationName: locationName,
            locationKey: locationKey,
            accent: accent,
            clockCityKeys: clockCityKeys,
            launch: launch
        )
        NativeInstrumentRenderer.shared.setWeatherUnits(weatherUnits)
        NativeInstrumentRenderer.shared.setWeather(liveWeather)
        if let trackedTimePage {
            NativeInstrumentRenderer.shared
                .forceTrackedTimePage(trackedTimePage)
        }
        guard let frame =
            NativeInstrumentRenderer.shared.renderFrame(
                force: true
            )
        else {
            return nil
        }
        return NSImage(
            cgImage: frame,
            size: NSSize(width: 466, height: 466)
        )
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeNSView(context: Context) -> NativeInstrumentView {
        let view = NativeInstrumentView()
        context.coordinator.attach(view)
        configure(context.coordinator, view: view)
        return view
    }

    func updateNSView(
        _ view: NativeInstrumentView,
        context: Context
    ) {
        configure(context.coordinator, view: view)
    }

    static func dismantleNSView(
        _ view: NativeInstrumentView,
        coordinator: Coordinator
    ) {
        coordinator.detach()
    }

    private func configure(
        _ coordinator: Coordinator,
        view: NativeInstrumentView
    ) {
        coordinator.configure(
            view: view,
            page: page,
            flight: flight,
            localRadarMapImage: localRadarMapImage,
            geodesicMapImage: geodesicMapImage,
            routeFrameMapImage: routeFrameMapImage,
            airlineLogoImage: airlineLogoImage,
            routeProgress: routeProgress,
            locationName: locationName,
            locationKey: locationKey,
            accent: accent,
            clockCityKeys: clockCityKeys,
            weatherUnits: weatherUnits,
            launch: launch,
            liveAircraft: liveAircraft,
            liveWeather: liveWeather
        )
    }

    @MainActor
    final class Coordinator {
        private struct Configuration: Equatable {
            let page: DevicePreviewPage
            let flight: TrackedFlight
            let routeProgress: Double
            let locationName: String
            let locationKey: String
            let accent: DeviceAccentColor
            let clockCityKeys: [String]
            let weatherUnits: WeatherDisplayUnits
            let launch: TrackedLaunch
            let liveAircraft: DeviceClient.AircraftSnapshot?
            let liveWeather: DeviceClient.WeatherSnapshot?
        }

        private weak var view: NativeInstrumentView?
        private var timer: Timer?
        private var needsForcedFrame = true
        private var lastConfiguration: Configuration?
        private var lastRadarMap: ObjectIdentifier?
        private var lastGlobeMap: ObjectIdentifier?
        private var lastFrameMap: ObjectIdentifier?
        private var lastAirlineLogo: ObjectIdentifier?
        private var lastAirlineCode = ""
        private var lastOriginFlagCode = ""
        private var lastDestinationFlagCode = ""
        private var didSendEmptyRadarMap = false
        private var didSendEmptyGlobeMap = false
        private var didSendEmptyFrameMap = false

        func attach(_ view: NativeInstrumentView) {
            self.view = view
            needsForcedFrame = true
            timer?.invalidate()
            timer = Timer.scheduledTimer(
                withTimeInterval: 1.0 / 30.0,
                repeats: true
            ) { [weak self] _ in
                MainActor.assumeIsolated {
                    self?.drawNextFrame()
                }
            }
            timer?.tolerance = 0.004
        }

        func detach() {
            timer?.invalidate()
            timer = nil
            view = nil
        }

        func configure(
            view: NativeInstrumentView,
            page: DevicePreviewPage,
            flight: TrackedFlight,
            localRadarMapImage: NSImage?,
            geodesicMapImage: NSImage?,
            routeFrameMapImage: NSImage?,
            airlineLogoImage: NSImage?,
            routeProgress: Double,
            locationName: String,
            locationKey: String,
            accent: DeviceAccentColor,
            clockCityKeys: [String],
            weatherUnits: WeatherDisplayUnits,
            launch: TrackedLaunch,
            liveAircraft: DeviceClient.AircraftSnapshot?,
            liveWeather: DeviceClient.WeatherSnapshot?
        ) {
            var mapChanged = sendMapIfChanged(
                localRadarMapImage,
                slot: 0,
                lastIdentifier: &lastRadarMap,
                sentEmpty: &didSendEmptyRadarMap
            )
            mapChanged = sendMapIfChanged(
                geodesicMapImage,
                slot: 1,
                lastIdentifier: &lastGlobeMap,
                sentEmpty: &didSendEmptyGlobeMap
            ) || mapChanged
            mapChanged = sendMapIfChanged(
                routeFrameMapImage,
                slot: 2,
                lastIdentifier: &lastFrameMap,
                sentEmpty: &didSendEmptyFrameMap
            ) || mapChanged
            let logoChanged = sendLogoIfChanged(
                airlineLogoImage,
                code: String(
                    flight.marketingNumber.prefix {
                        $0.isLetter
                    }
                )
            )
            let flagsChanged =
                sendRouteFlagsIfChanged(flight)
            let routeChanged =
                sendRoutePointsIfChanged(flight)

            let configuration = Configuration(
                page: page,
                flight: flight,
                routeProgress: routeProgress,
                locationName: locationName,
                locationKey: locationKey,
                accent: accent,
                clockCityKeys: clockCityKeys,
                weatherUnits: weatherUnits,
                launch: launch,
                liveAircraft: liveAircraft,
                liveWeather: liveWeather
            )
            if mapChanged ||
                logoChanged ||
                flagsChanged ||
                routeChanged ||
                lastConfiguration != configuration {
                NativeInstrumentRenderer.shared.configure(
                    page: page,
                    flight: flight,
                    routeProgress: routeProgress,
                    locationName: locationName,
                    locationKey: locationKey,
                    accent: accent,
                    clockCityKeys: clockCityKeys,
                    launch: launch
                )
                NativeInstrumentRenderer.shared.setWeatherUnits(
                    weatherUnits
                )
                NativeInstrumentRenderer.shared.setAircraft(liveAircraft)
                NativeInstrumentRenderer.shared.setWeather(liveWeather)
                lastConfiguration = configuration
                needsForcedFrame = true
            }
            drawNextFrame()
        }

        private func sendRoutePointsIfChanged(
            _ flight: TrackedFlight
        ) -> Bool {
            guard lastConfiguration?.flight != flight else {
                return false
            }
            NativeInstrumentRenderer.shared.setRoutePoints(
                for: flight
            )
            return true
        }

        private func sendLogoIfChanged(
            _ image: NSImage?,
            code rawCode: String
        ) -> Bool {
            let code =
                ProjectAssetCatalog.normalizedAirlineCode(rawCode)
            let identifier = image.map(ObjectIdentifier.init)
            guard
                identifier != lastAirlineLogo ||
                code != lastAirlineCode
            else {
                return false
            }
            NativeInstrumentRenderer.shared.setAirlineLogo(
                image,
                code: code
            )
            lastAirlineLogo = identifier
            lastAirlineCode = code
            return true
        }

        private func sendRouteFlagsIfChanged(
            _ flight: TrackedFlight
        ) -> Bool {
            let origin =
                flight.origin.countryCode?
                    .trimmingCharacters(
                        in: .whitespacesAndNewlines
                    )
                    .uppercased() ?? ""
            let destination =
                flight.destination.countryCode?
                    .trimmingCharacters(
                        in: .whitespacesAndNewlines
                    )
                    .uppercased() ?? ""
            guard
                origin != lastOriginFlagCode ||
                destination != lastDestinationFlagCode
            else {
                return false
            }
            NativeInstrumentRenderer.shared.setRouteFlags(
                for: flight
            )
            lastOriginFlagCode = origin
            lastDestinationFlagCode = destination
            return true
        }

        private func sendMapIfChanged(
            _ image: NSImage?,
            slot: Int32,
            lastIdentifier: inout ObjectIdentifier?,
            sentEmpty: inout Bool
        ) -> Bool {
            guard let image else {
                if !sentEmpty {
                    NativeInstrumentRenderer.shared.setMap(
                        nil,
                        slot: slot
                    )
                    sentEmpty = true
                    lastIdentifier = nil
                    return true
                }
                return false
            }

            let identifier = ObjectIdentifier(image)
            guard lastIdentifier != identifier else {
                return false
            }
            do {
                let data = try ImageWriter.rgb565Data(
                    from: image,
                    width: 466,
                    height: 466
                )
                NativeInstrumentRenderer.shared.setMap(
                    data,
                    slot: slot
                )
                lastIdentifier = identifier
                sentEmpty = false
                return true
            } catch {
                NSLog(
                    "Native preview map conversion failed: %@",
                    error.localizedDescription
                )
                return false
            }
        }

        private func drawNextFrame() {
            let force = needsForcedFrame
            guard let image =
                NativeInstrumentRenderer.shared.renderFrame(
                    force: force
                )
            else {
                return
            }
            needsForcedFrame = false
            view?.display(image)
        }
    }
}

@MainActor
final class NativeInstrumentView: NSView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configureLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureLayer()
    }

    private func configureLayer() {
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        layer?.isOpaque = true
        layer?.contentsGravity = .resizeAspectFill
        layer?.magnificationFilter = .linear
        layer?.minificationFilter = .linear
    }

    func display(_ image: CGImage) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        layer?.contents = image
        CATransaction.commit()
    }
}

@MainActor
private final class NativeInstrumentRenderer {
    typealias InitializeFunction =
        @convention(c) (UnsafePointer<CChar>?) -> Bool
    typealias SetPageFunction =
        @convention(c) (Int32) -> Void
    typealias SetAccentFunction =
        @convention(c) (Int32) -> Void
    typealias SetWeatherUnitsFunction =
        @convention(c) (Int32) -> Void
    typealias SetLocationFunction =
        @convention(c) (UnsafePointer<CChar>?) -> Void
    typealias SetClockCitiesFunction =
        @convention(c) (UnsafePointer<CChar>?) -> Void
    typealias SetWeatherFunction =
        @convention(c) (
            Bool, Bool, UnsafePointer<CChar>?, Int32,
            Int32, Int32, Int32, Int32, Int32, Int32,
            Int32, Int32, Int32, Int32,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?
        ) -> Void
    typealias SetWeatherForecastFunction =
        @convention(c) (
            Int32, UnsafePointer<CChar>?, Int32, Int32, Int32, Int32
        ) -> Void
    typealias BeginAircraftFunction =
        @convention(c) (
            Bool, Int32, Int32, Int32, Int32, Float, Bool, Int32
        ) -> Void
    typealias SetAircraftIdentityFunction =
        @convention(c) (
            Int32,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?, Int32, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            UnsafePointer<CChar>?
        ) -> Void
    typealias SetAircraftRouteFunction =
        @convention(c) (
            Int32, UnsafePointer<CChar>?, UnsafePointer<CChar>?,
            Bool, Bool, Bool, Float, Float, Float, Float
        ) -> Void
    typealias SetAircraftMotionFunction =
        @convention(c) (
            Int32, Float, Float, Int32, Int32, Int32, Bool,
            Float, Bool, Int32, Int32, Int32, Float, Int32
        ) -> Void
    typealias SetMapFunction =
        @convention(c) (
            Int32,
            UnsafePointer<UInt8>?,
            Int
        ) -> Void
    typealias SetRouteFlagFunction =
        @convention(c) (
            Int32,
            UnsafePointer<UInt8>?,
            Int
        ) -> Void
    typealias SetAirlineLogoFunction =
        @convention(c) (
            UnsafePointer<CChar>?,
            UnsafePointer<UInt8>?,
            Int,
            Int32,
            Int32
        ) -> Void
    typealias SetFlightFunction =
        @convention(c) (
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            Float,
            Int32,
            Int32,
            Int32
        ) -> Void
    typealias SetFlightTimingFunction =
        @convention(c) (
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            Bool,
            Int32,
            Int32,
            Int32,
            Int32,
            Int32,
            Int64,
            Int64,
            Int64,
            Int64
        ) -> Void
    typealias SetRoutePointsFunction =
        @convention(c) (
            UnsafePointer<Int16>?,
            Int32
        ) -> Void
    typealias ForceTrackedTimePageFunction =
        @convention(c) (Int32) -> Void
    typealias SetLaunchFunction =
        @convention(c) (
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            UnsafePointer<CChar>?,
            Int64,
            Int64,
            Int32,
            Int32
        ) -> Void
    typealias RenderFunction =
        @convention(c) () -> Void
    typealias PixelsFunction =
        @convention(c) () -> UnsafePointer<UInt16>?
    typealias DimensionFunction =
        @convention(c) () -> Int32
    typealias ErrorFunction =
        @convention(c) () -> UnsafePointer<CChar>?

    static let shared = NativeInstrumentRenderer()

    let isAvailable: Bool
    let loadingError: String?

    private let libraryHandle: UnsafeMutableRawPointer?
    private let initialize: InitializeFunction?
    private let setPage: SetPageFunction?
    private let setAccent: SetAccentFunction?
    private let setWeatherUnitsFunction: SetWeatherUnitsFunction?
    private let setLocation: SetLocationFunction?
    private let setClockCities: SetClockCitiesFunction?
    private let setWeatherFunction: SetWeatherFunction?
    private let setWeatherForecastFunction: SetWeatherForecastFunction?
    private let beginAircraftFunction: BeginAircraftFunction?
    private let setAircraftIdentityFunction: SetAircraftIdentityFunction?
    private let setAircraftRouteFunction: SetAircraftRouteFunction?
    private let setAircraftMotionFunction: SetAircraftMotionFunction?
    private let setMapFunction: SetMapFunction?
    private let setRouteFlagFunction: SetRouteFlagFunction?
    private let setAirlineLogoFunction: SetAirlineLogoFunction?
    private let setFlight: SetFlightFunction?
    private let setFlightTiming: SetFlightTimingFunction?
    private let setRoutePointsFunction: SetRoutePointsFunction?
    private let forceTrackedTimePageFunction:
        ForceTrackedTimePageFunction?
    private let setLaunch: SetLaunchFunction?
    private let render: RenderFunction?
    private let pixels: PixelsFunction?
    private let width: DimensionFunction?
    private let height: DimensionFunction?
    private let stride: DimensionFunction?
    private let rendererError: ErrorFunction?
    private var lastFrameSignature: UInt64?

    private init() {
        let candidates = Self.libraryCandidates
        var handle: UnsafeMutableRawPointer?
        for candidate in candidates
        where FileManager.default.fileExists(
            atPath: candidate.path
        ) {
            handle = dlopen(candidate.path, RTLD_NOW | RTLD_LOCAL)
            if handle != nil {
                break
            }
        }
        libraryHandle = handle

        guard let handle else {
            initialize = nil
            setPage = nil
            setAccent = nil
            setWeatherUnitsFunction = nil
            setLocation = nil
            setClockCities = nil
            setWeatherFunction = nil
            setWeatherForecastFunction = nil
            beginAircraftFunction = nil
            setAircraftIdentityFunction = nil
            setAircraftRouteFunction = nil
            setAircraftMotionFunction = nil
            setMapFunction = nil
            setRouteFlagFunction = nil
            setAirlineLogoFunction = nil
            setFlight = nil
            setFlightTiming = nil
            setRoutePointsFunction = nil
            forceTrackedTimePageFunction = nil
            setLaunch = nil
            render = nil
            pixels = nil
            width = nil
            height = nil
            stride = nil
            rendererError = nil
            isAvailable = false
            if let error = dlerror() {
                loadingError = String(cString: error)
            } else {
                loadingError =
                    "libAirspaceRenderer.dylib was not found"
            }
            return
        }

        initialize = Self.symbol(
            "airspace_renderer_initialize",
            in: handle,
            as: InitializeFunction.self
        )
        setPage = Self.symbol(
            "airspace_renderer_set_page",
            in: handle,
            as: SetPageFunction.self
        )
        setAccent = Self.symbol(
            "airspace_renderer_set_accent",
            in: handle,
            as: SetAccentFunction.self
        )
        setWeatherUnitsFunction = Self.symbol(
            "airspace_renderer_set_weather_units",
            in: handle,
            as: SetWeatherUnitsFunction.self
        )
        setLocation = Self.symbol(
            "airspace_renderer_set_location",
            in: handle,
            as: SetLocationFunction.self
        )
        setClockCities = Self.symbol(
            "airspace_renderer_set_clock_cities",
            in: handle,
            as: SetClockCitiesFunction.self
        )
        setWeatherFunction = Self.symbol(
            "airspace_renderer_set_weather",
            in: handle,
            as: SetWeatherFunction.self
        )
        setWeatherForecastFunction = Self.symbol(
            "airspace_renderer_set_weather_forecast",
            in: handle,
            as: SetWeatherForecastFunction.self
        )
        beginAircraftFunction = Self.symbol(
            "airspace_renderer_begin_aircraft",
            in: handle,
            as: BeginAircraftFunction.self
        )
        setAircraftIdentityFunction = Self.symbol(
            "airspace_renderer_set_aircraft_identity",
            in: handle,
            as: SetAircraftIdentityFunction.self
        )
        setAircraftRouteFunction = Self.symbol(
            "airspace_renderer_set_aircraft_route",
            in: handle,
            as: SetAircraftRouteFunction.self
        )
        setAircraftMotionFunction = Self.symbol(
            "airspace_renderer_set_aircraft_motion",
            in: handle,
            as: SetAircraftMotionFunction.self
        )
        setMapFunction = Self.symbol(
            "airspace_renderer_set_map",
            in: handle,
            as: SetMapFunction.self
        )
        setRouteFlagFunction = Self.symbol(
            "airspace_renderer_set_route_flag",
            in: handle,
            as: SetRouteFlagFunction.self
        )
        setAirlineLogoFunction = Self.symbol(
            "airspace_renderer_set_airline_logo",
            in: handle,
            as: SetAirlineLogoFunction.self
        )
        setFlight = Self.symbol(
            "airspace_renderer_set_flight",
            in: handle,
            as: SetFlightFunction.self
        )
        setFlightTiming = Self.symbol(
            "airspace_renderer_set_flight_timing",
            in: handle,
            as: SetFlightTimingFunction.self
        )
        setRoutePointsFunction = Self.symbol(
            "airspace_renderer_set_route_points",
            in: handle,
            as: SetRoutePointsFunction.self
        )
        forceTrackedTimePageFunction = Self.symbol(
            "airspace_renderer_force_tracked_time_page",
            in: handle,
            as: ForceTrackedTimePageFunction.self
        )
        setLaunch = Self.symbol(
            "airspace_renderer_set_launch",
            in: handle,
            as: SetLaunchFunction.self
        )
        render = Self.symbol(
            "airspace_renderer_render",
            in: handle,
            as: RenderFunction.self
        )
        pixels = Self.symbol(
            "airspace_renderer_pixels",
            in: handle,
            as: PixelsFunction.self
        )
        width = Self.symbol(
            "airspace_renderer_width",
            in: handle,
            as: DimensionFunction.self
        )
        height = Self.symbol(
            "airspace_renderer_height",
            in: handle,
            as: DimensionFunction.self
        )
        stride = Self.symbol(
            "airspace_renderer_stride_bytes",
            in: handle,
            as: DimensionFunction.self
        )
        rendererError = Self.symbol(
            "airspace_renderer_error",
            in: handle,
            as: ErrorFunction.self
        )

        let hasEverySymbol =
            initialize != nil &&
            setPage != nil &&
            setAccent != nil &&
            setWeatherUnitsFunction != nil &&
            setLocation != nil &&
            setClockCities != nil &&
            setWeatherFunction != nil &&
            setWeatherForecastFunction != nil &&
            beginAircraftFunction != nil &&
            setAircraftIdentityFunction != nil &&
            setAircraftRouteFunction != nil &&
            setAircraftMotionFunction != nil &&
            setMapFunction != nil &&
            setRouteFlagFunction != nil &&
            setAirlineLogoFunction != nil &&
            setFlight != nil &&
            setFlightTiming != nil &&
            setRoutePointsFunction != nil &&
            forceTrackedTimePageFunction != nil &&
            setLaunch != nil &&
            render != nil &&
            pixels != nil &&
            width != nil &&
            height != nil &&
            stride != nil
        guard hasEverySymbol, let initialize else {
            isAvailable = false
            loadingError =
                "The native renderer is missing a required symbol"
            return
        }

        let initialized =
            Self.assetRoot.path.withCString {
                initialize($0)
            }
        isAvailable = initialized
        if initialized {
            loadingError = nil
        } else if let rendererError,
                  let message = rendererError() {
            loadingError = String(cString: message)
        } else {
            loadingError =
                "The native LVGL renderer did not initialize"
        }
    }

    func setWeather(_ snapshot: DeviceClient.WeatherSnapshot?) {
        guard isAvailable,
              let snapshot,
              let setWeatherFunction,
              let setWeatherForecastFunction
        else {
            return
        }
        let strings = [
            snapshot.locationKey,
            snapshot.unit,
            snapshot.summary,
            snapshot.wind,
            snapshot.sunrise,
            snapshot.sunset,
        ].map { value in
            value.withCString { strdup($0) }
        }
        defer { strings.forEach { free($0) } }
        setWeatherFunction(
            snapshot.weatherValid,
            snapshot.airQualityValid,
            UnsafePointer(strings[0]),
            Int32(snapshot.fetchedUnix),
            Int32(snapshot.temperature),
            Int32(snapshot.apparentTemperature),
            Int32(snapshot.code),
            Int32(snapshot.humidityPercent),
            Int32(snapshot.pressureHPA),
            Int32(snapshot.precipitationTenthsMM),
            Int32(snapshot.uvTenths),
            Int32(snapshot.aqi),
            Int32(snapshot.windKT),
            Int32(snapshot.windDegrees),
            UnsafePointer(strings[1]),
            UnsafePointer(strings[2]),
            UnsafePointer(strings[3]),
            UnsafePointer(strings[4]),
            UnsafePointer(strings[5])
        )
        for (index, day) in snapshot.forecast.prefix(5).enumerated() {
            day.day.withCString { dayPointer in
                setWeatherForecastFunction(
                    Int32(index),
                    dayPointer,
                    Int32(day.code),
                    Int32(day.high),
                    Int32(day.low),
                    Int32(day.precipitationPercent)
                )
            }
        }
    }

    func setWeatherUnits(_ units: WeatherDisplayUnits) {
        guard isAvailable else { return }
        setWeatherUnitsFunction?(
            units == .fahrenheit ? 1 : 0
        )
    }

    func setAircraft(_ snapshot: DeviceClient.AircraftSnapshot?) {
        guard isAvailable,
              let snapshot,
              let beginAircraftFunction,
              let setAircraftIdentityFunction,
              let setAircraftRouteFunction,
              let setAircraftMotionFunction
        else {
            return
        }
        let aircraft = Array(snapshot.aircraft.prefix(16))
        beginAircraftFunction(
            snapshot.aircraftValid,
            Int32(aircraft.count),
            Int32(snapshot.aircraftRawCount),
            Int32(snapshot.aircraftUsableCount),
            Int32(snapshot.aircraftHttpStatus),
            Float(snapshot.aircraftRadiusMiles),
            snapshot.aircraftAuthUsed,
            Int32(snapshot.fetchedUnix)
        )
        for (index, item) in aircraft.enumerated() {
            let identityStrings = [
                item.icao24,
                item.callsign,
                item.typecode,
                item.model,
                item.registration,
                item.manufacturer,
                item.engineType,
                item.typeSource,
                item.airlineName,
                item.airlineIata,
                item.airlineIcao,
            ].map { value in
                value.withCString { strdup($0) }
            }
            defer { identityStrings.forEach { free($0) } }
            setAircraftIdentityFunction(
                Int32(index),
                UnsafePointer(identityStrings[0]),
                UnsafePointer(identityStrings[1]),
                UnsafePointer(identityStrings[2]),
                UnsafePointer(identityStrings[3]),
                UnsafePointer(identityStrings[4]),
                UnsafePointer(identityStrings[5]),
                UnsafePointer(identityStrings[6]),
                Int32(item.engineCount),
                UnsafePointer(identityStrings[7]),
                UnsafePointer(identityStrings[8]),
                UnsafePointer(identityStrings[9]),
                UnsafePointer(identityStrings[10])
            )
            item.origin.withCString { origin in
                item.destination.withCString { destination in
                    setAircraftRouteFunction(
                        Int32(index),
                        origin,
                        destination,
                        item.metadataValid,
                        item.routeValid,
                        item.routeProgressValid,
                        Float(item.routeProgress),
                        Float(item.routeDistanceNm),
                        Float(item.routeDistanceCoveredNm),
                        Float(item.routeDistanceRemainingNm)
                    )
                }
            }
            setAircraftMotionFunction(
                Int32(index),
                Float(item.latitude),
                Float(item.longitude),
                Int32(item.altitudeFt),
                Int32(item.speedKt),
                Int32(item.trackDeg),
                item.trackValid,
                Float(item.verticalRateMps),
                item.verticalRateValid,
                Int32(item.positionUnix),
                Int32(item.contactUnix),
                Int32(item.category),
                Float(item.distanceNm),
                Int32(item.bearingDeg)
            )
        }
    }

    func configure(
        page: DevicePreviewPage,
        flight: TrackedFlight,
        routeProgress: Double,
        locationName: String,
        locationKey: String,
        accent: DeviceAccentColor,
        clockCityKeys: [String],
        launch: TrackedLaunch
    ) {
        guard isAvailable else {
            return
        }

        setAccent?(accent.nativeRendererIndex)
        let normalizedLocationKey = locationKey
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        (normalizedLocationKey.isEmpty ? "SJC" : normalizedLocationKey)
            .withCString {
            setLocation?($0)
        }
        let normalizedClockCities = clockCityKeys
            .map {
                $0.trimmingCharacters(in: .whitespacesAndNewlines)
                    .uppercased()
            }
            .filter { !$0.isEmpty }
            .prefix(5)
            .joined(separator: ",")
        if !normalizedClockCities.isEmpty {
            normalizedClockCities.withCString {
                setClockCities?($0)
            }
        }

        let airlineIATA =
            String(
                flight.marketingNumber.prefix {
                    $0.isLetter
                }
            )
        let aircraftType =
            flight.aircraftTypeCode ??
            AircraftTypeCatalog.resolve(flight.aircraftType)?.code ??
            ""
        let cStrings = [
            flight.marketingNumber,
            flight.operationalCallsign,
            airlineIATA,
            flight.airlineName,
            aircraftType,
            flight.origin.code,
            flight.origin.city,
            flight.destination.code,
            flight.destination.city,
            flight.originTerminal ?? "",
            flight.originGate ?? "",
            flight.destinationTerminal ?? "",
            flight.destinationGate ?? "",
        ].map { string in
            string.withCString { strdup($0) }
        }
        defer {
            cStrings.forEach { free($0) }
        }

        setFlight?(
            UnsafePointer(cStrings[0]),
            UnsafePointer(cStrings[1]),
            UnsafePointer(cStrings[2]),
            UnsafePointer(cStrings[3]),
            UnsafePointer(cStrings[4]),
            UnsafePointer(cStrings[5]),
            UnsafePointer(cStrings[6]),
            UnsafePointer(cStrings[7]),
            UnsafePointer(cStrings[8]),
            UnsafePointer(cStrings[9]),
            UnsafePointer(cStrings[10]),
            UnsafePointer(cStrings[11]),
            UnsafePointer(cStrings[12]),
            Float(routeProgress),
            Int32(flight.altitudeMeters ?? 0),
            Int32(flight.groundSpeedKPH ?? 0),
            Int32(flight.headingDegrees ?? -1)
        )

        let now = Date()
        let estimatedDeparture =
            flight.estimatedDeparture ??
            flight.scheduledDeparture
        let estimatedArrival =
            flight.estimatedArrival ??
            flight.actualArrival
        let departureDate =
            flight.actualDeparture ??
            estimatedDeparture ??
            now
        let arrivalDate =
            flight.actualArrival ??
            estimatedArrival ??
            now
        let originTimeZone =
            flight.origin.timeZoneIdentifier
                .flatMap(TimeZone.init(identifier:))
        let destinationTimeZone =
            flight.destination.timeZoneIdentifier
                .flatMap(TimeZone.init(identifier:))
        let timingStrings = [
            flight.liveStatusSummary,
            flight.verticalPhase ?? "unknown",
            flight.journeyPhase,
            flight.durationSummary,
            flight.elapsedSummary ?? "-",
            flight.remainingSummary ?? "-",
            flight.origin.countryCode ?? "",
            flight.destination.countryCode ?? "",
        ].map { string in
            string.withCString { strdup($0) }
        }
        defer {
            timingStrings.forEach { free($0) }
        }
        setFlightTiming?(
            UnsafePointer(timingStrings[0]),
            UnsafePointer(timingStrings[1]),
            UnsafePointer(timingStrings[2]),
            UnsafePointer(timingStrings[3]),
            UnsafePointer(timingStrings[4]),
            UnsafePointer(timingStrings[5]),
            UnsafePointer(timingStrings[6]),
            UnsafePointer(timingStrings[7]),
            flight.liveStatusSummary
                .localizedCaseInsensitiveContains("delay"),
            Int32(flight.routeDistanceKilometers ?? 0),
            Int32(
                originTimeZone?
                    .secondsFromGMT(for: now) ?? 0
            ),
            Int32(
                destinationTimeZone?
                    .secondsFromGMT(for: now) ?? 0
            ),
            Int32(
                originTimeZone?
                    .secondsFromGMT(
                        for: departureDate
                    ) ?? 0
            ),
            Int32(
                destinationTimeZone?
                    .secondsFromGMT(
                        for: arrivalDate
                    ) ?? 0
            ),
            Self.unixTime(estimatedDeparture),
            Self.unixTime(flight.actualDeparture),
            Self.unixTime(estimatedArrival),
            Self.unixTime(flight.actualArrival)
        )

        let launchStrings = [
            launch.id,
            launch.missionName,
            launch.fullName,
            launch.agency.rawValue,
            launch.vehicle.rawValue,
            launch.site.rawValue,
            launch.padName,
            launch.locationName,
            launch.precisionName,
            launch.precisionAbbreviation,
            launch.statusName,
            launch.statusAbbreviation,
            launch.watchURL?.absoluteString ?? "",
        ].map { string in
            string.withCString { strdup($0) }
        }
        defer {
            launchStrings.forEach { free($0) }
        }
        setLaunch?(
            UnsafePointer(launchStrings[0]),
            UnsafePointer(launchStrings[1]),
            UnsafePointer(launchStrings[2]),
            UnsafePointer(launchStrings[3]),
            UnsafePointer(launchStrings[4]),
            UnsafePointer(launchStrings[5]),
            UnsafePointer(launchStrings[6]),
            UnsafePointer(launchStrings[7]),
            UnsafePointer(launchStrings[8]),
            UnsafePointer(launchStrings[9]),
            UnsafePointer(launchStrings[10]),
            UnsafePointer(launchStrings[11]),
            UnsafePointer(launchStrings[12]),
            Int64(
                launch.net.timeIntervalSince1970
                    .rounded()
            ),
            Int64(
                (launch.lastUpdated ?? Date())
                    .timeIntervalSince1970
                    .rounded()
            ),
            Int32(
                launch.currentUTCOffsetSeconds
            ),
            Int32(
                launch.weatherProbabilityPercent ?? -1
            )
        )
        setPage?(page.nativeRendererIndex)
    }

    private static func unixTime(_ date: Date?) -> Int64 {
        guard let date else {
            return 0
        }
        return Int64(
            date.timeIntervalSince1970.rounded()
        )
    }

    func setMap(_ data: Data?, slot: Int32) {
        guard isAvailable else {
            return
        }
        guard let data else {
            setMapFunction?(slot, nil, 0)
            return
        }
        data.withUnsafeBytes { bytes in
            setMapFunction?(
                slot,
                bytes.bindMemory(to: UInt8.self).baseAddress,
                bytes.count
            )
        }
    }

    func setRouteFlags(for flight: TrackedFlight) {
        setRouteFlag(
            countryCode: flight.origin.countryCode,
            endpoint: 0
        )
        setRouteFlag(
            countryCode: flight.destination.countryCode,
            endpoint: 1
        )
    }

    func setRoutePoints(for flight: TrackedFlight) {
        guard isAvailable else { return }
        let points = MapKitFlightMapRenderer.routeScreenPoints(
            for: flight
        )
        guard points.count >= 2 else {
            setRoutePointsFunction?(nil, 0)
            return
        }
        var coordinates: [Int16] = []
        coordinates.reserveCapacity(points.count * 2)
        for point in points {
            coordinates.append(
                Int16(max(0, min(465, Int(point.x.rounded()))))
            )
            coordinates.append(
                Int16(max(0, min(465, Int(point.y.rounded()))))
            )
        }
        coordinates.withUnsafeBufferPointer { buffer in
            setRoutePointsFunction?(
                buffer.baseAddress,
                Int32(points.count)
            )
        }
    }

    private func setRouteFlag(
        countryCode: String?,
        endpoint: Int32
    ) {
        guard isAvailable else {
            return
        }
        guard
            let data = try? FlagRenderer.rgb565Flag(
                countryCode: countryCode
            )
        else {
            setRouteFlagFunction?(endpoint, nil, 0)
            return
        }
        data.withUnsafeBytes { bytes in
            setRouteFlagFunction?(
                endpoint,
                bytes.bindMemory(
                    to: UInt8.self
                ).baseAddress,
                bytes.count
            )
        }
    }

    func forceTrackedTimePage(_ page: Int) {
        guard isAvailable else {
            return
        }
        forceTrackedTimePageFunction?(
            Int32(max(0, min(1, page)))
        )
    }

    func setAirlineLogo(
        _ image: NSImage?,
        code rawCode: String
    ) {
        guard isAvailable else { return }
        let code =
            ProjectAssetCatalog.normalizedAirlineCode(rawCode)
        guard
            let image,
            !code.isEmpty
        else {
            code.withCString {
                setAirlineLogoFunction?($0, nil, 0, 0, 0)
            }
            return
        }

        let sourceSize = image.size
        guard sourceSize.width > 0, sourceSize.height > 0 else {
            return
        }
        let scale = min(
            132.0 / sourceSize.width,
            52.0 / sourceSize.height,
            1.0
        )
        let width = max(
            1,
            Int(round(sourceSize.width * scale))
        )
        let height = max(
            1,
            Int(round(sourceSize.height * scale))
        )
        guard
            let data = try? ImageWriter.rgb565AlphaData(
                from: image,
                width: width,
                height: height
            )
        else {
            return
        }

        code.withCString { codePointer in
            data.withUnsafeBytes { bytes in
                setAirlineLogoFunction?(
                    codePointer,
                    bytes.bindMemory(
                        to: UInt8.self
                    ).baseAddress,
                    bytes.count,
                    Int32(width),
                    Int32(height)
                )
            }
        }
    }

    func renderFrame(force: Bool = false) -> CGImage? {
        guard
            isAvailable,
            let render,
            let pixels,
            let width,
            let height,
            let stride,
            let source = pixels()
        else {
            return nil
        }
        render()

        let pixelWidth = Int(width())
        let pixelHeight = Int(height())
        let sourceStride = Int(stride())
        guard
            pixelWidth > 0,
            pixelHeight > 0,
            sourceStride >= pixelWidth * 2
        else {
            return nil
        }

        let sourceBytes = UnsafeRawPointer(source)
            .assumingMemoryBound(to: UInt8.self)
        let frameByteCount =
            sourceStride * pixelHeight
        var signature: UInt64 =
            14_695_981_039_346_656_037
        for index in 0..<frameByteCount {
            signature ^= UInt64(sourceBytes[index])
            signature &*= 1_099_511_628_211
        }
        guard
            force ||
            signature != lastFrameSignature
        else {
            return nil
        }
        lastFrameSignature = signature

        var rgba = Data(
            count: pixelWidth * pixelHeight * 4
        )
        rgba.withUnsafeMutableBytes { outputBytes in
            guard let output =
                outputBytes.bindMemory(
                    to: UInt8.self
                ).baseAddress
            else {
                return
            }
            for y in 0..<pixelHeight {
                let row = UnsafeRawPointer(source)
                    .advanced(by: y * sourceStride)
                    .assumingMemoryBound(to: UInt16.self)
                for x in 0..<pixelWidth {
                    let rgb565 = row[x]
                    let offset =
                        (y * pixelWidth + x) * 4
                    output[offset] = UInt8(
                        ((rgb565 >> 11) & 0x1f) * 255 / 31
                    )
                    output[offset + 1] = UInt8(
                        ((rgb565 >> 5) & 0x3f) * 255 / 63
                    )
                    output[offset + 2] = UInt8(
                        (rgb565 & 0x1f) * 255 / 31
                    )
                    output[offset + 3] = 255
                }
            }
        }

        guard let provider = CGDataProvider(
            data: rgba as CFData
        ) else {
            return nil
        }
        return CGImage(
            width: pixelWidth,
            height: pixelHeight,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: pixelWidth * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(
                rawValue:
                    CGImageAlphaInfo.noneSkipLast.rawValue
            ),
            provider: provider,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        )
    }

    private static func symbol<T>(
        _ name: String,
        in handle: UnsafeMutableRawPointer,
        as type: T.Type
    ) -> T? {
        guard let pointer = dlsym(handle, name) else {
            return nil
        }
        return unsafeBitCast(pointer, to: type)
    }

    private static var libraryCandidates: [URL] {
        var candidates: [URL] = []
        if let configured =
            ProcessInfo.processInfo.environment[
                "AIRSPACE_RENDERER_LIBRARY"
            ],
           !configured.isEmpty {
            candidates.append(
                URL(fileURLWithPath: configured)
            )
        }
        if let frameworks =
            Bundle.main.privateFrameworksURL {
            candidates.append(
                frameworks.appendingPathComponent(
                    "libAirspaceRenderer.dylib"
                )
            )
        }
        if let executable =
            Bundle.main.executableURL {
            candidates.append(
                executable
                    .deletingLastPathComponent()
                    .appendingPathComponent(
                        "libAirspaceRenderer.dylib"
                    )
            )
        }
        candidates.append(
            repositoryRoot
                .appendingPathComponent(
                    "shared/native_renderer/build/" +
                    "libAirspaceRenderer.dylib"
                )
        )
        return candidates
    }

    private static var assetRoot: URL {
        ProjectAssetCatalog.assetRoot
    }

    private static var repositoryRoot: URL {
        #if DEBUG
            var directory =
                URL(fileURLWithPath: #filePath)
                    .deletingLastPathComponent()
            for _ in 0..<6 {
                let marker =
                    directory.appendingPathComponent(
                        "firmware/airspace-instrument",
                        isDirectory: true
                    )
                if FileManager.default.fileExists(
                    atPath: marker.path
                ) {
                    return directory
                }
                directory.deleteLastPathComponent()
            }
        #endif
        return URL(fileURLWithPath:
            FileManager.default.currentDirectoryPath)
    }
}

private extension DevicePreviewPage {
    var nativeRendererIndex: Int32 {
        Int32(
            DevicePreviewPage.allCases.firstIndex(
                of: self
            ) ?? 0
        )
    }
}

private extension DeviceAccentColor {
    var nativeRendererIndex: Int32 {
        switch self {
        case .green:
            0
        case .cyan:
            1
        case .orange:
            2
        case .red:
            3
        }
    }
}
