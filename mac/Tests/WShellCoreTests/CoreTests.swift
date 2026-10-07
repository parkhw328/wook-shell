import XCTest
@testable import WShellCore

final class CoreTests: XCTestCase {
    func host(_ name: String = "Fixture") -> Record {
        Record(name: name, fields: ["HostName":"localhost", "PortNumber":"22", "UserName":"tester", "Protocol":"ssh"])
    }
    func testQuickConnectAndInputValidation() throws {
        let record = try Record.quick("ssh://developer@[::1]:2222")
        XCTAssertEqual(record["HostName"], "::1"); XCTAssertEqual(record.port, 2222)
        for input in ["ssh://user:password@host", "-oProxyCommand=bad", "ssh://host/path", "host:99999", "host\nother"] {
            XCTAssertThrowsError(try Record.quick(input), input)
        }
    }
    func testSSHDoesNotReadExternalConfigurationOrAgents() throws {
        var record = host(); record["ProxyCommand"] = "untrusted"; record["RemoteCommand"] = "untrusted"
        record["PublicKeyFile"] = "/tmp/private key"
        let args = try SSHCommand.arguments(for: record, knownHosts: URL(fileURLWithPath: "/tmp/Application Support/wShell/known_hosts"))
        XCTAssertTrue(args.contains("/dev/null")); XCTAssertTrue(args.contains("IdentityAgent=none"))
        XCTAssertTrue(args.contains("ControlPath=none")); XCTAssertTrue(args.contains("/tmp/private key"))
        XCTAssertFalse(args.contains("untrusted")); XCTAssertEqual(args.suffix(2), ["--","localhost"])
        let password = try SSHCommand.arguments(for: host(), knownHosts: URL(fileURLWithPath: "/tmp/hosts"), savedPassword: true)
        XCTAssertTrue(password.contains("KbdInteractiveAuthentication=no")); XCTAssertTrue(password.contains("PubkeyAuthentication=no"))
    }
    func testBackupInteroperabilityAndSecretExclusion() throws {
        var record = host("서울")
        record["WookSshPasswordDPAPI"] = "ciphertext"; record["WookSshPasswordScope"] = "binding"; record["ProxyPassword"] = "secret"
        let data = try Backup.encode([record]); let restored = try Backup.decode(data)
        XCTAssertEqual(String(decoding: data.prefix(4), as: UTF8.self), "WSB1")
        XCTAssertEqual(restored[0].name, "서울"); XCTAssertEqual(restored[0]["HostName"], "localhost")
        XCTAssertEqual(restored[0]["WookSshPasswordDPAPI"], ""); XCTAssertEqual(restored[0]["ProxyPassword"], "")
        XCTAssertFalse(String(decoding: data, as: UTF8.self).contains("ciphertext"))
    }
    func testBackupRejectsTruncationAndDuplicateRecords() throws {
        let data = try Backup.encode([host()])
        for length in 0..<data.count { XCTAssertThrowsError(try Backup.decode(data.prefix(length))) }
        XCTAssertThrowsError(try Backup.decode(data + Data([0])))
        XCTAssertThrowsError(try Backup.decode(Backup.encode([host(),host()])))
    }
    func testAtomicStoreImportKeepsExistingNamesAndTrust() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let store = try SettingsStore(root: directory)
        try store.save(host()); var changed = host(); changed["HostName"] = "elsewhere"
        let trust = Record(category: "trust", name: "hostkeys", fields: ["WShellOpenSSHKnownHosts":"fixture-key"])
        XCTAssertEqual(try store.importBackup(Backup.encode([changed,host("Second"),trust])), 2)
        XCTAssertEqual(try store.read().first { $0.name == "Fixture" }?["HostName"], "localhost")
        XCTAssertEqual(try String(contentsOf: store.knownHosts, encoding: .utf8), "fixture-key")
        var otherTrust = trust; otherTrust["WShellOpenSSHKnownHosts"] = "replacement"
        _ = try store.importBackup(Backup.encode([otherTrust]))
        XCTAssertEqual(try String(contentsOf: store.knownHosts, encoding: .utf8), "fixture-key")
        XCTAssertThrowsError(try store.save(host()))
        try store.save(changed, replacing: "Fixture"); XCTAssertEqual(try store.read().first { $0.name == "Fixture" }?["HostName"], "elsewhere")
        try store.remove(changed); XCTAssertEqual(try store.read().filter { $0.category == "sessions" }.count, 1)
    }
    func testKeychainRoundTripAndEndpointBinding() throws {
        let store = CredentialStore(service: "com.wshell.test." + UUID().uuidString)
        let record = host(); var changed = record; changed["HostName"] = "other-host"
        defer { try? store.remove(record.credentialID) }
        try store.save(record.credentialID, password: Data("fixture-value".utf8))
        XCTAssertTrue(store.contains(record.credentialID)); XCTAssertEqual(try store.read(record.credentialID), Data("fixture-value".utf8))
        XCTAssertNil(try store.read(changed.credentialID))
        try store.remove(record.credentialID); XCTAssertNil(try store.read(record.credentialID))
    }
}
