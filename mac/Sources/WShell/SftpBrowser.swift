import AppKit
import WShellCore

private final class SftpRow: NSTableRowView {
    override func drawSelection(in dirtyRect: NSRect) {
        Theme.orange.withAlphaComponent(0.28).setFill(); bounds.fill()
        Theme.orange.setFill(); NSRect(x: 0, y: 0, width: 3, height: bounds.height).fill()
    }
}
final class SftpBrowser: NSView, NSTableViewDataSource, NSTableViewDelegate {
    override var isFlipped: Bool { true }
    let client: SftpClient
    let tables = [NSTableView(), NSTableView()], scrolls = [NSScrollView(), NSScrollView()]
    let paths = [Theme.input("Local folder"), Theme.input("Remote folder")]
    let headings = [Theme.label("LOCAL", size: 11, bold: true), Theme.label("REMOTE", size: 11, bold: true)]
    let status = Theme.label("Connecting to SFTP…", size: 11), progress = NSProgressIndicator()
    var actions: [ActionButton] = [], navigation: [ActionButton] = []
    var local = FileManager.default.homeDirectoryForCurrentUser, remote = "/", entries: [[SftpEntry]] = [[], []]
    private(set) var busy = false, connected = false, closed = false
    var focused = 1
    private let queue = DispatchQueue(label: "wShell.SFTP", qos: .userInitiated)
    init(client: SftpClient) {
        self.client = client; super.init(frame: .zero)
        wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
        let titles = ["Upload →", "← Download", "New folder", "Rename", "Delete", "Refresh", "Cancel"]
        actions = titles.enumerated().map { index, title in ActionButton(title, accent: index < 2) { [weak self] in self?.action(index) } }
        actions.forEach { addSubview($0) }
        for side in 0...1 {
            let table = tables[side]; table.backgroundColor = Theme.background; table.dataSource = self; table.delegate = self
            table.allowsMultipleSelection = true; table.rowHeight = 28; table.intercellSpacing = NSSize(width: 0, height: 3)
            table.target = self; table.doubleAction = #selector(openFolder(_:)); table.action = #selector(selectPane(_:))
            for (index, title) in ["Name", "Bytes", "Modified"].enumerated() {
                let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(String(index))); column.title = title
                column.headerCell.font = Theme.font(11); column.width = index == 0 ? 240 : 100; table.addTableColumn(column)
            }
            scrolls[side].documentView = table; scrolls[side].hasVerticalScroller = true; scrolls[side].hasHorizontalScroller = true
            scrolls[side].autohidesScrollers = true; scrolls[side].drawsBackground = false
            [scrolls[side], paths[side], headings[side]].forEach { addSubview($0) }
            paths[side].target = self; paths[side].action = #selector(goPath(_:))
            let up = ActionButton("↑") { [weak self] in guard let self else { return }; self.navigate(side, side == 0 ? self.local.deletingLastPathComponent().path : remoteJoin(self.remote, "..")) }
            let go = ActionButton("Go") { [weak self] in guard let self else { return }; self.navigate(side, self.paths[side].stringValue) }
            navigation += [up, go]; addSubview(up); addSubview(go)
        }
        status.maximumNumberOfLines = 3; status.lineBreakMode = .byWordWrapping
        progress.isIndeterminate = false; progress.minValue = 0; progress.maxValue = 1; progress.style = .bar
        addSubview(status); addSubview(progress)
        do { try readLocal() } catch { status.stringValue = error.localizedDescription }
        run { [self] in let home = try client.initialize(); return (home, try client.list(home), "Connected · Select files, then Upload or Download. Double-click folders to browse.") }
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    deinit { client.cancel() }
    func stop() { closed = true; client.cancel() }
    override func layout() {
        super.layout(); let w = bounds.width, h = bounds.height
        var x: CGFloat = 16, y: CGFloat = 14
        for (button, width) in zip(actions, [112.0, 130, 120, 92, 86, 96, 90]) {
            if x + width > w - 16 { x = 16; y += 44 }
            button.frame = NSRect(x: x, y: y, width: width, height: 34); x += width + 8
        }
        let top = y + 50, span = (w - 48) / 2
        for side in 0...1 {
            let left = 16 + CGFloat(side) * (span + 16)
            headings[side].frame = NSRect(x: left, y: top, width: span, height: 24)
            navigation[side*2].frame = NSRect(x: left, y: top+30, width: 40, height: 30)
            paths[side].frame = NSRect(x: left+50, y: top+34, width: span-100, height: 24)
            navigation[side*2+1].frame = NSRect(x: left+span-42, y: top+30, width: 42, height: 30)
            scrolls[side].frame = NSRect(x: left, y: top+72, width: span, height: max(30, h-top-142))
            tables[side].tableColumns[0].width = max(120, span-195)
            tables[side].tableColumns[1].width = 82; tables[side].tableColumns[2].width = 104
        }
        progress.frame = NSRect(x: 16, y: h-70, width: w-32, height: 6)
        status.frame = NSRect(x: 16, y: h-58, width: w-32, height: 52)
    }
    func numberOfRows(in tableView: NSTableView) -> Int { entries[tableView === tables[0] ? 0 : 1].count }
    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? { SftpRow() }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let side = tableView === tables[0] ? 0 : 1, entry = entries[side][row]
        let column = Int(tableColumn?.identifier.rawValue ?? "0") ?? 0
        var text = (entry.directory ? "▸  " : entry.regular ? "   " : "↗  ") + entry.name
        if column == 1 { text = entry.directory ? "Folder" : entry.regular ? String(entry.size) : "Link / other" }
        if column == 2 { text = entry.modified == 0 ? "" : Date(timeIntervalSince1970: Double(entry.modified)).formatted(.iso8601.year().month().day().dateSeparator(.dash)) }
        let label = Theme.label(text, size: column == 0 ? 13 : 11, color: column == 0 ? Theme.text : Theme.muted); label.toolTip = text
        return label
    }
    @objc func selectPane(_ sender: NSTableView) { focused = sender === tables[0] ? 0 : 1; state() }
    @objc func openFolder(_ sender: NSTableView) {
        let side = sender === tables[0] ? 0 : 1, row = sender.clickedRow
        if entries[side].indices.contains(row), entries[side][row].directory { navigate(side, side == 0 ? local.appendingPathComponent(entries[side][row].name).path : remoteJoin(remote, entries[side][row].name)) }
    }
    @objc func goPath(_ sender: NSTextField) { let side = sender === paths[0] ? 0 : 1; navigate(side, sender.stringValue) }
    func state() {
        for (i, button) in actions.enumerated() { button.isEnabled = i == 6 ? busy : !busy && connected }
        for (i, button) in navigation.enumerated() { button.isEnabled = !busy && (i < 2 || connected) }
        paths.enumerated().forEach { $0.element.isEnabled = !busy && ($0.offset == 0 || connected) }
        progress.isHidden = !busy
        for side in 0...1 { headings[side].stringValue = "\(side == 0 ? "LOCAL" : "REMOTE") · \(entries[side].count) items"; headings[side].textColor = focused == side ? Theme.orange : Theme.muted }
    }
    func readLocal() throws {
        let urls = try FileManager.default.contentsOfDirectory(at: local, includingPropertiesForKeys: nil)
        entries[0] = try urls.prefix(100000).map { url in
            let a = try FileManager.default.attributesOfItem(atPath: url.path), type = a[.type] as? FileAttributeType
            return SftpEntry(name: url.lastPathComponent, size: (a[.size] as? NSNumber)?.uint64Value ?? 0,
                mode: type == .typeDirectory ? 0o040000 : type == .typeRegular ? 0o100000 : 0o120000,
                modified: UInt32(clamping: Int64((a[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0)))
        }.sorted { $0.directory != $1.directory ? $0.directory : $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        paths[0].stringValue = local.path; tables[0].reloadData(); state()
    }
    func run(_ work: @escaping () throws -> (String, [SftpEntry], String)) {
        guard !busy && !closed else { return }; busy = true; progress.doubleValue = 0; state()
        queue.async { [self] in
            let result = Result { try work() }
            DispatchQueue.main.async { [self] in
                guard !closed else { return }; busy = false
                switch result {
                case let .success((path, list, message)):
                    connected = true; remote = path; entries[1] = list; paths[1].stringValue = path; tables[1].reloadData(); status.stringValue = message
                    do { try readLocal() } catch { status.stringValue = error.localizedDescription }
                case let .failure(error):
                    connected = false; client.cancel(); status.stringValue = error.localizedDescription + " Reconnect to continue. An interrupted upload may leave a .wshell-*.part file on the server."
                }; state()
            }
        }
    }
    func navigate(_ side: Int, _ path: String) {
        guard !busy else { return }
        if side == 1 {
            guard connected else { return }
            run { [self] in let p = try client.canonical(path); return (p, try client.list(p), "Ready · Files transfer individually; browse folders to transfer their contents.") }
        } else {
            let old = local; local = URL(fileURLWithPath: path).standardizedFileURL
            do { try readLocal() } catch { local = old; status.stringValue = error.localizedDescription }
        }
    }
    func selected(_ side: Int) -> [SftpEntry] { tables[side].selectedRowIndexes.compactMap { entries[side].indices.contains($0) ? entries[side][$0] : nil } }
    func transfer(_ upload: Bool) throws {
        let files = selected(upload ? 0 : 1)
        guard !files.isEmpty else { status.stringValue = "Select one or more files in the source pane first."; return }
        guard files.allSatisfy({ $0.regular && portableName($0.name) }) else { throw WShellError("Select regular files with portable filenames. Browse folders to transfer their contents; links are not followed.") }
        let approved = Set(files.filter { item in upload ? entries[1].contains(where: { $0.name == item.name }) : FileManager.default.fileExists(atPath: local.appendingPathComponent(item.name).path) }.map(\.name))
        if !approved.isEmpty {
            let alert = Theme.alert("Replace \(approved.count) existing file(s)?", "Destination: \(upload ? remote : local.path)\n\n" + approved.sorted().prefix(8).joined(separator: "\n") + "\n\nOnly completed transfers replace these files. Cancelled or failed transfers keep the previous file.")
            alert.addButton(withTitle: "Cancel"); alert.addButton(withTitle: "Replace files")
            guard alert.runModal() == .alertSecondButtonReturn else { return }
        }
        let folder = local, path = remote
        run { [self] in
            for (index, file) in files.enumerated() {
                var last = Date.distantPast
                let update: (UInt64, UInt64) -> Void = { [weak self] done, total in
                    guard done == total || Date().timeIntervalSince(last) > 0.1 else { return }; last = Date()
                    DispatchQueue.main.async { [weak self] in
                        guard let self, !self.closed else { return }
                        self.status.stringValue = "\(upload ? "Uploading" : "Downloading") \(file.name) (\(index+1)/\(files.count)) · \(done) / \(total) bytes"
                        self.progress.doubleValue = total == 0 ? 1 : Double(done)/Double(total)
                    }
                }
                if upload { try client.upload(folder.appendingPathComponent(file.name), remoteJoin(path, file.name), replace: approved.contains(file.name), progress: update) }
                else { try client.download(remoteJoin(path, file.name), folder.appendingPathComponent(file.name), replace: approved.contains(file.name), progress: update) }
            }
            return (path, try client.list(path), "\(files.count) file(s) \(upload ? "uploaded" : "downloaded") successfully.")
        }
    }
    func action(_ index: Int) {
        if index == 6 { client.cancel(); status.stringValue = "Cancelling transfer…"; return }
        guard !busy && connected else { return }
        do {
            if index < 2 { try transfer(index == 0); return }
            if index == 5 { try readLocal(); navigate(1, remote); return }
            let side = focused, files = selected(side)
            if index != 2 && files.isEmpty { return }
            if index == 3 && files.count != 1 { throw WShellError("Select one file or folder to rename.") }
            var name = ""
            if index == 2 || index == 3 {
                let alert = Theme.alert(index == 2 ? "New folder" : "Rename", side == 1 ? "Remote name" : "Local name")
                let field = Theme.input(); field.frame = NSRect(x: 0, y: 0, width: 440, height: 30); field.stringValue = index == 3 ? files[0].name : ""
                alert.accessoryView = field; alert.addButton(withTitle: "Continue"); alert.addButton(withTitle: "Cancel"); alert.window.initialFirstResponder = field
                guard alert.runModal() == .alertFirstButtonReturn else { return }; name = field.stringValue
                guard portableName(name) else { throw WShellError("Enter a single portable filename, without path separators or reserved characters.") }
            } else {
                let alert = Theme.alert("Delete \(files.count) selected item(s)?", "Permanently remove these items from the \(side == 1 ? "remote" : "local") pane. Only files, links and empty folders are removed. This does not use the Trash.")
                alert.addButton(withTitle: "Cancel"); alert.addButton(withTitle: "Delete items")
                guard alert.runModal() == .alertSecondButtonReturn else { return }
            }
            let folder = local, path = remote, newName = name
            run { [self] in
                if side == 1 {
                    if index == 2 { try client.mkdir(remoteJoin(path, newName)) }
                    else if index == 3 { try client.rename(remoteJoin(path, files[0].name), remoteJoin(path, newName)) }
                    else { for file in files { try client.remove(remoteJoin(path, file.name), directory: file.directory) } }
                } else {
                    let target = folder.appendingPathComponent(newName)
                    if index == 2 {
                        guard !FileManager.default.fileExists(atPath: target.path) else { throw WShellError("The destination already exists.") }
                        try FileManager.default.createDirectory(at: target, withIntermediateDirectories: false)
                    } else if index == 3 { try FileManager.default.moveItem(at: folder.appendingPathComponent(files[0].name), to: target) }
                    else { for file in files {
                        let path = folder.appendingPathComponent(file.name).path
                        if (file.directory ? rmdir(path) : unlink(path)) != 0 { throw WShellError("Cannot remove \(file.name). Only empty folders can be deleted.") }
                    } }
                }
                return (path, try client.list(path), "Done · Folder operations apply to the highlighted pane.")
            }
        } catch { status.stringValue = error.localizedDescription }
    }
}
