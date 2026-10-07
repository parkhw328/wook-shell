import SwiftUI
import UniformTypeIdentifiers

@main struct WShellApp: App {
    @StateObject private var store = HostStore()
    var body: some Scene { WindowGroup { Workspace(store: store).preferredColorScheme(.dark).tint(Palette.orange).font(Palette.font()).foregroundStyle(Palette.text) } }
}
struct BackupDocument: FileDocument {
    static let readableContentTypes: [UTType] = [.json]
    var data: Data
    init(_ data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}
private struct OpenRequest: Identifiable { let id = UUID(); let host: Host; let sftp: Bool }
private struct TrustRequest: Identifiable { let id = UUID(); let host: Host; let fingerprint: String; let respond: (Bool) -> Void }
struct Workspace: View {
    @ObservedObject var store: HostStore
    @Environment(\.scenePhase) private var phase
    @State private var sessions: [Connection] = []
    @State private var selected: UUID?
    @State private var splitCount = 1
    @State private var panes: [UUID] = []
    @State private var editor: Host?
    @State private var openRequest: OpenRequest?
    @State private var trustRequest: TrustRequest?
    @State private var pendingConnection: (() -> Void)?
    @State private var error = ""
    @State private var showError = false
    @State private var about = false
    @State private var importing = false
    @State private var exporting = false
    @State private var backupInfo = false
    @State private var backup = BackupDocument(Data())
    @State private var search = ""
    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 18) {
                Text("wShell").font(Palette.font(24, bold: true)).padding(.top, 20)
                Text("YOUR CONNECTIONS").font(Palette.font(11)).foregroundStyle(Palette.muted)
                TextField("Search hosts", text: $search).textFieldStyle(.roundedBorder)
                Button { editor = Host() } label: { Label("Add a host", systemImage: "plus").frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 8) }
                List(store.hosts.filter { search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) }) { host in
                    VStack(alignment: .leading, spacing: 10) {
                        Button(host.name) { openRequest = OpenRequest(host: host, sftp: false) }.font(Palette.font(14, bold: true))
                        Text("\(host.user)@\(host.address)").font(Palette.font(11)).foregroundStyle(Palette.muted).lineLimit(1)
                        HStack {
                            Button("SFTP") { openRequest = OpenRequest(host: host, sftp: true) }
                            Spacer()
                            Button("Edit") { editor = host }
                        }.buttonStyle(.borderless)
                    }.padding(.vertical, 8).listRowBackground(Palette.panel)
                }.listStyle(.plain).scrollContentBackground(.hidden)
                Button("Export / Import") { backupInfo = true }
                Button("About wShell") { about = true }
            }.padding(18).background(Palette.panel).navigationBarHidden(true)
        } detail: {
            VStack(spacing: 0) {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        Button("Home") { select(nil) }
                        ForEach(sessions) { session in
                            HStack(spacing: 12) {
                                Button((session.isSFTP ? "⇅ " : "") + session.host.name) { select(session.id) }
                                Button { close(session) } label: { Image(systemName: "xmark") }.accessibilityLabel("Close \(session.host.name)")
                            }.padding(12).background(selected == session.id ? Palette.orange.opacity(0.2) : Palette.panel).clipShape(RoundedRectangle(cornerRadius: 8))
                        }
                        Menu("Split") {
                            ForEach(1...4, id: \.self) { count in
                                Button(count == 1 ? "Single pane" : "\(count) panes") { setSplit(count) }.disabled(sessions.count < count || selected == nil)
                            }
                        }.padding(.horizontal, 12)
                    }.padding(12)
                }.background(Palette.panel)
                if selected != nil, !visibleSessions.isEmpty {
                    GeometryReader { geometry in
                        let visible = visibleSessions
                        let frames = SplitLayout.frames(count: visible.count, in: CGRect(origin: .zero, size: geometry.size))
                        ForEach(Array(visible.enumerated()), id: \.element.id) { index, session in
                            SessionPane(connection: session) { openRequest = OpenRequest(host: session.host, sftp: session.isSFTP) }
                                .overlay { Rectangle().stroke(selected == session.id && visible.count > 1 ? Palette.orange : Palette.muted.opacity(0.2), lineWidth: visible.count > 1 ? 2 : 0).allowsHitTesting(false) }
                                .frame(width: frames[index].width, height: frames[index].height)
                                .position(x: frames[index].midX, y: frames[index].midY)
                                .simultaneousGesture(TapGesture().onEnded { select(session.id) })
                        }
                    }
                } else {
                    VStack(alignment: .leading, spacing: 22) {
                        Text("Made for the\ncommand line.").font(Palette.font(32, bold: true))
                        Text("Your servers. Your iPad.\nSSH terminals and SFTP, side by side.").foregroundStyle(Palette.muted)
                        Button { editor = Host() } label: { Label("Add a host", systemImage: "plus").padding(12) }.buttonStyle(.borderedProminent)
                        Text("Personal preview · Created by Hyunwook Park").font(Palette.font(11)).foregroundStyle(Palette.muted)
                    }.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
                }
            }.background(Palette.background).navigationBarHidden(true)
        }.navigationSplitViewStyle(.balanced)
        .sheet(item: $editor) { host in HostEditor(store: store, host: host) }
        .sheet(item: $openRequest, onDismiss: {
            let action = pendingConnection; pendingConnection = nil; action?()
        }) { request in
            AuthenticationSheet(host: request.host) { password, key, passphrase in
                pendingConnection = { connect(request, password, key, passphrase) }; openRequest = nil
            }
        }
        .sheet(item: $trustRequest, onDismiss: { trustRequest?.respond(false) }) { request in
            VStack(alignment: .leading, spacing: 24) {
                Text("Verify server identity").font(Palette.font(20, bold: true))
                Text(request.host.endpoint)
                Text(request.fingerprint).textSelection(.enabled)
                Text("Compare this SHA-256 fingerprint with your server administrator or trusted console before connecting. Your credentials have not been sent.").foregroundStyle(Palette.muted)
                HStack {
                    Button("Cancel") { request.respond(false); trustRequest = nil }
                    Spacer()
                    Button("Trust and connect") {
                        do { try store.trust(request.host, request.fingerprint); request.respond(true) }
                        catch { request.respond(false); report(error.localizedDescription) }
                        trustRequest = nil
                    }.buttonStyle(.borderedProminent)
                }
            }.padding(32).interactiveDismissDisabled()
        }
        .sheet(isPresented: $about) { AboutPane() }
        .confirmationDialog("Settings backup", isPresented: $backupInfo, titleVisibility: .visible) {
            Button("Export settings") { do { backup = BackupDocument(try store.export()); exporting = true } catch { report(error.localizedDescription) } }
            Button("Import settings") { importing = true }
        } message: { Text(HostStore.backupNotice) }
        .fileExporter(isPresented: $exporting, document: backup, contentType: .json, defaultFilename: "wShell-iPad-settings") { result in
            switch result { case .success: report("Export complete. " + HostStore.backupNotice); case .failure(let error): report(error.localizedDescription) }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.json]) { result in
            do {
                let url = try result.get(), access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                try store.restore(Data(contentsOf: url)); report("Import complete. " + HostStore.backupNotice)
            } catch { report(error.localizedDescription) }
        }
        .alert("wShell", isPresented: $showError) { Button("OK", role: .cancel) {} } message: { Text(error) }
        .task { do { try store.load() } catch { report(error.localizedDescription) }; Smoke.runIfRequested(store) }
        .onChange(of: phase) { _, next in
            if next == .background { sessions.forEach { $0.stop() } }
        }
    }
    private func report(_ message: String) { error = message; showError = true }
    private var visibleSessions: [Connection] {
        guard let selected else { return [] }
        if splitCount == 1 { return sessions.filter { $0.id == selected } }
        var ids = panes.filter { id in sessions.contains { $0.id == id } }
        if !ids.contains(selected) { ids.insert(selected, at: 0) }
        for session in sessions where ids.count < splitCount && !ids.contains(session.id) { ids.append(session.id) }
        return ids.prefix(splitCount).compactMap { id in sessions.first { $0.id == id } }
    }
    private func select(_ id: UUID?) {
        if let id, splitCount > 1, !visibleSessions.contains(where: { $0.id == id }) {
            panes = visibleSessions.map(\.id)
            if let index = panes.firstIndex(where: { $0 == selected }) { panes[index] = id }
            else if !panes.isEmpty { panes[0] = id }
        }
        selected = id
    }
    private func setSplit(_ count: Int) {
        guard let selected else { return }
        splitCount = count; panes = [selected] + sessions.filter { $0.id != selected }.prefix(count-1).map(\.id)
    }
    private func close(_ session: Connection) {
        session.stop(); sessions.removeAll { $0.id == session.id }; panes.removeAll { $0 == session.id }
        if selected == session.id { selected = panes.first ?? sessions.last?.id }
    }
    private func connect(_ request: OpenRequest, _ password: String, _ key: Data?, _ passphrase: String) {
        guard sessions.count < 16 else { report("Close a tab before opening more than 16 sessions."); return }
        let session = Connection(host: request.host, sftp: request.sftp)
        if !request.sftp { session.terminal = TerminalPane(session) }
        sessions.append(session); select(session.id)
        session.start(password: password, key: key, passphrase: passphrase) { fingerprint, respond in
            if let known = store.fingerprint(request.host) {
                if known == fingerprint { respond(true) }
                else { respond(false); report("HOST KEY CHANGED for \(request.host.endpoint). Connection blocked. Verify the server independently before removing trust in host settings.") }
            } else if trustRequest == nil {
                trustRequest = TrustRequest(host: request.host, fingerprint: fingerprint, respond: respond)
            } else { respond(false); report("Finish verifying the other server, then reconnect.") }
        }
    }
}
private struct SessionPane: View {
    @ObservedObject var connection: Connection
    let duplicate: () -> Void
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading) {
                    Text(connection.host.name).font(Palette.font(11, bold: true)).lineLimit(1)
                    Text(connection.status).lineLimit(2).font(Palette.font(11)).foregroundStyle(Palette.muted)
                }
                Spacer()
                Menu("Tab") {
                    Button(connection.ready ? "Duplicate tab" : "Reconnect", action: duplicate)
                    Button("Disconnect") { connection.stop() }.disabled(!connection.ready)
                }
            }.padding(12)
            if connection.isSFTP {
                if connection.ready { FilesPane(connection) }
                else { Spacer(); Text(connection.status).padding(); Spacer() }
            } else { TerminalSurface(connection: connection) }
        }
    }
}
private struct AuthenticationSheet: View {
    let host: Host
    let connect: (String, Data?, String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var password = ""
    @State private var passphrase = ""
    @State private var error = ""
    @State private var remember = false
    @State private var hasSaved = false
    var body: some View {
        NavigationStack {
            Form {
                Section(host.name) {
                    Text("\(host.user)@\(host.endpoint)")
                    if host.publicKey {
                        Text("Public-key authentication signs with your imported private key. A .pub file alone cannot log in.")
                        SecureField("Private-key passphrase (if encrypted)", text: $passphrase)
                    } else {
                        SecureField(hasSaved ? "Leave blank to use saved password" : "SSH password", text: $password)
                        Toggle("Save password on this iPad", isOn: $remember)
                        Text("Saved passwords are protected by Keychain and excluded from export/import.").font(Palette.font(11))
                    }
                }
                if !error.isEmpty { Text(error).foregroundStyle(Palette.orange) }
                Button("Connect") { submit() }.buttonStyle(.borderedProminent)
            }.navigationTitle("Connect").toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }.task { do { hasSaved = try Vault.get(host.credential + "\npassword") != nil } catch { self.error = error.localizedDescription } }
    }
    private func submit() {
        do {
            let key = host.publicKey ? try Vault.get(host.credential + "\nkey") : nil
            if host.publicKey && key == nil { throw AppError("Import your private key in Edit host first.") }
            let saved = try Vault.get(host.credential + "\npassword")
            let secret = password.isEmpty ? String(data: saved ?? Data(), encoding: .utf8) ?? "" : password
            if !host.publicKey && secret.isEmpty { throw AppError("Enter your SSH password.") }
            if remember && !host.publicKey { try Vault.put(host.credential + "\npassword", Data(secret.utf8)) }
            connect(secret, key, passphrase)
        } catch { self.error = error.localizedDescription }
    }
}
private struct HostEditor: View {
    @ObservedObject var store: HostStore
    @State var host: Host
    @Environment(\.dismiss) private var dismiss
    @State private var importing = false
    @State private var key: Data?
    @State private var keyName = ""
    @State private var error = ""
    @State private var delete = false
    @State private var forgetTrust = false
    var body: some View {
        NavigationStack {
            Form {
                Section("Connection") {
                    TextField("Name", text: $host.name)
                    TextField("Host or IP address", text: $host.address).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("Username", text: $host.user).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("Port", value: $host.port, format: .number).keyboardType(.numberPad)
                    Picker("Authentication", selection: $host.publicKey) { Text("Password").tag(false); Text("Public key").tag(true) }
                }
                if host.publicKey {
                    Section("Private key") {
                        Button("Import private key from Files") { importing = true }
                        Text(keyName.isEmpty ? "OpenSSH or PEM · RSA, Ed25519, ECDSA. Stored only in this iPad's Keychain. PPK: export OpenSSH from desktop first." : keyName).font(Palette.font(11))
                        Text("Install the matching public key in the server's authorized_keys. Keep the private key on your device.").font(Palette.font(11))
                    }
                }
                Section("Device security") {
                    Button("Forget saved password") { do { try Vault.remove(host.credential + "\npassword") } catch { self.error = error.localizedDescription } }
                    Button("Forget private key") { do { try Vault.remove(host.credential + "\nkey"); key = nil; keyName = "" } catch { self.error = error.localizedDescription } }
                    Button("Reset trusted host key") { forgetTrust = true }
                }
                if !error.isEmpty { Text(error).foregroundStyle(Palette.orange) }
                if store.hosts.contains(where: { $0.id == host.id }) { Button("Delete host", role: .destructive) { delete = true } }
            }.navigationTitle("Host settings").toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) { Button("Save") {
                    do {
                        try host.validate()
                        if let key { try Vault.put(host.credential + "\nkey", key) }
                        try store.update(host); dismiss()
                    } catch { self.error = error.localizedDescription }
                } }
            }
        }.fileImporter(isPresented: $importing, allowedContentTypes: [.data, .plainText]) { result in
            do {
                let url = try result.get(), access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max
                guard size <= 1_048_576 else { throw AppError("Key file exceeds 1 MiB.") }
                let data = try Data(contentsOf: url)
                guard let text = String(data: data, encoding: .utf8), text.contains("PRIVATE KEY-----"), !data.contains(0) else { throw AppError("Select a PEM/OpenSSH private key, not a .pub or PPK file.") }
                key = data; keyName = url.lastPathComponent
            } catch { self.error = error.localizedDescription }
        }.alert("Delete host?", isPresented: $delete) {
            Button("Cancel", role: .cancel) {}
            Button("Delete", role: .destructive) { do { try store.delete(host); dismiss() } catch { self.error = error.localizedDescription } }
        }.alert("Reset trust for \(host.endpoint)?", isPresented: $forgetTrust) {
            Button("Cancel", role: .cancel) {}
            Button("Reset", role: .destructive) { do { try store.forgetTrust(host) } catch { self.error = error.localizedDescription } }
        } message: { Text("Only do this after independently verifying a server key change. The next connection will ask you to verify its fingerprint.") }
    }
}
private struct AboutPane: View {
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    Text("wShell for iPad").font(Palette.font(24, bold: true))
                    Text("Personal preview \(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "")\nCreated by Hyunwook Park")
                    Text("Free Apple account installations expire after 7 days and require signing again. Back up settings before reinstalling. Passwords and private keys are excluded from backups.")
                    Text("Connections close when the app enters the background. iPadOS does not provide a local CMD or system shell.")
                    Text((try? String(contentsOf: Bundle.main.url(forResource: "legal-notices", withExtension: "txt")!, encoding: .utf8)) ?? "Licenses unavailable").font(Palette.font(11)).textSelection(.enabled)
                }.padding(24)
            }.toolbar { Button("Done") { dismiss() } }
        }
    }
}
