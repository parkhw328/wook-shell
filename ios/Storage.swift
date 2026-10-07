import Foundation
import Combine
import Security

struct Host: Codable, Identifiable, Equatable {
    var id = UUID()
    var name = "", address = "", user = "", port = 22
    var publicKey = false
    var endpoint: String { "[\(address.lowercased())]:\(port)" }
    var credential: String { "\(id.uuidString)\n\(endpoint)\n\(user)" }
    func validate() throws {
        guard !name.trimmingCharacters(in: .whitespaces).isEmpty, !address.isEmpty, !user.isEmpty,
              (1...65535).contains(port), name.utf8.count < 256, address.utf8.count < 256, user.utf8.count < 256,
              ![name, address, user].contains(where: { $0.unicodeScalars.contains { CharacterSet.controlCharacters.contains($0) } }),
              !address.contains(where: { $0.isWhitespace }) else { throw AppError("Enter a name, valid host, user and port (1–65535).") }
    }
}
struct AppError: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}
enum Vault {
    static func query(_ account: String) -> [String: Any] {
        [kSecClass as String:kSecClassGenericPassword, kSecAttrService as String:"com.wshell.ipad.secrets",
         kSecAttrAccount as String:account, kSecAttrSynchronizable as String:false]
    }
    static func get(_ account: String) throws -> Data? {
        var q = query(account); q[kSecReturnData as String] = true
        var item: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &item)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else { throw AppError("Cannot read Keychain (\(status)). Unlock your iPad and retry.") }
        return item as? Data
    }
    static func put(_ account: String, _ data: Data) throws {
        let values: [String:Any] = [kSecValueData as String:data, kSecAttrAccessible as String:kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        var status = SecItemUpdate(query(account) as CFDictionary, values as CFDictionary)
        if status == errSecItemNotFound {
            var q = query(account); values.forEach { q[$0.key] = $0.value }
            status = SecItemAdd(q as CFDictionary, nil)
        }
        guard status == errSecSuccess else { throw AppError("Cannot save to Keychain (\(status)).") }
    }
    static func remove(_ account: String) throws {
        let status = SecItemDelete(query(account) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw AppError("Cannot remove Keychain item (\(status)).") }
    }
}
final class HostStore: ObservableObject {
    @Published var hosts: [Host] = []
    private var trusted: [String:String] = [:]
    private let directory: URL
    static let backupNotice = "Passwords, private keys and trusted host keys are NOT included. Enter passwords and import private keys again after restoring."
    struct Saved: Codable { var hosts: [Host]; var trusted: [String:String] }
    struct Backup: Codable { var format = "wShell-iPad-1"; var hosts: [Host] }
    init() {
        directory = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("wShell", isDirectory: true)
    }
    func load() throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let url = directory.appendingPathComponent("settings.json")
        guard FileManager.default.fileExists(atPath: url.path) else { return }
        let saved = try JSONDecoder().decode(Saved.self, from: Data(contentsOf: url))
        for host in saved.hosts { try host.validate() }
        hosts = saved.hosts; trusted = saved.trusted
    }
    private func save(_ next: [Host], _ trust: [String:String]) throws {
        let data = try JSONEncoder().encode(Saved(hosts: next, trusted: trust))
        try data.write(to: directory.appendingPathComponent("settings.json"), options: [.atomic, .completeFileProtection])
        hosts = next; trusted = trust
    }
    func update(_ host: Host) throws {
        try host.validate()
        var next = hosts; next.removeAll { $0.id == host.id }; next.append(host)
        try save(next.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }, trusted)
    }
    func delete(_ host: Host) throws {
        try Vault.remove(host.credential + "\npassword"); try Vault.remove(host.credential + "\nkey")
        try save(hosts.filter { $0.id != host.id }, trusted)
    }
    func fingerprint(_ host: Host) -> String? { trusted[host.endpoint] }
    func trust(_ host: Host, _ fingerprint: String) throws {
        var next = trusted; next[host.endpoint] = fingerprint; try save(hosts, next)
    }
    func forgetTrust(_ host: Host) throws {
        var next = trusted; next.removeValue(forKey: host.endpoint); try save(hosts, next)
    }
    func export() throws -> Data { try JSONEncoder().encode(Backup(hosts: hosts)) }
    func restore(_ data: Data) throws {
        guard data.count <= 2_000_000 else { throw AppError("Backup is too large.") }
        let backup = try JSONDecoder().decode(Backup.self, from: data)
        guard backup.format == "wShell-iPad-1", backup.hosts.count <= 1000 else { throw AppError("Use a wShell iPad settings backup.") }
        var next = hosts
        for var host in backup.hosts {
            try host.validate()
            if next.contains(where: { $0.name == host.name && $0.endpoint == host.endpoint && $0.user == host.user }) { continue }
            host.id = UUID(); next.append(host)
        }
        try save(next, trusted)
    }
}
