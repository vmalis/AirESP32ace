// AirESP32ace
// Created by VM, August 2026

import Foundation

struct OpenSkyDiagnosticClient {
    struct Result {
        let authenticated: Bool
        let aircraftCount: Int
        let sampleCallsigns: [String]

        var summary: String {
            let source = authenticated ? "authenticated" : "anonymous"
            let sample = sampleCallsigns.isEmpty ? "no callsigns" : sampleCallsigns.joined(separator: ", ")
            return "OpenSky \(source): \(aircraftCount) aircraft · \(sample)"
        }
    }

    func testNearby(
        clientID: String,
        clientSecret: String,
        latitude: String,
        longitude: String,
        halfDegrees: String
    ) async throws -> Result {
        guard let lat = Double(latitude.trimmingCharacters(in: .whitespacesAndNewlines)),
              let lon = Double(longitude.trimmingCharacters(in: .whitespacesAndNewlines)),
              let half = Double(halfDegrees.trimmingCharacters(in: .whitespacesAndNewlines)) else {
            throw OpenSkyDiagnosticError.badLocation
        }

        let trimmedClientID = clientID.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedClientSecret = clientSecret.trimmingCharacters(in: .whitespacesAndNewlines)
        let token: String?
        if trimmedClientID.isEmpty || trimmedClientSecret.isEmpty {
            token = nil
        } else {
            token = try await fetchToken(clientID: trimmedClientID, clientSecret: trimmedClientSecret)
        }

        var components = URLComponents(string: "https://opensky-network.org/api/states/all")
        components?.queryItems = [
            URLQueryItem(name: "lamin", value: String(format: "%.4f", lat - half)),
            URLQueryItem(name: "lomin", value: String(format: "%.4f", lon - half)),
            URLQueryItem(name: "lamax", value: String(format: "%.4f", lat + half)),
            URLQueryItem(name: "lomax", value: String(format: "%.4f", lon + half)),
            URLQueryItem(name: "extended", value: "1")
        ]
        guard let url = components?.url else {
            throw OpenSkyDiagnosticError.badURL
        }

        var request = URLRequest(url: url)
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if let token {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }

        let (data, response) = try await URLSession.shared.data(for: request)
        try validate(response: response, data: data, service: "OpenSky states")

        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw OpenSkyDiagnosticError.badPayload
        }
        let states = object["states"] as? [[Any?]] ?? []
        let allCallsigns: [String] = states
            .compactMap { row -> String? in
                guard row.indices.contains(1), let raw = row[1] as? String else { return nil }
                let callsign = raw.trimmingCharacters(in: .whitespacesAndNewlines)
                return callsign.isEmpty ? nil : callsign
            }
        let callsigns = Array(allCallsigns.prefix(5))

        return Result(
            authenticated: token != nil,
            aircraftCount: states.count,
            sampleCallsigns: callsigns
        )
    }

    private func fetchToken(clientID: String, clientSecret: String) async throws -> String {
        guard let url = URL(string: "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token") else {
            throw OpenSkyDiagnosticError.badURL
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = [
            ("grant_type", "client_credentials"),
            ("client_id", clientID),
            ("client_secret", clientSecret)
        ]
        .map { "\($0.0)=\(Self.formEncode($0.1))" }
        .joined(separator: "&")
        .data(using: .utf8)

        let (data, response) = try await URLSession.shared.data(for: request)
        try validate(response: response, data: data, service: "OpenSky auth")

        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let token = object["access_token"] as? String,
              !token.isEmpty else {
            throw OpenSkyDiagnosticError.badPayload
        }

        return token
    }

    private func validate(response: URLResponse, data: Data, service: String) throws {
        guard let http = response as? HTTPURLResponse else {
            throw OpenSkyDiagnosticError.badResponse(service)
        }
        guard (200..<300).contains(http.statusCode) else {
            let body = String(data: data, encoding: .utf8) ?? ""
            throw OpenSkyDiagnosticError.httpStatus(service, http.statusCode, body)
        }
    }

    private static func formEncode(_ value: String) -> String {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }
}

enum OpenSkyDiagnosticError: LocalizedError {
    case badLocation
    case badURL
    case badResponse(String)
    case badPayload
    case httpStatus(String, Int, String)

    var errorDescription: String? {
        switch self {
        case .badLocation:
            "OpenSky test needs valid latitude, longitude, and half-degree radius."
        case .badURL:
            "Could not build OpenSky diagnostic URL."
        case .badResponse(let service):
            "\(service) returned a non-HTTP response."
        case .badPayload:
            "OpenSky returned an unexpected payload."
        case .httpStatus(let service, let status, let body):
            "\(service) HTTP \(status): \(body)"
        }
    }
}
