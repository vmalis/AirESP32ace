// AirESP32ace
// Created by VM, November 2026

import Foundation
import Security

struct BoardIdentity: Decodable, Sendable {
    let deviceId: String
    let elfSHA256: String
    let firmwareVersion: String
    let ip: String
}

struct DevicePairing: Codable, Equatable, Sendable {
    let deviceId: String
    let certificate: Data
    let token: String

    static func stored() -> Self? {
        guard let value = LocalCredentialStore(service: "AirESP32acePairing").string(for: "board"),
              let bytes = Data(base64Encoded: value) else { return nil }
        return try? JSONDecoder().decode(Self.self, from: bytes)
    }

    func save() throws {
        let store = LocalCredentialStore(service: "AirESP32acePairing")
        store.set(try JSONEncoder().encode(self).base64EncodedString(), for: "board")
        guard Self.stored() == self else { throw PairingFailure.storage }
    }

    // Apple's system openssl is used only on explicit pairing, not at launch or
    // in the ordinary sync path. Never ship a keypair shared by multiple boards.
    static func make(for id: String) throws -> (Self, String) {
        guard id.utf8.count == 12, id.allSatisfy(\.isHexDigit) else { throw PairingFailure.identity }
        let fm = FileManager.default
        let dir = fm.temporaryDirectory.appendingPathComponent("AirESP32ace-Pair-\(UUID().uuidString)")
        try fm.createDirectory(at: dir, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        defer { try? fm.removeItem(at: dir) }
        func openssl(_ arguments: [String]) throws {
            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/usr/bin/openssl")
            process.currentDirectoryURL = dir
            process.arguments = arguments
            process.standardInput = FileHandle.nullDevice
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            try process.run(); process.waitUntilExit()
            guard process.terminationStatus == 0 else { throw PairingFailure.generation }
        }
        // Explicit EC parameters produced by some LibreSSL versions are
        // considerably larger and not accepted by embedded named-curve parsers.
        try openssl(["ecparam","-name","prime256v1","-param_enc","named_curve","-genkey","-noout","-out","key.pem"])
        try openssl(["req","-x509","-new","-key","key.pem","-sha256","-subj","/CN=AirESP32ace-\(id)",
                     "-days","3650","-out","cert.pem"])
        let cert = try String(contentsOf: dir.appendingPathComponent("cert.pem"), encoding: .utf8)
        let key = try String(contentsOf: dir.appendingPathComponent("key.pem"), encoding: .utf8)
        let encoded = cert.split(separator: "\n").filter { !$0.hasPrefix("-----") }.joined()
        guard let der = Data(base64Encoded: encoded), SecCertificateCreateWithData(nil, der as CFData) != nil else {
            throw PairingFailure.generation
        }
        var random = [UInt8](repeating: 0, count: 32)
        guard SecRandomCopyBytes(kSecRandomDefault, random.count, &random) == errSecSuccess else { throw PairingFailure.generation }
        let token = random.map { String(format: "%02x", $0) }.joined()
        let payload = try JSONEncoder().encode(["cert": cert, "key": key, "token": token])
        return (Self(deviceId: id, certificate: der, token: token), String(decoding: payload, as: UTF8.self))
    }
}

enum PairingFailure: LocalizedError {
    case unpaired, identity, generation, storage, boot
    var errorDescription: String? {
        switch self {
        case .unpaired: "Connect your tracker by USB and choose Manage Device → Pair This Mac."
        case .identity: "This is not the board paired with this Mac. Select Pair This Mac to switch boards."
        case .generation: "Could not generate unique device credentials. The pairing was not changed."
        case .storage: "Could not retain the pairing on this Mac. Pair again after checking storage."
        case .boot: "Firmware was written, but this USB device did not confirm booting the selected image. Leave it connected and check the display."
        }
    }
}

final class PairedDeviceSession: NSObject, URLSessionDelegate, URLSessionTaskDelegate, @unchecked Sendable {
    private let pin: Data
    init(pin: Data) { self.pin = pin }

    func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
                    completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust,
              let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate],
              let leaf = chain.first, (SecCertificateCopyData(leaf) as Data) == pin else {
            completionHandler(.cancelAuthenticationChallenge, nil); return
        }
        // Physical USB provisioning is the trust anchor; IP addresses can change.
        completionHandler(.useCredential, URLCredential(trust: trust))
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }

    private static let lock = NSLock()
    nonisolated(unsafe) private static var previous: DevicePairing?
    nonisolated(unsafe) private static var session: URLSession?
    static func current() throws -> URLSession {
        try lock.withLock {
            guard let pair = DevicePairing.stored() else { throw PairingFailure.unpaired }
            if pair == previous, let session { return session }
            session?.invalidateAndCancel()
            let config = URLSessionConfiguration.ephemeral
            config.httpAdditionalHeaders = ["Authorization": "Bearer \(pair.token)"]
            config.urlCache = nil
            config.httpCookieStorage = nil
            config.urlCredentialStorage = nil
            config.httpMaximumConnectionsPerHost = 1
            let result = URLSession(configuration: config, delegate: PairedDeviceSession(pin: pair.certificate), delegateQueue: nil)
            previous = pair; session = result
            return result
        }
    }
}
