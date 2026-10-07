import SwiftUI

enum Smoke {
    static func runIfRequested(_ store: HostStore) {
        #if targetEnvironment(simulator)
        guard ProcessInfo.processInfo.arguments.contains("--smoke-test") else { return }
        Task { @MainActor in
            let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            var checks: [String] = [], connections: [Connection] = []
            func save(_ passed: Bool, _ message: String = "") {
                let result: [String:Any] = ["passed":passed, "checks":checks, "error":message]
                try? JSONSerialization.data(withJSONObject: result, options: .prettyPrinted).write(to: documents.appendingPathComponent("result.json"), options: .atomic)
            }
            do {
                let config = try JSONDecoder().decode(Config.self, from: Data(contentsOf: documents.appendingPathComponent("smoke.json")))
                func require(_ condition: Bool, _ message: String) throws { if !condition { throw AppError(message) }; checks.append(message) }
                var host = Host(name: "Loopback", address: "127.0.0.1", user: "password", port: config.port)
                try store.update(host); try Vault.put(host.credential + "\npassword", Data(config.password.utf8))
                try require(try Vault.get(host.credential + "\npassword") == Data(config.password.utf8), "Device Keychain round trip")
                let exported = try store.export()
                try require(!String(decoding: exported, as: UTF8.self).contains(config.password), "Backup excludes password")
                try store.restore(exported)
                try require(store.hosts.count == 1, "Import does not duplicate existing host")
                let rejected = Connection(host: host, sftp: false); connections.append(rejected)
                rejected.start(password: config.password, key: nil, passphrase: "") { _, reply in reply(false) }
                try await wait { rejected.status != "Connecting" }
                try require(!rejected.ready, "Host rejection before authentication")
                for (index, key) in [Optional<Data>.none, Data(config.rsa.utf8), Data(config.ed25519.utf8)].enumerated() {
                    host.user = index == 0 ? "password" : index == 1 ? "rsa" : "ed25519"
                    let connection = Connection(host: host, sftp: false); connections.append(connection)
                    let terminal = TerminalPane(connection); connection.terminal = terminal
                    var received = Data()
                    connection.output = { data in received.append(data); terminal.feed(byteArray: Array(data)[...]) }
                    connection.start(password: config.password, key: key, passphrase: index == 1 ? config.passphrase : "") { fingerprint, reply in reply(fingerprint == config.fingerprint) }
                    try await wait { connection.ready || connection.status != "Connecting" }
                    try require(connection.ready, "SSH authentication \(host.user): \(connection.status)")
                    try await wait { String(decoding: received, as: UTF8.self).contains("WSHELL_SSH_READY") }
                    connection.resize(101, 31)
                    terminal.setMarkedText("취소", selectedRange: NSRange(location: 2, length: 0))
                    terminal.setMarkedText("", selectedRange: NSRange(location: 0, length: 0)); terminal.unmarkText()
                    terminal.insertText("|ime:한글🙂|\n")
                    try await wait { String(decoding: received, as: UTF8.self).contains("|ime:한글🙂|") }
                    try require(terminal.clipboardRead(source: terminal) == nil, "Remote clipboard denied \(index)")
                    if index == 2, let window = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first?.windows.first {
                        let controller = UIViewController(); controller.view = terminal
                        window.rootViewController = controller
                        try await Task.sleep(for: .milliseconds(500))
                        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
                        let image = renderer.image { _ in window.drawHierarchy(in: window.bounds, afterScreenUpdates: true) }
                        try image.pngData()?.write(to: documents.appendingPathComponent("terminal.png"))
                    }
                    connection.stop()
                }
                host.user = "rsa"
                let files = Connection(host: host, sftp: true); connections.append(files)
                files.start(password: "", key: Data(config.rsa.utf8), passphrase: config.passphrase) { fingerprint, reply in reply(fingerprint == config.fingerprint) }
                try await wait { files.ready || files.status != "Connecting" }
                try require(files.ready, "Embedded SSH SFTP subsystem: \(files.status)")
                let payload = Data((0..<1_200_003).map { UInt8($0 % 251) })
                let source = documents.appendingPathComponent("upload.bin"), downloaded = documents.appendingPathComponent("download.bin")
                try payload.write(to: source)
                try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                    files.queue.async {
                        do {
                            let client = FileClient(files.ftp!, files)
                            let directory = try client.canonical(".")
                            _ = try client.list(directory)
                            try client.upload(source, remoteJoin(directory, "upload-한글.bin"), replace: false) { _, _ in }
                            try client.download(remoteJoin(directory, "upload-한글.bin"), downloaded, replace: false) { _, _ in }
                            do {
                                try client.upload(source, remoteJoin(directory, "upload-한글.bin"), replace: true) { _, _ in }
                                throw AppError("Server without atomic rename accepted overwrite")
                            } catch let error as AppError {
                                guard error.message.contains("atomically") else { throw error }
                            }
                            try client.mkdir("/new-folder"); try client.rename("/new-folder", "/renamed-folder"); try client.remove("/renamed-folder", directory: true)
                            continuation.resume()
                        } catch { continuation.resume(throwing: error) }
                    }
                }
                try require(try Data(contentsOf: downloaded) == payload, "SFTP 1.2 MB upload/download exact bytes and short reads")
                checks.append("SFTP mkdir/rename/remove and refusal of unsafe overwrite")
                try require(!portableName("../escape") && !portableName("CON") && portableName("한글.txt"), "Safe download names")
                let view = UIHostingController(rootView: FilesPane(files).preferredColorScheme(.dark).tint(Palette.orange).font(Palette.font()))
                UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first?.windows.first?.rootViewController = view
                try await Task.sleep(for: .seconds(1))
                try Vault.remove(host.credential + "\npassword")
                save(true)
            } catch { save(false, error.localizedDescription) }
            connections.forEach { $0.stop() }
        }
        #endif
    }
    #if targetEnvironment(simulator)
    private struct Config: Decodable { let port: Int; let password, rsa, ed25519, passphrase, fingerprint: String }
    @MainActor private static func wait(_ condition: () -> Bool) async throws {
        let deadline = Date().addingTimeInterval(30)
        while !condition() { if Date() > deadline { throw AppError("Timed out waiting for simulator SSH") }; try await Task.sleep(for: .milliseconds(50)) }
    }
    #endif
}
