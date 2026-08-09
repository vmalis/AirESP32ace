// AirESP32ace
// Created by VM, August 2026

import CoreLocation
import Foundation

/// Live data used by the Mac digital twin. This intentionally does not read
/// the instrument's current screen state: the preview follows the city chosen
/// in the companion, even before that configuration is synced to the device.
struct CompanionPreviewDataClient: Sendable {
    private let requestTimeout: TimeInterval = 12

    func fetchAircraft(
        location: RadarLocationPreset,
        radiusMiles: Int,
        clientID: String,
        clientSecret: String
    ) async throws -> DeviceClient.AircraftSnapshot {
        let radius = Double(max(1, radiusMiles))
        let latitude = location.coordinate.latitude
        let longitude = location.coordinate.longitude
        let latitudeDelta = radius / 69.0
        let longitudeScale = max(0.15, cos(latitude * .pi / 180.0))
        let longitudeDelta = radius / (69.0 * longitudeScale)

        var components = URLComponents(
            string: "https://opensky-network.org/api/states/all"
        )
        components?.queryItems = [
            .init(name: "lamin", value: Self.number(latitude - latitudeDelta)),
            .init(name: "lomin", value: Self.number(longitude - longitudeDelta)),
            .init(name: "lamax", value: Self.number(latitude + latitudeDelta)),
            .init(name: "lomax", value: Self.number(longitude + longitudeDelta)),
            .init(name: "extended", value: "1"),
        ]
        guard let url = components?.url else {
            throw PreviewDataError.badURL("OpenSky")
        }

        let trimmedID = clientID.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedSecret = clientSecret.trimmingCharacters(in: .whitespacesAndNewlines)
        let authenticated = !trimmedID.isEmpty && !trimmedSecret.isEmpty
        let token = authenticated
            ? try await OpenSkyPreviewTokenStore.shared.token(
                clientID: trimmedID,
                clientSecret: trimmedSecret
            )
            : nil

        var request = URLRequest(url: url)
        request.timeoutInterval = requestTimeout
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if let token {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }

        let (data, response) = try await URLSession.shared.data(for: request)
        try Self.validate(response, data: data, service: "OpenSky")
        guard let payload = try JSONSerialization.jsonObject(with: data)
                as? [String: Any] else {
            throw PreviewDataError.badPayload("OpenSky")
        }

        let rawStates = payload["states"] as? [[Any]] ?? []
        let now = Int(Date().timeIntervalSince1970)
        let parsed = rawStates.compactMap { row -> DeviceClient.AircraftSnapshot.Aircraft? in
            guard row.count > 10,
                  let icao24 = Self.string(row, 0),
                  let aircraftLatitude = Self.double(row, 6),
                  let aircraftLongitude = Self.double(row, 5) else {
                return nil
            }
            let distanceMiles = Self.distanceMiles(
                latitude,
                longitude,
                aircraftLatitude,
                aircraftLongitude
            )
            guard distanceMiles <= radius else { return nil }

            let barometricAltitudeMeters = Self.double(row, 7)
            let geometricAltitudeMeters = Self.double(row, 13)
            let altitudeMeters = geometricAltitudeMeters ??
                barometricAltitudeMeters ?? 0
            let speedMetersPerSecond = Self.double(row, 9) ?? 0
            let track = Self.double(row, 10)
            let verticalRate = Self.double(row, 11)
            let category = Self.int(row, 17) ?? 0
            let callsign = Self.string(row, 1)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let bearing = Self.bearingDegrees(
                latitude,
                longitude,
                aircraftLatitude,
                aircraftLongitude
            )

            return .init(
                icao24: icao24.lowercased(),
                callsign: callsign,
                typecode: "",
                model: "",
                registration: "",
                manufacturer: "",
                engineType: "",
                engineCount: 0,
                typeSource: "OpenSky",
                airlineName: "",
                airlineIata: "",
                airlineIcao: "",
                origin: "",
                destination: "",
                originCity: "",
                destinationCity: "",
                metadataValid: false,
                routeValid: false,
                routeProgressValid: false,
                routeProgress: 0,
                routeDistanceNm: 0,
                routeDistanceCoveredNm: 0,
                routeDistanceRemainingNm: 0,
                iconLabel: "",
                latitude: aircraftLatitude,
                longitude: aircraftLongitude,
                altitudeFt: Int((altitudeMeters * 3.28084).rounded()),
                speedKt: Int((speedMetersPerSecond * 1.94384).rounded()),
                trackDeg: Int((track ?? 0).rounded()),
                trackValid: track != nil,
                verticalRateMps: verticalRate ?? 0,
                verticalRateValid: verticalRate != nil,
                positionUnix: Self.int(row, 3) ?? now,
                contactUnix: Self.int(row, 4) ?? now,
                category: category,
                distanceNm: distanceMiles * 0.868976,
                bearingDeg: Int(bearing.rounded())
            )
        }
        .sorted { lhs, rhs in
            let lhsCommercial = Self.looksCommercial(lhs.callsign)
            let rhsCommercial = Self.looksCommercial(rhs.callsign)
            if lhsCommercial != rhsCommercial { return lhsCommercial }
            return lhs.distanceNm < rhs.distanceNm
        }

        let aircraft = Array(parsed.prefix(32))
        return .init(
            ok: true,
            aircraftValid: true,
            aircraftCount: aircraft.count,
            aircraftRawCount: rawStates.count,
            aircraftUsableCount: parsed.count,
            aircraftHttpStatus: 200,
            aircraftRadiusMiles: radius,
            aircraftAuthUsed: authenticated,
            fetchedUnix: now,
            nowUnix: now,
            aircraft: aircraft
        )
    }

