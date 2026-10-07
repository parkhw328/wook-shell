import Foundation
import Darwin

public struct WShellError: LocalizedError {
    public let message: String
    public init(_ message: String) { self.message = message }
    public var errorDescription: String? { message }
}

public struct Record: Codable, Equatable {
    public var category: String
    public var name: String
    public var fields: [String: String]
    public init(category: String = "sessions", name: String, fields: [String: String] = [:]) {
        self.category = category; self.name = name; self.fields = fields
    }
    public subscript(_ key: String) -> String {
        get { fields[key] ?? "" }
        set { fields[key] = newValue }
    }
    public var port: Int { Int(self["PortNumber"]) ?? 22 }
    public var fontSize: Double { min(28, max(9, Double(self["FontHeight"]) ?? 13)) }
    public var credentialID: String { [name, self["HostName"], String(port), self["UserName"]].joined(separator: "\n") }
    public func validate() throws {
        guard !name.isEmpty, name.utf8.count <= 100, !name.hasPrefix("__wook_"),
              !name.contains("\0"), !name.contains("\n") else { throw WShellError("Enter a host name of 1–100 UTF-8 bytes.") }
        let host = self["HostName"], user = self["UserName"]
        let invalid = CharacterSet.whitespacesAndNewlines.union(.controlCharacters)
        guard !host.isEmpty, !host.hasPrefix("-"), host.rangeOfCharacter(from: invalid) == nil,
              !user.hasPrefix("-"), user.rangeOfCharacter(from: invalid) == nil,
              (1...65535).contains(port), Int(self["PortNumber"]) != nil,
              !self["PublicKeyFile"].contains("\0") else { throw WShellError("Check the host, SSH port (1–65535), user and private-key path.") }
        guard self["Protocol"].isEmpty || self["Protocol"] == "ssh" else {
            throw WShellError("This macOS release supports SSH. Windows also supports Serial, Telnet, Rlogin and Raw TCP.")
        }
    }
    public static func quick(_ address: String) throws -> Record {
        let text = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let url = URLComponents(string: text.hasPrefix("ssh://") ? text : "ssh://" + text),
              url.scheme == "ssh", url.password == nil, url.query == nil, url.fragment == nil,
              url.path.isEmpty, let host = url.host else { throw WShellError("Use user@hostname:22 or ssh://user@[::1]:22.") }
        var result = Record(name: text)
        result["HostName"] = host.trimmingCharacters(in: CharacterSet(charactersIn: "[]"))
        result["UserName"] = url.user ?? ""; result["PortNumber"] = String(url.port ?? 22); result["Protocol"] = "ssh"
        try result.validate(); return result
    }
}

public enum Backup {
    public static let notice = "Passwords are not exported or imported, even in encrypted form. Enter them again for imported hosts. Private-key files are not included."
    static let secrets: Set<String> = ["ProxyPassword", "WookSshPasswordDPAPI", "WookSshPasswordScope", "WShellPassword", "WShellKeychainID"]
    public static func clean(_ record: Record) -> Record {
        var result = record
        result.fields = result.fields.filter { !secrets.contains($0.key) }
        return result
    }
    static func allowed(_ record: Record) -> Bool {
        ["sessions", "trust", "cas"].contains(record.category) && !record.name.isEmpty &&
            record.name.utf8.count <= 100 && !record.name.hasPrefix("__wook_") &&
            (record.category != "trust" || record.name == "hostkeys")
    }
    public static func encode(_ records: [Record]) throws -> Data {
        guard records.count <= 10000 else { throw WShellError("Too many records.") }
        var data = Data("WSB1".utf8)
        func number(_ value: Int) { var n = UInt32(value).littleEndian; withUnsafeBytes(of: &n) { data.append(contentsOf: $0) } }
        func field(_ value: String) throws {
            guard !value.contains("\0"), value.utf8.count <= 4 * 1024 * 1024 else { throw WShellError("Invalid settings field.") }
            number(value.utf8.count); data.append(contentsOf: value.utf8)
        }
        number(records.count)
        for original in records {
            let record = clean(original)
            guard allowed(record), record.fields.count <= 10000 else { throw WShellError("Invalid settings record.") }
            try field(record.category); try field(record.name); number(record.fields.count)
            for key in record.fields.keys.sorted() { try field(key); try field(record[key]) }
        }
        guard data.count <= 32 * 1024 * 1024 else { throw WShellError("Backup exceeds 32 MiB.") }
        return data
    }
    public static func decode(_ data: Data) throws -> [Record] {
        guard data.count >= 8, data.count <= 32 * 1024 * 1024, data.prefix(4) == Data("WSB1".utf8) else { throw WShellError("Select a wShell .wshell backup (up to 32 MiB).") }
        var offset = 4
        func number() throws -> Int {
            guard offset + 4 <= data.count else { throw WShellError("Truncated backup.") }
            let n = (0..<4).reduce(0) { $0 | Int(data[offset + $1]) << ($1 * 8) }; offset += 4; return n
        }
        func field(_ limit: Int) throws -> String {
            let count = try number()
            guard count <= limit, count <= data.count - offset,
                  let value = String(data: data[offset..<offset + count], encoding: .utf8), !value.contains("\0") else { throw WShellError("Invalid backup field.") }
            offset += count; return value
        }
        let count = try number()
        guard count <= 10000 else { throw WShellError("Too many records.") }
        var records: [Record] = [], names = Set<String>()
        for _ in 0..<count {
            var record = try Record(category: field(20), name: field(100))
            guard allowed(record), names.insert(record.category + "\0" + record.name).inserted else { throw WShellError("Invalid or repeated record.") }
            let pairs = try number(); guard pairs <= 10000 else { throw WShellError("Too many fields.") }
            var size = 4
            for _ in 0..<pairs {
                let key = try field(65536), value = try field(4 * 1024 * 1024)
                size += 2 * key.utf8.count + 2 * value.utf8.count + 2
                guard !key.isEmpty, record.fields[key] == nil, size <= 16 * 1024 * 1024 else { throw WShellError("Repeated or oversized field.") }
                record[key] = value
            }
            records.append(clean(record))
        }
        guard offset == data.count else { throw WShellError("Unexpected data after backup.") }
        return records
    }
}

