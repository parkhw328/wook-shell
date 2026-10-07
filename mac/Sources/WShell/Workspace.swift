import AppKit
import SwiftTerm
import WShellCore

final class Workspace: NSObject, NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate, LocalProcessTerminalViewDelegate {
    let store: SettingsStore
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1200, height: 780), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
    let root = Canvas(), sidebar = Canvas(), home = Canvas(), tabBar = Canvas(), content = Canvas()
    let table = NSTableView(), scroll = NSScrollView(), tabsScroll = NSScrollView()
    let search = Theme.input("Find a host"), quick = Theme.input("user@hostname"), status = Theme.label("LOCAL DATA  ·  Flexoki Dark / JetBrains Mono", size: 11, color: Theme.muted)
    var sessions: [Session] = [], selected: UUID?, hosts: [Record] = [], allHosts: [Record] = []
    var tabButtons: [ActionButton] = [], sidebarViews: [NSView] = [], homeViews: [NSView] = []
    var smoke: SmokeTest?
    var splitCount = 1, panes: [UUID] = []
    var paneHeaders: [UUID:ActionButton] = [:]
    var focusMonitor: Any?
    lazy var splitButton = ActionButton("Split") { [weak self] in self?.splitMenu() }
    init(store: SettingsStore) { self.store = store; super.init() }
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.appearance = NSAppearance(named: .darkAqua); Theme.loadFonts(); buildMenu()
        window.title = "wShell"; window.minSize = NSSize(width: 1060, height: 720); window.delegate = self
        window.backgroundColor = Theme.background; window.contentView = root; root.wantsLayer = true
        window.setContentSize(NSSize(width: 1200, height: 780))
        root.layer?.backgroundColor = Theme.background.cgColor
        sidebar.wantsLayer = true; sidebar.layer?.backgroundColor = Theme.panel.cgColor
        [sidebar, content, tabsScroll, status, splitButton].forEach { root.addSubview($0) }
        content.addSubview(home); tabsScroll.documentView = tabBar; tabsScroll.hasHorizontalScroller = true
        tabsScroll.drawsBackground = false; tabsScroll.autohidesScrollers = true
        tabsScroll.scrollerStyle = .overlay; tabsScroll.horizontalScrollElasticity = .none
        root.place = { [weak self] in self?.layout() }
        buildSidebar(); buildHome(); safely { try self.refresh() }; rebuildTabs()
        window.center(); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
        layout()
        focusMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] event in
            guard let self, event.window === window, selected != nil else { return event }
            let point = content.convert(event.locationInWindow, from: nil)
            if let session = sessions.first(where: { self.panes.contains($0.id) && $0.id != self.selected && $0.view.frame.contains(point) }) {
                selected = session.id; rebuildTabs()
            }
            return event
        }
        if let index = CommandLine.arguments.firstIndex(of: "--smoke-test"), CommandLine.arguments.count > index + 1 {
            smoke = SmokeTest(workspace: self, output: URL(fileURLWithPath: CommandLine.arguments[index + 1])); smoke?.start()
        }
    }
    func safely(_ body: () throws -> Void) {
        do { try body() } catch { _ = Theme.alert("wShell", error.localizedDescription).runModal() }
    }
    func buildSidebar() {
        sidebarViews = [Theme.label("wShell", size: 24, color: Theme.bright, bold: true),
            Theme.label("YOUR PERSONAL WORKSPACE", size: 11, color: Theme.muted), search,
            ActionButton("+ New host", accent: true) { [weak self] in self?.edit(nil) },
            Theme.label("SAVED HOSTS", size: 11, color: Theme.muted)]
        sidebarViews.forEach { sidebar.addSubview($0) }
        search.delegate = self
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("host")); column.width = 212
        table.addTableColumn(column); table.headerView = nil; table.backgroundColor = Theme.panel
        table.rowHeight = 51; table.delegate = self; table.dataSource = self; table.target = self; table.doubleAction = #selector(connectSelected)
        table.selectionHighlightStyle = .regular; table.intercellSpacing = NSSize(width: 0, height: 5)
        scroll.documentView = table; scroll.hasVerticalScroller = true; scroll.drawsBackground = false
        scroll.autohidesScrollers = true; scroll.scrollerStyle = .overlay; sidebar.addSubview(scroll)
        let actions = [ActionButton("Connect", accent: true) { [weak self] in self?.connectSelected() },
            ActionButton("Connection settings") { [weak self] in guard let self else { return }; self.edit(self.selectedHost) },
            ActionButton("Tools") { [weak self] in self?.tools() }, ActionButton("About") { [weak self] in self?.about() },
            ActionButton("SFTP") { [weak self] in guard let self, let host = self.selectedHost ?? self.active?.host else { return }; self.safely { try self.openSFTP(host) } }]
        sidebarViews += actions; actions.forEach { sidebar.addSubview($0) }
    }
    func buildHome() {
        homeViews = [Theme.label("LESS FRICTION. MORE FLOW.", size: 11, color: Theme.orange, bold: true),
            Theme.label("Your servers. One quiet workspace.", size: 24, color: Theme.bright, bold: true),
            Theme.label("A familiar terminal, with room for every connection.", color: Theme.muted), quick,
            ActionButton("Connect →", accent: true) { [weak self] in guard let self else { return }; self.safely { try self.openSSH(Record.quick(self.quick.stringValue)) } },
            Theme.label("QUICK CONNECT   user@hostname:22 or ssh://user@[::1]:22", size: 11, color: Theme.muted)]
        func card(_ title: String, _ description: String, _ button: String, action: @escaping () -> Void) -> Canvas {
            let view = Canvas(); view.wantsLayer = true; view.layer?.backgroundColor = Theme.panel.cgColor; view.layer?.cornerRadius = 10
            let heading = Theme.label(title, size: 15, color: Theme.bright, bold: true), detail = Theme.label(description, color: Theme.muted), control = ActionButton(button, action: action)
            [heading, detail, control].forEach { view.addSubview($0) }
            view.place = { [weak view] in guard let view else { return }; heading.frame = NSRect(x: 20, y: 19, width: view.bounds.width - 40, height: 23)
                detail.frame = NSRect(x: 20, y: 55, width: view.bounds.width - 40, height: 23); control.frame = NSRect(x: 20, y: 88, width: view.bounds.width - 40, height: 38) }
            return view
        }
        homeViews += [card("A home for every host", "Save once. Open in a new tab.", "+ Add a host") { [weak self] in self?.edit(nil) },
            card("Made for the command line", "Warm colors. Sharp type. Full color.", "Color preview →") { [weak self] in self?.openPreview() },
            card("Local terminal", "Your Mac. Your login shell.", "Open Terminal   ⌘⇧L") { [weak self] in self?.openLocal() },
            Theme.label("SSH / Local shell  ·  One workspace, independent tabs", size: 13, color: Theme.muted)]
        homeViews.forEach { home.addSubview($0) }
    }
    func layout() {
        let width = root.bounds.width, height = root.bounds.height, side: CGFloat = 242
        sidebar.frame = NSRect(x: 0, y: 0, width: side, height: height - 30)
        let positions: [NSRect] = [NSRect(x: 22,y: 23,width: 200,height: 34), NSRect(x: 22,y: 66,width: 200,height: 20),
            NSRect(x: 18,y: 99,width: 206,height: 32), NSRect(x: 18,y: 147,width: 206,height: 36), NSRect(x: 20,y: 199,width: 202,height: 20),
            NSRect(x: 18,y: height-189,width: 126,height: 35), NSRect(x: 18,y: height-142,width: 206,height: 35),
            NSRect(x: 18,y: height-95,width: 128,height: 35), NSRect(x: 156,y: height-95,width: 68,height: 35),
            NSRect(x: 154,y: height-189,width: 70,height: 35)]
        for (view, frame) in zip(sidebarViews, positions) { view.frame = frame }
        scroll.frame = NSRect(x: 14,y: 232,width: 214,height: max(60,height-437))
        tabsScroll.frame = NSRect(x: side + 1,y: 0,width: width-side-109,height: 53)
        splitButton.frame = NSRect(x: width-100,y: 8,width: 90,height: 32)
        splitButton.isEnabled = selected != nil
        content.frame = NSRect(x: side + 1,y: 54,width: width-side-1,height: height-84)
        home.frame = content.bounds
        let span = home.bounds.width - 80, card = (span - 16) / 2
        let homeFrames: [NSRect] = [NSRect(x: 40,y: 35,width: span,height: 23), NSRect(x: 40,y: 80,width: span,height: 38),
            NSRect(x: 40,y: 129,width: span,height: 25), NSRect(x: 40,y: 182,width: span-132,height: 38),
            NSRect(x: 40+span-120,y: 179,width: 120,height: 43), NSRect(x: 40,y: 238,width: span,height: 25),
            NSRect(x: 40,y: 290,width: card,height: 145), NSRect(x: 56+card,y: 290,width: card,height: 145),
            NSRect(x: 40,y: 454,width: span,height: 145), NSRect(x: 40,y: 623,width: span,height: 23)]
        for (view, frame) in zip(homeViews, homeFrames) { view.frame = frame }
        homeViews.last?.isHidden = home.bounds.height < 660
        status.frame = NSRect(x: 18,y: height-26,width: width-36,height: 23)
        panes.removeAll { id in !sessions.contains { $0.id == id } }
        if let selected {
            if splitCount == 1 { panes = [selected] }
            if panes.isEmpty { panes = [selected] }
            for session in sessions where panes.count < splitCount && !panes.contains(session.id) { panes.append(session.id) }
        }
        let frames = SplitLayout.frames(count: panes.count, in: content.bounds)
        for session in sessions {
            let index = panes.firstIndex(of: session.id)
            session.view.isHidden = selected == nil || index == nil
            paneHeaders[session.id]?.isHidden = selected == nil || index == nil || panes.count == 1
            guard let index else { continue }
            let frame = frames[index]
            if panes.count > 1 {
                paneHeaders[session.id]?.frame = NSRect(x: frame.minX,y: frame.minY,width: frame.width,height: 26)
                paneHeaders[session.id]?.accent = selected == session.id
                paneHeaders[session.id]?.needsDisplay = true
                session.view.frame = NSRect(x:frame.minX+2,y:frame.minY+28,width:frame.width-4,height:max(10,frame.height-30))
            } else { session.view.frame = session.files == nil ? frame.insetBy(dx: 10, dy: 8) : frame }
        }
        tabBar.frame = NSRect(x: 0,y: 0,width: max(tabsScroll.contentSize.width, CGFloat(tabButtons.count)*184+12),height: 42)
        for (index, button) in tabButtons.enumerated() { button.frame = NSRect(x: 8+CGFloat(index)*184,y: 7,width: 176,height: 32) }
    }
    func refresh() throws {
        allHosts = try store.read().filter { $0.category == "sessions" && $0.name != "Default Settings" }.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        let filter = search.stringValue.lowercased()
        hosts = allHosts.filter { filter.isEmpty || [$0.name,$0["HostName"],$0["UserName"],$0["WookGroup"]].joined(separator: " ").lowercased().contains(filter) }
        table.reloadData()
    }
    var selectedHost: Record? { hosts.indices.contains(table.selectedRow) ? hosts[table.selectedRow] : nil }
    func controlTextDidChange(_ obj: Notification) { safely { try refresh() } }
    func numberOfRows(in tableView: NSTableView) -> Int { hosts.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let view = Canvas(frame: NSRect(x: 0,y: 0,width: 210,height: 51)), host = hosts[row]
        let name = Theme.label(host.name, bold: true), address = Theme.label(host["HostName"], size: 11, color: Theme.muted)
        name.frame = NSRect(x: 8,y: 5,width: 194,height: 22); address.frame = NSRect(x: 8,y: 29,width: 194,height: 18)
        view.addSubview(name); view.addSubview(address); return view
    }
    @objc func connectSelected() { if let host = selectedHost { safely { try openSSH(host) } } }
    func edit(_ host: Record?) { safely { if try editHost(host, store: store) { try refresh() } } }
    func rebuildTabs() {
        tabButtons.forEach { $0.removeFromSuperview() }
        tabButtons = [ActionButton("Workspace", accent: selected == nil) { [weak self] in self?.select(nil) }]
        for session in sessions {
            let button = ActionButton((session.ended ? "○ " : "") + String(session.title.prefix(18)), accent: selected == session.id) { [weak self, weak session] in self?.select(session?.id) }
            button.toolTip = session.title + " · ⌘W close · ⌘⇧D duplicate"
            let menu = NSMenu(); menu.addItem(withTitle: "Close tab", action: #selector(closeMenuTab(_:)), keyEquivalent: "").representedObject = session.id.uuidString
            menu.items.forEach { $0.target = self }; button.menu = menu; tabButtons.append(button)
        }
        tabButtons.append(ActionButton("+") { [weak self] in self?.select(nil); self?.window.makeFirstResponder(self?.quick) })
        tabButtons.forEach { tabBar.addSubview($0) }; layout()
    }
    func select(_ id: UUID?) {
        if let id, splitCount > 1, !panes.contains(id) {
            if let index = panes.firstIndex(where: { $0 == selected }) { panes[index] = id }
            else if !panes.isEmpty { panes[0] = id }
        }
        selected = id; home.isHidden = id != nil
        rebuildTabs()
        if let session = active { window.makeFirstResponder(session.files?.tables[1] ?? session.terminal) } else { window.makeFirstResponder(quick) }
    }
    var active: Session? { sessions.first { $0.id == selected } }
    func setSplit(_ count: Int) {
        guard let selected, (1...4).contains(count), sessions.count >= count else { return }
        splitCount = count; panes = [selected]
        for session in sessions where panes.count < count && session.id != selected { panes.append(session.id) }
        layout()
    }
    func splitMenu() {
        let menu = NSMenu()
        for (index, title) in ["Single pane", "2 panes — side by side", "3 panes — left + stacked right", "4 panes — grid"].enumerated() {
            let item = NSMenuItem(title: title, action: #selector(menuAction(_:)), keyEquivalent: "")
            item.target = self; item.representedObject = "split-\(index+1)"; item.state = splitCount == index+1 ? .on : .off
            item.isEnabled = sessions.count >= index+1 && selected != nil; menu.addItem(item)
        }
        menu.autoenablesItems = false; menu.popUp(positioning: nil, at: NSPoint(x:0,y:splitButton.bounds.height), in: splitButton)
    }
    func attach(_ session: Session) throws {
        guard sessions.count < 32 else { throw WShellError("Close a tab before opening more than 32 sessions.") }
        sessions.append(session); content.addSubview(session.view)
        let header = ActionButton(session.title) { [weak self, weak session] in self?.select(session?.id) }
        paneHeaders[session.id] = header; content.addSubview(header)
        session.terminal.processDelegate = self; select(session.id)
    }
    func environment() -> [String: String] {
        var env = ProcessInfo.processInfo.environment
        env["TERM"] = "xterm-256color"; env["COLORTERM"] = "truecolor"; env["TERM_PROGRAM"] = "wShell"
        env["SSH_AUTH_SOCK"] = nil; env["SSH_AGENT_PID"] = nil
        return env
    }
    func openLocal() {
        safely {
            let session = Session(kind: .local, title: "Local terminal")
            try attach(session)
            let entry = getpwuid(getuid())
            let shell = entry.map { String(cString: $0.pointee.pw_shell) } ?? "/bin/zsh"
            session.terminal.startProcess(executable: FileManager.default.isExecutableFile(atPath: shell) ? shell : "/bin/zsh",
                args: smoke == nil ? ["-l"] : ["-f"], environment: environment().map { "\($0.key)=\($0.value)" },
                currentDirectory: smoke?.output.path ?? FileManager.default.homeDirectoryForCurrentUser.path)
        }
    }
    func prepareSSH(_ host: Record, sftp: Bool = false) throws -> (args: [String], env: [String: String], attempt: URL?)? {
        guard sessions.count < 32 else { throw WShellError("Close a tab before opening more than 32 sessions.") }
        guard try HostTrust.ensure(host, file: store.knownHosts, confirm: { fingerprints in
            let alert = Theme.alert("Verify SSH host key", "\(host["HostName"]):\(host.port)\n\nCompare these fingerprints with your server administrator before trusting this host.\n\n" + fingerprints.joined(separator: "\n\n"))
            alert.addButton(withTitle: "Trust host"); alert.addButton(withTitle: "Cancel")
            return alert.runModal() == .alertFirstButtonReturn
        }) else { return nil }
        let saved = CredentialStore().contains(host.credentialID)
        let args = try SSHCommand.arguments(for: host, knownHosts: store.knownHosts, savedPassword: saved, sftp: sftp)
        var attempt: URL?
        var env = environment()
        env["SSH_ASKPASS"] = Bundle.main.executableURL!.path; env["SSH_ASKPASS_REQUIRE"] = "force"; env["WSHELL_ASKPASS"] = "1"
        env["WOOK_DATA_DIR"] = store.root.path
        if saved {
            let nonce = UUID().uuidString
            env["WSHELL_PASSWORD_HOST"] = host.name; env["WSHELL_PASSWORD_SCOPE"] = host.credentialID
            env["WSHELL_PASSWORD_ATTEMPT"] = nonce; attempt = store.root.appendingPathComponent("attempt-" + nonce)
        }
        return (args, env, attempt)
    }
    func openSSH(_ host: Record) throws {
        guard let config = try prepareSSH(host) else { return }
        let session = Session(kind: .ssh, host: host, title: host.name); session.attempt = config.attempt; try attach(session)
        session.terminal.startProcess(executable: "/usr/bin/ssh", args: config.args, environment: config.env.map { "\($0.key)=\($0.value)" }, currentDirectory: FileManager.default.homeDirectoryForCurrentUser.path)
    }
    func openSFTP(_ host: Record) throws {
        guard let config = try prepareSSH(host, sftp: true) else { return }
        let client = try SftpClient(arguments: config.args, environment: config.env, attempt: config.attempt)
        let session = Session(kind: .sftp, host: host, title: "SFTP · " + host.name)
        session.files = SftpBrowser(client: client); try attach(session)
    }
    func openPreview() {
        safely {
            let session = Session(kind: .preview, title: "Color preview"); try attach(session)
            session.terminal.feed(text: "\r\n  \u{1b}[1mwShell · Made for the command line\u{1b}[0m\r\n\r\n  Flexoki Dark / JetBrains Mono\r\n  UTF-8: 안녕하세요  →  café\r\n\r\n")
            for index in 0..<16 { session.terminal.feed(text: "\u{1b}[48;5;\(index)m   \u{1b}[0m") }
            session.terminal.feed(text: "\r\n\r\n  \u{1b}[38;2;218;112;44m24-bit True Color · wShell\u{1b}[0m\r\n")
        }
    }
    func close(_ session: Session, confirm: Bool = true) {
        if confirm && session.running {
            let alert = Theme.alert("Close tab?", "Commands may still be running."); alert.addButton(withTitle: "Close tab"); alert.addButton(withTitle: "Cancel")
            if alert.runModal() != .alertFirstButtonReturn { return }
        }
        session.stop(); session.view.removeFromSuperview(); sessions.removeAll { $0.id == session.id }
        paneHeaders.removeValue(forKey: session.id)?.removeFromSuperview(); panes.removeAll { $0 == session.id }
        select(selected == session.id ? panes.first ?? sessions.last?.id : selected)
    }
    @objc func closeMenuTab(_ sender: NSMenuItem) { if let id = sender.representedObject as? String, let session = sessions.first(where: { $0.id.uuidString == id }) { close(session) } }
    func duplicate() {
        guard let session = active else { return }
        switch session.kind { case .local: openLocal(); case .ssh: safely { try openSSH(session.host!) }; case .sftp: safely { try openSFTP(session.host!) }; case .preview: openPreview(); case .keygen: break }
    }
    func restart() { guard let session = active else { return }; duplicate(); close(session, confirm: false) }
    func moveTab(_ offset: Int) {
        guard let index = sessions.firstIndex(where: { $0.id == selected }), sessions.indices.contains(index + offset) else { return }
        sessions.swapAt(index,index+offset); rebuildTabs()
    }
    func processTerminated(source: TerminalView, exitCode: Int32?) {
        if let session = sessions.first(where: { $0.terminal === source }) {
            session.ended = true; session.terminal.feed(text: "\r\n\u{1b}[38;2;218;112;44mSession ended (\(exitCode ?? 0)). Use Reconnect to start again.\u{1b}[0m\r\n")
            rebuildTabs()
        }
    }
    func setTerminalTitle(source: LocalProcessTerminalView, title: String) {}
    func sizeChanged(source: LocalProcessTerminalView, newCols: Int, newRows: Int) {}
    func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) {}
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if sessions.contains(where: { $0.running }) {
            let alert = Theme.alert("Close wShell?", "All active terminals will be closed."); alert.addButton(withTitle: "Close workspace"); alert.addButton(withTitle: "Cancel")
            if alert.runModal() != .alertFirstButtonReturn { return false }
        }
        sessions.forEach { $0.stop() }; return true
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply { windowShouldClose(window) ? .terminateNow : .terminateCancel }
}