    func fetchWeather(
        location: RadarLocationPreset
    ) async throws -> DeviceClient.WeatherSnapshot {
        var components = URLComponents(
            string: "https://api.open-meteo.com/v1/forecast"
        )
        components?.queryItems = [
            .init(name: "latitude", value: Self.number(location.coordinate.latitude)),
            .init(name: "longitude", value: Self.number(location.coordinate.longitude)),
            .init(
                name: "current",
                value: [
                    "temperature_2m",
                    "apparent_temperature",
                    "relative_humidity_2m",
                    "surface_pressure",
                    "precipitation",
                    "weather_code",
                    "wind_speed_10m",
                    "wind_direction_10m",
                ].joined(separator: ",")
            ),
            .init(
                name: "daily",
                value: [
                    "weather_code",
                    "temperature_2m_max",
                    "temperature_2m_min",
                    "precipitation_probability_max",
                    "sunrise",
                    "sunset",
                    "uv_index_max",
                ].joined(separator: ",")
            ),
            .init(name: "temperature_unit", value: "celsius"),
            .init(name: "wind_speed_unit", value: "kn"),
            .init(name: "timezone", value: "auto"),
            .init(name: "forecast_days", value: "5"),
        ]
        guard let url = components?.url else {
            throw PreviewDataError.badURL("Open-Meteo")
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = requestTimeout
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let (data, response) = try await URLSession.shared.data(for: request)
        try Self.validate(response, data: data, service: "Open-Meteo")
        guard let payload = try JSONSerialization.jsonObject(with: data)
                as? [String: Any],
              let current = payload["current"] as? [String: Any],
              let daily = payload["daily"] as? [String: Any] else {
            throw PreviewDataError.badPayload("Open-Meteo")
        }

        let temperature = Self.number(current["temperature_2m"])
        let apparent = Self.number(current["apparent_temperature"])
        let humidity = Self.number(current["relative_humidity_2m"])
        let pressure = Self.number(current["surface_pressure"])
        let precipitation = Self.number(current["precipitation"])
        let weatherCode = Int(Self.number(current["weather_code"]).rounded())
        let windKT = Self.number(current["wind_speed_10m"])
        let windDegrees = Int(Self.number(current["wind_direction_10m"]).rounded())

        let dates = daily["time"] as? [String] ?? []
        let codes = Self.numberArray(daily["weather_code"])
        let highs = Self.numberArray(daily["temperature_2m_max"])
        let lows = Self.numberArray(daily["temperature_2m_min"])
        let rain = Self.numberArray(daily["precipitation_probability_max"])
        let sunriseValues = daily["sunrise"] as? [String] ?? []
        let sunsetValues = daily["sunset"] as? [String] ?? []
        let uvValues = Self.numberArray(daily["uv_index_max"])
        let forecast = dates.indices.prefix(5).map { index in
            DeviceClient.WeatherSnapshot.ForecastDay(
                day: Self.weekday(dates[index]),
                code: Int(Self.value(codes, at: index).rounded()),
                high: Int(Self.value(highs, at: index).rounded()),
                low: Int(Self.value(lows, at: index).rounded()),
                precipitationPercent: Int(Self.value(rain, at: index).rounded())
            )
        }
        let now = Int(Date().timeIntervalSince1970)
        return .init(
            ok: true,
            weatherValid: true,
            airQualityValid: false,
            locationKey: location.key,
            fetchedUnix: now,
            temperature: Int(temperature.rounded()),
            apparentTemperature: Int(apparent.rounded()),
            code: weatherCode,
            humidityPercent: Int(humidity.rounded()),
            pressureHPA: Int(pressure.rounded()),
            precipitationTenthsMM: Int((precipitation * 10).rounded()),
            uvTenths: Int((Self.value(uvValues, at: 0) * 10).rounded()),
            aqi: 0,
            windKT: Int(windKT.rounded()),
            windDegrees: windDegrees,
            unit: "C",
            summary: Self.weatherSummary(weatherCode),
            wind: Self.compass(windDegrees),
            sunrise: Self.clockTime(sunriseValues.first),
            sunset: Self.clockTime(sunsetValues.first),
            forecast: forecast
        )
    }

    fileprivate static func validate(
        _ response: URLResponse,
        data: Data,
        service: String
    ) throws {
        guard let http = response as? HTTPURLResponse else {
            throw PreviewDataError.badResponse(service)
        }
        guard (200..<300).contains(http.statusCode) else {
            let body = String(data: data, encoding: .utf8) ?? ""
            throw PreviewDataError.httpStatus(service, http.statusCode, body)
        }
    }

    private static func string(_ row: [Any], _ index: Int) -> String? {
        guard row.indices.contains(index), !(row[index] is NSNull) else {
            return nil
        }
        return row[index] as? String
    }

    private static func double(_ row: [Any], _ index: Int) -> Double? {
        guard row.indices.contains(index), !(row[index] is NSNull) else {
            return nil
        }
        return (row[index] as? NSNumber)?.doubleValue
    }

    private static func int(_ row: [Any], _ index: Int) -> Int? {
        double(row, index).map { Int($0.rounded()) }
    }

    private static func number(_ value: Any?) -> Double {
        (value as? NSNumber)?.doubleValue ?? 0
    }

    private static func numberArray(_ value: Any?) -> [Double] {
        (value as? [Any])?.map(number) ?? []
    }

    private static func value(_ values: [Double], at index: Int) -> Double {
        values.indices.contains(index) ? values[index] : 0
    }

    private static func number(_ value: Double) -> String {
        String(format: "%.5f", value)
    }

    private static func distanceMiles(
        _ latitudeA: Double,
        _ longitudeA: Double,
        _ latitudeB: Double,
        _ longitudeB: Double
    ) -> Double {
        let earthRadiusMiles = 3_958.7613
        let lat1 = latitudeA * .pi / 180
        let lat2 = latitudeB * .pi / 180
        let deltaLat = (latitudeB - latitudeA) * .pi / 180
        let deltaLon = (longitudeB - longitudeA) * .pi / 180
        let a = sin(deltaLat / 2) * sin(deltaLat / 2) +
            cos(lat1) * cos(lat2) * sin(deltaLon / 2) * sin(deltaLon / 2)
        return earthRadiusMiles * 2 * atan2(sqrt(a), sqrt(max(0, 1 - a)))
    }

    private static func bearingDegrees(
        _ latitudeA: Double,
        _ longitudeA: Double,
        _ latitudeB: Double,
        _ longitudeB: Double
    ) -> Double {
        let lat1 = latitudeA * .pi / 180
        let lat2 = latitudeB * .pi / 180
        let deltaLon = (longitudeB - longitudeA) * .pi / 180
        let y = sin(deltaLon) * cos(lat2)
        let x = cos(lat1) * sin(lat2) -
            sin(lat1) * cos(lat2) * cos(deltaLon)
        let result = atan2(y, x) * 180 / .pi
        return result < 0 ? result + 360 : result
    }

    private static func looksCommercial(_ callsign: String) -> Bool {
        let normalized = callsign.uppercased()
        let prefix = normalized.prefix { $0.isLetter }
        return prefix.count == 3 && normalized.contains { $0.isNumber }
    }

    private static func weekday(_ isoDate: String) -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        guard let date = formatter.date(from: isoDate) else {
            return String(isoDate.prefix(3)).uppercased()
        }
        formatter.dateFormat = "EEE"
        return formatter.string(from: date).uppercased()
    }

