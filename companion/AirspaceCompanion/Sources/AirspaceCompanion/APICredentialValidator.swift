// AirESP32ace
// Created by VM, August 2026

import Foundation

enum APICredentialState: Equatable, Sendable {
    case checking
    case valid
    case invalid(String)
    case unavailable(String)

    var isValid: Bool {
        if case .valid = self { return true }
        return false
    }

    var detail: String? {
        if case .invalid(let detail) = self { return detail }
        if case .unavailable(let detail) = self { return detail }
        return nil
    }
}

enum APIProvider: CaseIterable, Hashable, Sendable {
    case flightAware
    case openSky
    case airLabs
}

struct APICredentialValidator: Sendable {
    private let session: URLSession

    init(session: URLSession = .shared) {
        self.session = session
    }

    private static func failure(_ code: Int) -> APICredentialState {
        switch code {
        case 401, 403: .invalid("Authorization rejected (HTTP \(code))")
        case 429: .unavailable("Provider rate limit reached; key not reverified")
        default: .unavailable("Provider unavailable (HTTP \(code)); key not reverified")
        }
    }

    func validateFlightAware(apiKey: String) async -> APICredentialState {
        let key = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty else {
            return .invalid("API key not provided")
        }
        guard let url = URL(
            string: "https://aeroapi.flightaware.com/aeroapi/account/usage"
        ) else {
            return .invalid("Could not build authorization request")
        }

        var request = URLRequest(url: url)
        request.timeoutInterval = 15
        request.setValue(key, forHTTPHeaderField: "x-apikey")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, response) = try await session.data(for: request)
            guard let http = response as? HTTPURLResponse else {
                return .unavailable("FlightAware returned an invalid response")
            }
            guard (200..<300).contains(http.statusCode) else {
                return Self.failure(http.statusCode)
            }
            guard http.statusCode == 200,
                  (try? JSONSerialization.jsonObject(with: data)) is [String: Any] else {
                return .unavailable("FlightAware returned an invalid response")
            }
            return .valid
        } catch {
            return .unavailable(Self.safeMessage(error))
        }
    }

    func validateOpenSky(
        clientID: String,
        clientSecret: String
    ) async -> APICredentialState {
        let id = clientID.trimmingCharacters(in: .whitespacesAndNewlines)
        let secret = clientSecret.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !id.isEmpty, !secret.isEmpty else {
            return .invalid("Client ID and secret are both required")
        }
        guard let url = URL(
            string: "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
        ) else {
            return .invalid("Could not build authorization request")
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 15
        request.setValue(
            "application/x-www-form-urlencoded",
            forHTTPHeaderField: "Content-Type"
        )
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.httpBody = [
            ("grant_type", "client_credentials"),
            ("client_id", id),
            ("client_secret", secret),
        ]
        .map { "\($0.0)=\(Self.formEncode($0.1))" }
        .joined(separator: "&")
        .data(using: .utf8)

        do {
            let (data, response) = try await session.data(for: request)
            guard let http = response as? HTTPURLResponse else {
                return .unavailable("OpenSky returned an invalid response")
            }
            guard (200..<300).contains(http.statusCode) else {
                if http.statusCode == 400,
                   let failure = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
                   failure["error"] as? String == "invalid_client" {
                    return .invalid("Client credentials rejected")
                }
                return Self.failure(http.statusCode)
            }
            let object = try JSONSerialization.jsonObject(with: data)
            guard let root = object as? [String: Any],
                  let token = root["access_token"] as? String,
                  !token.isEmpty else {
                return .unavailable("Authorization response contained no token")
            }
            return .valid
        } catch {
            return .unavailable(Self.safeMessage(error))
        }
    }

    func validateAirLabs(apiKey: String) async -> APICredentialState {
        let key = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty else {
            return .invalid("API key not provided")
        }
        guard let url = URL(string: "https://airlabs.co/api/v9/ping") else {
            return .invalid("Could not build authorization request")
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = "api_key=\(Self.formEncode(key))".data(using: .utf8)
        request.timeoutInterval = 15
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, response) = try await session.data(for: request)
            guard let http = response as? HTTPURLResponse else {
                return .unavailable("AirLabs returned an invalid response")
            }
            guard (200..<300).contains(http.statusCode) else {
                return Self.failure(http.statusCode)
            }
            guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                return .unavailable("AirLabs returned an invalid response")
            }
            if let error = root["error"],
               !(error is NSNull) {
                let code = (error as? [String: Any])?["code"] as? String ?? ""
                if code == "unknown_api_key" || code == "expired_api_key" { return Self.failure(401) }
                if code.hasSuffix("_limit_exceeded") { return Self.failure(429) }
                return .unavailable("AirLabs could not verify the key")
            }
            return .valid
        } catch {
            return .unavailable(Self.safeMessage(error))
        }
    }

    private static func safeMessage(_ error: Error) -> String {
        if let urlError = error as? URLError {
            switch urlError.code {
            case .timedOut:
                return "Authorization check timed out"
            case .notConnectedToInternet, .networkConnectionLost:
                return "Network unavailable"
            default:
                return "Authorization check failed"
            }
        }
        return "Authorization check failed"
    }

    private static func formEncode(_ value: String) -> String {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&+=")
        return value.addingPercentEncoding(
            withAllowedCharacters: allowed
        ) ?? value
    }
}
