import Foundation
import CryptoKit
import Darwin

public enum HostTrust {
    static func run(_ executable: String, _ args: [String]) throws -> (Int32, Data) {
        let process = Process(), pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: executable); process.arguments = args
        process.standardOutput = pipe; process.standardError = FileHandle.nullDevice
        try process.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile(); process.waitUntilExit()
        guard data.count <= 1024 * 1024 else { throw WShellError("The host-key response is too large.") }
        return (process.terminationStatus, data)
    }
    static func lookup(_ host: Record) -> String {
        host.port == 22 ? host["HostName"] : "[\(host["HostName"])]:\(host.port)"
    }
    public static func isKnown(_ host: Record, file: URL) throws -> Bool {
        guard FileManager.default.fileExists(atPath: file.path) else { return false }
        let (status, _) = try run("/usr/bin/ssh-keygen", ["-F", lookup(host), "-f", file.path])
        guard status == 0 || status == 1 else { throw WShellError("Cannot read the wShell host-key store.") }
        return status == 0
    }
    /// Keyscan does not authenticate the peer. A person must verify fingerprints
    /// before these public host keys become trusted; no password is involved.
    public static func ensure(_ host: Record, file: URL, confirm: ([String]) -> Bool) throws -> Bool {
        try host.validate()
        if try isKnown(host, file: file) { return true }
        let (status, data) = try run("/usr/bin/ssh-keyscan", ["-T", "5", "-p", String(host.port), "-t", "ed25519,ecdsa,rsa", "--", host["HostName"]])
        guard status == 0, let output = String(data: data, encoding: .utf8) else { throw WShellError("Cannot read the server's SSH host key. Check the address, port and network.") }
        let expected = lookup(host)
        var lines: [String] = [], fingerprints: [String] = []
        for line in output.split(separator: "\n") where !line.hasPrefix("#") {
            let parts = line.split(whereSeparator: { $0 == " " || $0 == "\t" })
            guard parts.count == 3, String(parts[0]) == expected,
                  ["ssh-ed25519", "ecdsa-sha2-nistp256", "ssh-rsa"].contains(String(parts[1])),
                  let blob = Data(base64Encoded: String(parts[2])), blob.count >= 16 else { throw WShellError("Invalid SSH host-key response.") }
            let fingerprint = Data(SHA256.hash(data: blob)).base64EncodedString().replacingOccurrences(of: "=", with: "")
            lines.append(String(line)); fingerprints.append(String(parts[1]) + "\nSHA256:" + fingerprint)
        }
        guard !lines.isEmpty else { throw WShellError("The server did not provide a supported host key.") }
        guard confirm(fingerprints) else { return false }
        let lock = open(file.appendingPathExtension("lock").path, O_CREAT | O_RDWR, 0o600)
        guard lock >= 0 else { throw WShellError("Cannot lock the host-key store.") }
        defer { flock(lock, LOCK_UN); close(lock) }
        guard flock(lock, LOCK_EX | LOCK_NB) == 0 else { throw WShellError("Another window is updating host keys. Try again.") }
        // A concurrent first connection may have added a key while the dialog was open.
        // Leave it unchanged; strict checking in ssh will reject a different key.
        if try isKnown(host, file: file) { return true }
        var existing = FileManager.default.fileExists(atPath: file.path) ? try Data(contentsOf: file) : Data()
        if !existing.isEmpty && existing.last != 10 { existing.append(10) }
        existing.append(contentsOf: (lines.joined(separator: "\n") + "\n").utf8)
        try existing.write(to: file, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
        return true
    }
}
