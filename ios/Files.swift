import SwiftUI
import UniformTypeIdentifiers

final class FileBrowser: ObservableObject {
    @Published var entries: [SftpEntry] = []
    @Published var locals: [URL] = []
    @Published var path = "."
    @Published var busy = false
    @Published var showHidden = false
    @Published var message = ""
    let connection: Connection
    let directory: URL
    init(_ connection: Connection) {
        self.connection = connection
        directory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Files", isDirectory: true)
        do { try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true); try refreshLocal() }
        catch { message = error.localizedDescription }
    }
    func refreshLocal() throws {
        locals = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.isRegularFileKey, .isHiddenKey]).filter { (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }
    func perform(_ operation: @escaping (FileClient) throws -> Void) {
        guard !busy else { return }; busy = true; message = "Working"
        connection.queue.async {
            do {
                guard let ftp = self.connection.ftp, !self.connection.stopped else { throw AppError("Reconnect to browse files.") }
                try operation(FileClient(ftp, self.connection))
                DispatchQueue.main.async { self.busy = false; self.message = "Done"; do { try self.refreshLocal() } catch { self.message = error.localizedDescription } }
            } catch { DispatchQueue.main.async { self.busy = false; self.message = error.localizedDescription } }
        }
    }
    func refresh(_ location: String? = nil) {
        let target = location ?? path
        perform { client in
            let canonical = try client.canonical(target), entries = try client.list(canonical)
            DispatchQueue.main.async { self.path = canonical; self.entries = entries.filter { $0.name != "." && $0.name != ".." } }
        }
    }
    func transfer(_ local: URL?, _ remote: SftpEntry?, replace: Bool) {
        let path = self.path
        perform { client in
            let progress: (UInt64, UInt64) -> Void = { done, total in
                // At most one UI update per 256 KiB, plus completion.
                if done == total || done % 262144 < 32768 {
                    DispatchQueue.main.async { self.message = "\(done / 1024) / \(total / 1024) KiB" }
                }
            }
            if let local {
                guard portableName(local.lastPathComponent) else { throw AppError("Choose a portable filename.") }
                try client.upload(local, remoteJoin(path, local.lastPathComponent), replace: replace, progress: progress)
            } else if let remote {
                guard portableName(remote.name) else { throw AppError("Unsafe local filename rejected.") }
                try client.download(remoteJoin(path, remote.name), self.directory.appendingPathComponent(remote.name), replace: replace, progress: progress)
            }
            let entries = try client.list(path)
            DispatchQueue.main.async { self.entries = entries.filter { $0.name != "." && $0.name != ".." } }
        }
    }
    func importFiles(_ urls: [URL]) {
        do {
            for url in urls {
                let access = url.startAccessingSecurityScopedResource(); defer { if access { url.stopAccessingSecurityScopedResource() } }
                guard portableName(url.lastPathComponent), (try url.resourceValues(forKeys: [.isRegularFileKey])).isRegularFile == true else { throw AppError("Import regular files only.") }
                // copyItem refuses to overwrite an existing local file.
                try FileManager.default.copyItem(at: url, to: directory.appendingPathComponent(url.lastPathComponent))
            }
            try refreshLocal(); message = "Files imported"
        } catch { message = error.localizedDescription }
    }
}
struct FilesPane: View {
    @StateObject var browser: FileBrowser
    @State private var local: URL?
    @State private var remote: String?
    @State private var importing = false
    @State private var overwrite = false
    @State private var uploading = false
    @State private var folder = ""
    @State private var folderDialog = false
    @State private var deleteDialog = false
    init(_ connection: Connection) { _browser = StateObject(wrappedValue: FileBrowser(connection)) }
    private var selected: SftpEntry? { browser.entries.first { $0.name == remote } }
    var body: some View {
        VStack(spacing: 12) {
            HStack {
                Button("Import files") { importing = true }
                if let local { ShareLink("Share local file", item: local) }
                Spacer()
                Button("Refresh") { browser.refresh() }
                Button("Cancel") { browser.connection.stop() }.disabled(!browser.busy)
            }
            Toggle(isOn: $browser.showHidden) { Label("Show hidden files", systemImage: browser.showHidden ? "checkmark.square.fill" : "square") }
                .toggleStyle(.button).frame(maxWidth: .infinity, alignment: .leading).disabled(browser.busy)
            HStack(alignment: .top, spacing: 12) {
                VStack(alignment: .leading) {
                    Text("ON MY IPAD").foregroundStyle(Palette.muted)
                    Text("wShell / Files").lineLimit(1)
                    List(browser.locals.filter { browser.showHidden || (!$0.lastPathComponent.hasPrefix(".") && (try? $0.resourceValues(forKeys: [.isHiddenKey]).isHidden) != true) }, id: \.self, selection: $local) { url in
                        Text(url.lastPathComponent).tag(url).listRowBackground(Palette.panel)
                    }.scrollContentBackground(.hidden)
                    Button("Upload →") { transfer(true) }.disabled(local == nil || browser.busy)
                }
                Rectangle().fill(Palette.muted.opacity(0.2)).frame(width: 1)
                VStack(alignment: .leading) {
                    Text("REMOTE SFTP").foregroundStyle(Palette.muted)
                    HStack {
                        Button("↑") { browser.refresh(remoteJoin(browser.path, "..")) }
                        Text(browser.path).lineLimit(1).truncationMode(.middle)
                    }
                    List(browser.entries.filter { browser.showHidden || !$0.name.hasPrefix(".") }, id: \.name, selection: $remote) { entry in
                        HStack {
                            Image(systemName: entry.directory ? "folder" : entry.regular ? "doc" : "link")
                            Text(entry.name).lineLimit(1)
                            Spacer()
                            if entry.directory { Button("Open") { browser.refresh(remoteJoin(browser.path, entry.name)) }.buttonStyle(.borderless) }
                        }.tag(entry.name).listRowBackground(Palette.panel)
                    }.scrollContentBackground(.hidden)
                    HStack {
                        Button("← Download") { transfer(false) }.disabled(selected?.regular != true)
                        Spacer()
                        Button("New folder") { folder = ""; folderDialog = true }
                        Button("Delete", role: .destructive) { deleteDialog = true }.disabled(selected == nil)
                    }.disabled(browser.busy)
                }
            }
            Text(browser.message).frame(maxWidth: .infinity, alignment: .leading).foregroundStyle(Palette.muted)
            Text("Files only · No folder recursion · Cancelling disconnects this tab; remote .part files may remain.").font(Palette.font(11)).foregroundStyle(Palette.muted)
        }.padding(16)
        .onChange(of: browser.showHidden) { _, _ in local = nil; remote = nil }
        .task { if browser.connection.ready { browser.refresh() } }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: true) { result in
            switch result { case .success(let urls): browser.importFiles(urls); case .failure(let error): browser.message = error.localizedDescription }
        }
        .alert("Replace existing file?", isPresented: $overwrite) {
            Button("Cancel", role: .cancel) {}
            Button("Replace", role: .destructive) { browser.transfer(uploading ? local : nil, uploading ? nil : selected, replace: true) }
        } message: { Text("Replace \(uploading ? local?.lastPathComponent ?? "" : remote ?? "") on \(uploading ? browser.path : "this iPad") after the transfer completes?") }
        .alert("New remote folder", isPresented: $folderDialog) {
            TextField("Folder name", text: $folder)
            Button("Cancel", role: .cancel) {}
            Button("Create") {
                guard portableName(folder) else { browser.message = "Choose a portable folder name."; return }
                let destination = remoteJoin(browser.path, folder)
                browser.perform { try $0.mkdir(destination) }
            }
        }
        .alert("Delete \(remote ?? "")?", isPresented: $deleteDialog) {
            Button("Cancel", role: .cancel) {}
            Button("Delete", role: .destructive) {
                guard let entry = selected else { return }
                let target = remoteJoin(browser.path, entry.name)
                browser.perform { try $0.remove(target, directory: entry.directory) }
            }
        } message: { Text("This removes the remote file or empty folder permanently.") }
    }
    private func transfer(_ upload: Bool) {
        uploading = upload
        if upload, let local { overwrite = browser.entries.contains { $0.name == local.lastPathComponent } }
        else if let selected { overwrite = FileManager.default.fileExists(atPath: browser.directory.appendingPathComponent(selected.name).path) }
        if !overwrite { browser.transfer(upload ? local : nil, upload ? nil : selected, replace: false) }
    }
}