public final class SettingsStore {
    public let root: URL
    public var knownHosts: URL { root.appendingPathComponent("known_hosts") }
    public init(root: URL? = nil) throws {
        self.root = root ?? ProcessInfo.processInfo.environment["WOOK_DATA_DIR"].map { URL(fileURLWithPath: $0, isDirectory: true) }
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/wShell", isDirectory: true)
        try FileManager.default.createDirectory(at: self.root, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
    }
    public func read() throws -> [Record] {
        let file = root.appendingPathComponent("settings.json")
        guard FileManager.default.fileExists(atPath: file.path) else { return [] }
        let data = try Data(contentsOf: file)
        guard data.count <= 32 * 1024 * 1024 else { throw WShellError("Settings exceed 32 MiB.") }
        return try JSONDecoder().decode([Record].self, from: data)
    }
    func change(_ update: (inout [Record]) throws -> Void) throws {
        let fd = open(root.appendingPathComponent("settings.lock").path, O_CREAT | O_RDWR, 0o600)
        guard fd >= 0 else { throw WShellError("Cannot lock settings.") }
        defer { flock(fd, LOCK_UN); close(fd) }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { throw WShellError("Another wShell window is saving settings. Try again.") }
        var records = try read(); try update(&records)
        let encoded = try JSONEncoder().encode(records)
        guard encoded.count <= 32 * 1024 * 1024 else { throw WShellError("Settings exceed 32 MiB.") }
        let file = root.appendingPathComponent("settings.json")
        try encoded.write(to: file, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }
    public func save(_ record: Record, replacing oldName: String? = nil) throws {
        try record.validate()
        try change { records in
            guard !records.contains(where: { $0.category == "sessions" && $0.name == record.name && $0.name != oldName }) else { throw WShellError("A host with this name already exists.") }
            records.removeAll { $0.category == "sessions" && $0.name == oldName }
            records.append(Backup.clean(record))
        }
    }
    public func remove(_ record: Record) throws {
        try change { $0.removeAll { $0.category == record.category && $0.name == record.name } }
    }
    public func exportBackup() throws -> Data {
        var records = try read()
        // OpenSSH trust is preserved in a separate field; PuTTY trust entries are kept unchanged.
        if FileManager.default.fileExists(atPath: knownHosts.path) {
            let text = try String(contentsOf: knownHosts, encoding: .utf8)
            if let index = records.firstIndex(where: { $0.category == "trust" }) { records[index]["WShellOpenSSHKnownHosts"] = text }
            else { records.append(Record(category: "trust", name: "hostkeys", fields: ["WShellOpenSSHKnownHosts": text])) }
        }
        return try Backup.encode(records)
    }
    public func importBackup(_ data: Data) throws -> Int {
        let incoming = try Backup.decode(data)
        var count = 0
        try change { records in
            for record in incoming {
                if let index = records.firstIndex(where: { $0.category == record.category && $0.name == record.name }) {
                    if record.category == "trust" {
                        for (key, value) in record.fields where records[index].fields[key] == nil { records[index][key] = value }
                    }
                } else { records.append(record); count += 1 }
            }
        }
        // Never merge or replace existing trust silently; an absent store can be restored.
        if !FileManager.default.fileExists(atPath: knownHosts.path),
           let text = incoming.first(where: { $0.category == "trust" })?.fields["WShellOpenSSHKnownHosts"] {
            try Data(text.utf8).write(to: knownHosts, options: .withoutOverwriting)
            try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: knownHosts.path)
        }
        return count
    }
}

public enum SSHCommand {
    public static func arguments(for host: Record, knownHosts: URL, savedPassword: Bool = false, sftp: Bool = false) throws -> [String] {
        try host.validate()
        // Imported engine fields and the user's ~/.ssh/config are intentionally not executed.
        var result = [sftp ? "-T" : "-tt", "-F", "/dev/null", "-p", String(host.port),
            "-o", "UserKnownHostsFile=\"\(knownHosts.path.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\""))\"",
            "-o", "GlobalKnownHostsFile=/dev/null", "-o", "StrictHostKeyChecking=yes", "-o", "UpdateHostKeys=no",
            "-o", "IdentityAgent=none", "-o", "ForwardAgent=no", "-o", "ControlMaster=no", "-o", "ControlPath=none",
            "-o", "IdentitiesOnly=yes", "-o", "ConnectTimeout=15", "-o", "ServerAliveCountMax=3",
            "-o", "ServerAliveInterval=\(min(3600, max(0, Int(host["PingIntervalSecs"]) ?? 30)))",
            "-o", "SendEnv=COLORTERM"]
        if !host["UserName"].isEmpty { result += ["-l", host["UserName"]] }
        if savedPassword {
            result += ["-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no", "-o", "KbdInteractiveAuthentication=no", "-o", "IdentityFile=none"]
        } else if !host["PublicKeyFile"].isEmpty { result += ["-i", host["PublicKeyFile"]] }
        else { result += ["-o", "IdentityFile=none"] }
        if host["Compression"] == "1" { result += ["-C"] }
        if sftp { result += ["-s", "-o", "ClearAllForwardings=yes"] }
        return result + ["--", host["HostName"]] + (sftp ? ["sftp"] : [])
    }
}
