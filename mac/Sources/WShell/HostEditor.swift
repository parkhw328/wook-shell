import AppKit
import WShellCore

func editHost(_ existing: Record?, store: SettingsStore) throws -> Bool {
    let alert = Theme.alert(existing == nil ? "New host" : "Connection settings", "Public key authentication: register the .pub key on the server, then select the matching private key here.")
    alert.addButton(withTitle: "Save host"); alert.addButton(withTitle: "Cancel")
    let form = Canvas(frame: NSRect(x: 0, y: 0, width: 560, height: 484))
    var fields: [String: NSTextField] = [:]
    let rows = [("Name", "name", existing?.name ?? ""), ("Host", "HostName", existing?["HostName"] ?? ""),
                ("Port", "PortNumber", existing?["PortNumber"] ?? "22"), ("User", "UserName", existing?["UserName"] ?? ""),
                ("Authentication", "authentication", ""), ("Private key", "PublicKeyFile", existing?["PublicKeyFile"] ?? ""),
                ("Font size", "FontHeight", existing?["FontHeight"].isEmpty == false ? existing!["FontHeight"] : "13"),
                ("Keepalive (s)", "PingIntervalSecs", existing?["PingIntervalSecs"].isEmpty == false ? existing!["PingIntervalSecs"] : "30")]
    for (index, row) in rows.enumerated() {
        let label = Theme.label(row.0); label.frame = NSRect(x: 0, y: index * 39 + 6, width: 135, height: 25); form.addSubview(label)
        let field = Theme.input(); field.stringValue = row.2
        field.frame = NSRect(x: 143, y: index * 39, width: row.1 == "PublicKeyFile" ? 325 : 417, height: 30)
        fields[row.1] = field; form.addSubview(field)
    }
    fields["authentication"]?.removeFromSuperview(); fields["authentication"] = nil
    let authentication = NSPopUpButton(frame: NSRect(x: 140, y: 156, width: 420, height: 30), pullsDown: false)
    authentication.font = Theme.font(); authentication.addItems(withTitles: ["Password", "Public key authentication"])
    authentication.selectItem(at: existing?["PublicKeyFile"].isEmpty == false ? 1 : 0); form.addSubview(authentication)
    let browse = ActionButton("Choose") {
        let panel = NSOpenPanel(); panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { fields["PublicKeyFile"]?.stringValue = url.path }
    }
    browse.frame = NSRect(x: 478, y: 195, width: 82, height: 30); form.addSubview(browse)
    let saved = NSButton(checkboxWithTitle: "Save password in this Mac's Keychain", target: nil, action: nil)
    saved.font = Theme.font(); saved.frame = NSRect(x: 0, y: 324, width: 560, height: 26)
    saved.state = existing.map { CredentialStore().contains($0.credentialID) } == true ? .on : .off
    form.addSubview(saved)
    let password = NSSecureTextField(frame: NSRect(x: 0, y: 359, width: 560, height: 30))
    password.font = Theme.font(); password.placeholderString = "Leave blank to keep the saved password"; form.addSubview(password)
    let compression = NSButton(checkboxWithTitle: "Enable SSH compression", target: nil, action: nil)
    compression.font = Theme.font(); compression.state = existing?["Compression"] == "1" ? .on : .off
    compression.frame = NSRect(x: 0, y: 401, width: 560, height: 26); form.addSubview(compression)
    let note = Theme.label("Export / Import never transfers passwords or private-key files.", size: 11, color: Theme.muted)
    note.frame = NSRect(x: 0, y: 448, width: 560, height: 28); form.addSubview(note)
    alert.accessoryView = form
    guard alert.runModal() == .alertFirstButtonReturn else { return false }
    var record = existing ?? Record(name: "")
    record.name = fields["name"]!.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
    for (key, field) in fields where key != "name" { record[key] = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines) }
    record["Protocol"] = "ssh"; record["Compression"] = compression.state == .on ? "1" : "0"
    let publicKey = authentication.indexOfSelectedItem == 1
    if !publicKey { record["PublicKeyFile"] = "" }
    else {
        guard !record["PublicKeyFile"].isEmpty else { throw WShellError("Select the matching private key. Generate a key pair in Tools → SSH key manager.") }
        guard !record["PublicKeyFile"].lowercased().hasSuffix(".pub") else { throw WShellError("A public .pub key belongs on the server in ~/.ssh/authorized_keys. Select the matching OpenSSH private key here.") }
        guard !record["PublicKeyFile"].lowercased().hasSuffix(".ppk") else { throw WShellError("macOS uses OpenSSH-format private keys. Choose the original OpenSSH key or convert the PPK key before connecting.") }
    }
    guard let size = Double(record["FontHeight"]), (9...28).contains(size),
          let interval = Int(record["PingIntervalSecs"]), (0...3600).contains(interval) else { throw WShellError("Use font size 9–28 and keepalive 0–3600 seconds.") }
    try record.validate()
    let credentials = CredentialStore()
    if publicKey { saved.state = .off }
    var secret = Data(password.stringValue.utf8); password.stringValue = ""
    defer { secret.resetBytes(in: 0..<secret.count) }
    if saved.state == .on && secret.isEmpty && !credentials.contains(record.credentialID) {
        throw WShellError("Enter the password again for this host and user.")
    }
    try store.save(record, replacing: existing?.name)
    if saved.state == .on && !secret.isEmpty { try credentials.save(record.credentialID, password: secret) }
    if saved.state == .off { try credentials.remove(record.credentialID) }
    if let old = existing, old.credentialID != record.credentialID { try credentials.remove(old.credentialID) }
    return true
}

