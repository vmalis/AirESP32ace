// AirESP32ace
// Created by VM, November 2026
import Foundation
import XCTest
@testable import AirspaceCompanion

final class SecureDeviceTransportTests: XCTestCase {
    /// Loopback fixture; no board, local saved credentials, or third-party
    /// provider is contacted. Exercises URLSession's actual TLS challenge.
    func testTLSAcceptsOnlyUSBPinnedLeafAndRequiresAuthorization() async throws {
        let (pair, json) = try DevicePairing.make(for: "020304050608")
        let wire = try JSONDecoder().decode([String:String].self, from: Data(json.utf8))
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: false, attributes: [.posixPermissions:0o700])
        defer { try? FileManager.default.removeItem(at: dir) }
        for name in ["cert","key","token"] {
            try XCTUnwrap(wire[name]).write(to:dir.appendingPathComponent(name),atomically:true,encoding:.utf8)
        }
        let server = Process()
        server.currentDirectoryURL = dir
        server.executableURL = URL(fileURLWithPath:"/usr/bin/python3")
        server.arguments = ["-I","-c", """
        import http.server,ssl,pathlib,os,sys
        os.chdir(sys.argv[1])
        token=pathlib.Path('token').read_text()
        class H(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200 if self.headers.get('Authorization')=='Bearer '+token else 401)
                self.end_headers()
            def log_message(self,*args): pass
        s=http.server.HTTPServer(('127.0.0.1',0),H)
        c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        c.load_cert_chain('cert','key')
        s.socket=c.wrap_socket(s.socket,server_side=True)
        pathlib.Path('port').write_text(str(s.server_port))
        s.serve_forever()
        """,dir.path]
        let errors = Pipe()
        server.standardError = errors
        try server.run()
        defer { if server.isRunning { server.terminate(); server.waitUntilExit() } }
        let port = dir.appendingPathComponent("port")
        for _ in 0..<400 where !FileManager.default.fileExists(atPath:port.path) && server.isRunning {
            try await Task.sleep(for:.milliseconds(50))
        }
        guard FileManager.default.fileExists(atPath:port.path) else {
            if server.isRunning { XCTFail("Loopback server startup timed out"); return }
            let diagnostic = String(decoding: errors.fileHandleForReading.readDataToEndOfFile(),as:UTF8.self)
            if diagnostic.contains("Operation not permitted") { throw XCTSkip("Sandbox does not allow the loopback listener") }
            XCTFail("TLS fixture did not start: \(diagnostic.suffix(300))"); return
        }
        let url = try XCTUnwrap(URL(string:"https://127.0.0.1:" + (try String(contentsOf:port,encoding:.utf8))))
        func send(_ pin: Data,_ bearer: String) async throws -> Int? {
            let session = URLSession(configuration:.ephemeral, delegate:PairedDeviceSession(pin:pin), delegateQueue:nil)
            defer { session.invalidateAndCancel() }
            var req = URLRequest(url:url); req.timeoutInterval=4
            req.setValue("Bearer \(bearer)",forHTTPHeaderField:"Authorization")
            let (_,r)=try await session.data(for:req)
            return (r as? HTTPURLResponse)?.statusCode
        }
        let good = try await send(pair.certificate,pair.token)
        let unpaired = try await send(pair.certificate,"synthetic-invalid")
        XCTAssertEqual(good,200); XCTAssertEqual(unpaired,401)
        do { _ = try await send(Data([0]),pair.token); XCTFail("Unexpected certificate accepted") }
        catch { /* a changed certificate must fail before sending the request */ }
    }
    func testPhysicalPairCreatesUniquePinsAndSecretsWithoutPersistingFixture() throws {
        let (first, json) = try DevicePairing.make(for: "020304050607")
        let (second, _) = try DevicePairing.make(for: "020304050607")
        let wire = try JSONDecoder().decode([String:String].self, from: Data(json.utf8))
        XCTAssertEqual(wire["token"]?.count, 64)
        XCTAssertLessThan(wire["cert"]?.utf8.count ?? 9999, 1024)
        XCTAssertLessThan(wire["key"]?.utf8.count ?? 9999, 512)
        XCTAssertFalse(first.certificate == second.certificate)
        XCTAssertFalse(first.token == second.token)
        XCTAssertNil((try JSONSerialization.jsonObject(with: JSONEncoder().encode(first)) as? [String:Any])?["key"])
        XCTAssertThrowsError(try DevicePairing.make(for: "unexpected name"))
    }
    func testOnlyHTTPSAndBoundAPIRoutesAreAccepted() throws {
        let client = DeviceClient()
        for host in ["board.local","http://board.local","https://board.local"] {
            XCTAssertEqual(try client.url(host: host,path:"/api/status?mapInventory=1").absoluteString,
                           "https://board.local/api/status?mapInventory=1")
        }
        for host in ["file:///tmp","https://user@board.local","board.local/elsewhere","board.local?redirect=true"] {
            XCTAssertThrowsError(try client.url(host: host,path:"/api/status"))
        }
        XCTAssertThrowsError(try client.url(host:"board.local",path:"//other.local"))
    }
    func testBootVerificationUsesBuildFingerprintNotVersionString() throws {
        var image = Data(repeating: 0, count: 208)
        XCTAssertThrowsError(try USBConfigurationClient.imageELFDigest(image))
        image[0] = 0xe9; image.replaceSubrange(32..<36, with: [0x32,0x54,0xcd,0xab])
        image.replaceSubrange(176..<208, with: [UInt8](repeating: 0x5a,count:32))
        XCTAssertEqual(try USBConfigurationClient.imageELFDigest(image), String(repeating:"5a",count:32))
        XCTAssertThrowsError(try USBConfigurationClient.imageELFDigest(Data(image.prefix(200))))
    }
}
