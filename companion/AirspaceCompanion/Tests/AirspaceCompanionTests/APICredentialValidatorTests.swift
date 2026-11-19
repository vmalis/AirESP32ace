// AirESP32ace
// Created by VM, August 2026

import Foundation
import XCTest
@testable import AirspaceCompanion

final class APICredentialValidatorTests: XCTestCase {
    override func tearDown() {
        APICredentialURLProtocol.handler = nil
        super.tearDown()
    }

    func testFlightAwareUsesZeroCostAccountUsageAuthorizationProbe() async {
        let validator = makeValidator { request in
            XCTAssertEqual(request.url?.path, "/aeroapi/account/usage")
            XCTAssertNil(
                URLComponents(
                    url: try XCTUnwrap(request.url),
                    resolvingAgainstBaseURL: false
                )?.query
            )
            XCTAssertEqual(
                request.value(forHTTPHeaderField: "x-apikey"),
                "active-key"
            )
            return Self.response(for: request, statusCode: 200)
        }

        let result = await validator.validateFlightAware(apiKey: "active-key")

        XCTAssertEqual(result, .valid)
    }

    func testFlightAwareDisabledKeyIsRejected() async {
        let validator = makeValidator { request in
            Self.response(
                for: request,
                statusCode: 401,
                body: [
                    "title": "Invalid API key",
                    "status": 401,
                ]
            )
        }

        let result = await validator.validateFlightAware(apiKey: "disabled-key")

        XCTAssertEqual(
            result,
            .invalid("Authorization rejected (HTTP 401)")
        )
    }

    func testFlightAwareTransientFailuresDoNotInvalidateKey() async {
        for code in [429, 503] {
            let validator = makeValidator { Self.response(for: $0, statusCode: code) }
            let result = await validator.validateFlightAware(apiKey: "test")
            guard case .unavailable = result else { return XCTFail("Unexpected \(result)") }
        }
        let offline = makeValidator { _ in throw URLError(.notConnectedToInternet) }
        let status = await offline.validateFlightAware(apiKey: "test")
        XCTAssertEqual(status, .unavailable("Network unavailable"))
    }

    func testAirLabsUsesFormAndClassifiesProviderErrors() async {
        for (code, rejected) in [("unknown_api_key", true), ("expired_api_key", true),
                                 ("month_limit_exceeded", false), ("internal_error", false)] {
            let validator = makeValidator { request in
                XCTAssertNil(request.url?.query)
                XCTAssertEqual(request.httpMethod, "POST")
                return Self.response(for: request, statusCode: 200, body: ["error":["code":code]])
            }
            let status = await validator.validateAirLabs(apiKey: "test")
            if rejected {
                guard case .invalid = status else { return XCTFail("Expected rejection") }
            } else {
                guard case .unavailable = status else { return XCTFail("Expected temporary failure") }
            }
        }
    }

    private func makeValidator(
        handler: @escaping (URLRequest) throws -> (HTTPURLResponse, Data)
    ) -> APICredentialValidator {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [APICredentialURLProtocol.self]
        APICredentialURLProtocol.handler = handler
        return APICredentialValidator(
            session: URLSession(configuration: configuration)
        )
    }

    private static func response(
        for request: URLRequest,
        statusCode: Int,
        body: [String: Any] = ["total_cost": 0]
    ) -> (HTTPURLResponse, Data) {
        (
            HTTPURLResponse(
                url: request.url!,
                statusCode: statusCode,
                httpVersion: nil,
                headerFields: nil
            )!,
            try! JSONSerialization.data(withJSONObject: body)
        )
    }
}

private final class APICredentialURLProtocol: URLProtocol, @unchecked Sendable {
    nonisolated(unsafe) static var handler:
        ((URLRequest) throws -> (HTTPURLResponse, Data))?

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(
        for request: URLRequest
    ) -> URLRequest { request }

    override func startLoading() {
        do {
            guard let handler = Self.handler else {
                throw URLError(.badServerResponse)
            }
            let (response, data) = try handler(request)
            client?.urlProtocol(
                self,
                didReceive: response,
                cacheStoragePolicy: .notAllowed
            )
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        } catch {
            client?.urlProtocol(self, didFailWithError: error)
        }
    }

    override func stopLoading() {}
}
