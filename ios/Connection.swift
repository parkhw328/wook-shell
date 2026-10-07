import Foundation
import Combine

final class Connection: ObservableObject, Identifiable {
    let id = UUID(), host: Host, isSFTP: Bool
    @Published var status = "Connecting"
    @Published var ready = false
    let queue = DispatchQueue(label: "wShell.SSH.\(UUID())", qos: .userInitiated)
    private let lock = NSLock()
    private var engine: OpaquePointer?
    private var cancelled = false
    private var pending = Data()
    private var dimensions: (Int32, Int32)?
    private(set) var ftp: OpaquePointer?
    var output: ((Data) -> Void)?
    var terminal: TerminalPane?
    init(host: Host, sftp: Bool) { self.host = host; isSFTP = sftp }
    func start(password: String, key: Data?, passphrase: String,
               trust: @escaping (String, @escaping (Bool) -> Void) -> Void) {
        let credentials = AuthenticationInput(password: password, key: key, passphrase: passphrase)
        queue.async { [self] in
            defer { credentials.clear() }
            guard let s = wssh_new() else { finish("Cannot create SSH session"); return }
            lock.lock(); engine = s; let stop = cancelled; lock.unlock()
            if stop { cleanup(); return }
            do {
                try check(wssh_connect(s, host.address, Int32(host.port)), s)
                guard let bytes = wssh_fingerprint(s) else { throw AppError("Server did not provide a SHA-256 host fingerprint.") }
                let fingerprint = "SHA256:" + Data(bytes: bytes, count: 32).base64EncodedString().replacingOccurrences(of: "=", with: "")
                let decision = TrustDecision()
                DispatchQueue.main.async { trust(fingerprint) { decision.resolve($0) } }
                let deadline = Date().addingTimeInterval(120)
                while decision.wait() == nil && !stopped && Date() < deadline { Thread.sleep(forTimeInterval: 0.05) }
                guard decision.wait() == true, !stopped else { throw AppError("Host verification cancelled") }
                try check(credentials.authenticate(s, user: host.user), s)
                credentials.clear()
                if isSFTP {
                    guard let client = wssh_sftp(s) else { throw AppError(String(cString: wssh_error(s))) }
                    ftp = client
                    guard wsftp_init(client) != 0 else { throw AppError(String(cString: wsftp_error(client))) }
                    DispatchQueue.main.async { self.status = "Connected · SFTP"; self.ready = true }
                } else {
                    try check(wssh_shell(s, 80, 24), s)
                    DispatchQueue.main.async { self.status = "Connected · SSH"; self.ready = true }
                    var buffer = [UInt8](repeating: 0, count: 32768)
                    var outgoing = Data()
                    while !stopped {
                        lock.lock()
                        outgoing.append(pending); pending.removeAll(keepingCapacity: true)
                        let size = dimensions
                        lock.unlock()
                        if let size, wssh_resize(s, size.0, size.1) == 0 {
                            lock.lock(); if dimensions?.0 == size.0 && dimensions?.1 == size.1 { dimensions = nil }; lock.unlock()
                        }
                        if !outgoing.isEmpty {
                            let n = outgoing.withUnsafeBytes { wssh_write(s, $0.baseAddress, Int32(min(outgoing.count, 32768))) }
                            if n < 0 { throw AppError(String(cString: wssh_error(s))) }
                            if n > 0 { outgoing.removeFirst(Int(n)) }
                        }
                        let n = wssh_read(s, &buffer, Int32(buffer.count))
                        if n < 0 { break }
                        if n > 0 {
                            let data = Data(buffer.prefix(Int(n)))
                            // Backpressure: keep terminal output bounded when the UI is busy.
                            DispatchQueue.main.sync { self.output?(data) }
                        } else { Thread.sleep(forTimeInterval: 0.008) }
                    }
                    cleanup(); finish("Disconnected")
                }
            } catch { cleanup(); finish(error.localizedDescription) }
        }
    }
    private func check(_ code: Int32, _ s: OpaquePointer) throws {
        if code < 0 { throw AppError(String(cString: wssh_error(s))) }
    }
    var stopped: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func send(_ data: Data) {
        lock.lock(); defer { lock.unlock() }
        if pending.count + data.count <= 1_048_576 { pending.append(data) }
    }
    func resize(_ columns: Int, _ rows: Int) {
        lock.lock(); dimensions = (Int32(max(1, min(columns, 4096))), Int32(max(1, min(rows, 4096)))); lock.unlock()
    }
    func stop() {
        lock.lock(); cancelled = true; if let engine { wssh_cancel(engine) }; lock.unlock()
        queue.async { self.cleanup(); self.finish("Disconnected") }
    }
    private func cleanup() {
        if let ftp { wsftp_free(ftp); self.ftp = nil }
        lock.lock(); let old = engine; engine = nil; lock.unlock()
        if let old { wssh_free(old) }
    }
    private func finish(_ text: String) { DispatchQueue.main.async { self.ready = false; self.status = text } }
}
private final class TrustDecision {
    private let lock = NSLock()
    private var value: Bool?
    func resolve(_ v: Bool) { lock.lock(); value = v; lock.unlock() }
    func wait() -> Bool? { lock.lock(); defer { lock.unlock() }; return value }
}
private final class AuthenticationInput {
    private var password: String, key: Data?, passphrase: String
    init(password: String, key: Data?, passphrase: String) {
        self.password = password; self.key = key; self.passphrase = passphrase
    }
    func authenticate(_ session: OpaquePointer, user: String) -> Int32 {
        if let key {
            return key.withUnsafeBytes { bytes in
                wssh_auth(session, user, "", bytes.baseAddress?.assumingMemoryBound(to: CChar.self), key.count, passphrase)
            }
        }
        return wssh_auth(session, user, password, nil, 0, "")
    }
    // Release prompt credentials as soon as authentication finishes, not after the terminal closes.
    func clear() { password = ""; key = nil; passphrase = "" }
}