    private static func clockTime(_ isoLocal: String?) -> String {
        guard let isoLocal,
              let time = isoLocal.split(separator: "T").last else {
            return "-"
        }
        return String(time.prefix(5))
    }

    private static func compass(_ degrees: Int) -> String {
        let names = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        let normalized = (degrees % 360 + 360) % 360
        return names[Int((Double(normalized) / 45.0).rounded()) % names.count]
    }

    private static func weatherSummary(_ code: Int) -> String {
        switch code {
        case 0: "Clear"
        case 1, 2: "Partly cloudy"
        case 3: "Overcast"
        case 45, 48: "Fog"
        case 51...57: "Drizzle"
        case 61...67: "Rain"
        case 71...77: "Snow"
        case 80...82: "Showers"
        case 85, 86: "Snow showers"
        case 95...99: "Thunderstorm"
        default: "Weather"
        }
    }
}

private actor OpenSkyPreviewTokenStore {
    static let shared = OpenSkyPreviewTokenStore()

    private var cachedToken: String?
    private var cachedClientID = ""
    private var expiry = Date.distantPast

    func token(clientID: String, clientSecret: String) async throws -> String {
        if cachedClientID == clientID,
           let cachedToken,
           expiry.timeIntervalSinceNow > 60 {
            return cachedToken
        }
        guard let url = URL(
            string: "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
        ) else {
            throw PreviewDataError.badURL("OpenSky authentication")
        }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 12
        request.setValue(
            "application/x-www-form-urlencoded",
            forHTTPHeaderField: "Content-Type"
        )
        request.httpBody = [
            "grant_type=client_credentials",
            "client_id=\(Self.formEncode(clientID))",
            "client_secret=\(Self.formEncode(clientSecret))",
        ].joined(separator: "&").data(using: .utf8)
        let (data, response) = try await URLSession.shared.data(for: request)
        try CompanionPreviewDataClient.validate(
            response,
            data: data,
            service: "OpenSky authentication"
        )
        guard let object = try JSONSerialization.jsonObject(with: data)
                as? [String: Any],
              let token = object["access_token"] as? String,
              !token.isEmpty else {
            throw PreviewDataError.badPayload("OpenSky authentication")
        }
        let expiresIn = (object["expires_in"] as? NSNumber)?.doubleValue ?? 1_800
        cachedToken = token
        cachedClientID = clientID
        expiry = Date().addingTimeInterval(expiresIn)
        return token
    }

    private static func formEncode(_ value: String) -> String {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }
}

private enum PreviewDataError: LocalizedError {
    case badURL(String)
    case badResponse(String)
    case badPayload(String)
    case httpStatus(String, Int, String)

    var errorDescription: String? {
        switch self {
        case .badURL(let service):
            "Could not build the \(service) request."
        case .badResponse(let service):
            "\(service) returned a non-HTTP response."
        case .badPayload(let service):
            "\(service) returned an unexpected response."
        case .httpStatus(let service, let status, let body):
            "\(service) HTTP \(status): \(body.prefix(160))"
        }
    }
}