func runAskpass() -> Never {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory); app.appearance = NSAppearance(named: .darkAqua); Theme.loadFonts()
    let environment = ProcessInfo.processInfo.environment
    let confirmation = environment["SSH_ASKPASS_PROMPT"] == "confirm"
    do {
        let store = try SettingsStore()
        // The parent uses StrictHostKeyChecking=yes and pre-verifies unknown hosts
        // separately, so this password-only process can never receive a trust prompt.
        if !confirmation, environment["SSH_ASKPASS_PROMPT"] != "none",
           let name = environment["WSHELL_PASSWORD_HOST"], let nonce = environment["WSHELL_PASSWORD_ATTEMPT"], UUID(uuidString: nonce) != nil,
           let record = try store.read().first(where: { $0.category == "sessions" && $0.name == name }),
           record.credentialID == environment["WSHELL_PASSWORD_SCOPE"] {
            let marker = store.root.appendingPathComponent("attempt-" + nonce)
            let fd = open(marker.path, O_CREAT | O_EXCL | O_WRONLY, 0o600)
            if fd >= 0 {
                close(fd)
                if var password = try CredentialStore().read(record.credentialID) {
                    FileHandle.standardOutput.write(password); FileHandle.standardOutput.write(Data([10]))
                    password.resetBytes(in: 0..<password.count); exit(0)
                }
            }
        }
        app.activate(ignoringOtherApps: true)
        let prompt = CommandLine.arguments.dropFirst().joined(separator: " ")
        let alert = Theme.alert(confirmation ? "Verify SSH host key" : "SSH authentication", String(prompt.prefix(4096)))
        alert.addButton(withTitle: confirmation ? "Trust host" : "Continue"); alert.addButton(withTitle: "Cancel")
        let input = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 460, height: 30)); input.font = Theme.font()
        if !confirmation { alert.accessoryView = input }
        guard alert.runModal() == .alertFirstButtonReturn else { exit(1) }
        var password = Data((confirmation ? "yes" : input.stringValue).utf8); input.stringValue = ""
        FileHandle.standardOutput.write(password); FileHandle.standardOutput.write(Data([10]))
        password.resetBytes(in: 0..<password.count); exit(0)
    } catch { _ = Theme.alert("SSH authentication", error.localizedDescription).runModal(); exit(1) }
}
