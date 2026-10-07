import AppKit
import WShellCore

final class SmokeTest {
    unowned let workspace: Workspace
    let output: URL
    var timer: Timer?, phase = 0, ticks = 0, checks = 0
    var received = ""
    init(workspace: Workspace, output: URL) { self.workspace = workspace; self.output = output }
    func capture(_ name: String) throws {
        let view = workspace.root
        view.layoutSubtreeIfNeeded(); view.displayIfNeeded()
        workspace.active?.terminal.displayIfNeeded()
        CATransaction.flush()
        guard let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { throw WShellError("Cannot capture the native window.") }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        try bitmap.representation(using: .png, properties: [:])!.write(to: output.appendingPathComponent(name + "-view.png"))
        // Layer-backed terminal drawing is not included by NSView bitmap caching.
        // Capture this fixture window through WindowServer as well.
        let capture = Process(); capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
        capture.arguments = ["-x", "-l", String(workspace.window.windowNumber), output.appendingPathComponent(name + ".png").path]
        try capture.run(); capture.waitUntilExit()
        try require(capture.terminationStatus == 0, "Cannot capture the rendered fixture window.")
    }
    func start() {
        do { try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true); try capture("mac-home") }
        catch { finish(error.localizedDescription); return }
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in self?.tick() }
    }
    func require(_ condition: Bool, _ message: String) throws { if !condition { throw WShellError(message) }; checks += 1 }
    func advance(_ next: Int) { phase = next; ticks = 0; received = "" }
    func observe() { workspace.active?.terminal.output = { [weak self] text in self?.received += text } }
    func tick() {
        ticks += 1
        do {
            if ticks > 240 { throw WShellError("Timed out in macOS smoke phase \(phase).") }
            switch phase {
            case 0:
                try require(Theme.font().fontName.contains("JetBrains"), "Embedded font is missing.")
                workspace.openLocal(); observe(); advance(1)
            case 1 where ticks == 8:
                workspace.active?.terminal.send(txt: "printf '\\033[38;2;218;112;44mLocal terminal · 안녕하세요\\033[0m\\n'; printf local-ok > local.ready\r")
            case 1 where FileManager.default.fileExists(atPath: output.appendingPathComponent("local.ready").path):
                try require(try String(contentsOf: output.appendingPathComponent("local.ready"), encoding: .utf8) == "local-ok", "Local shell did not execute input.")
                try capture("mac-local"); workspace.duplicate(); advance(2)
            case 2 where ticks == 8:
                try require(workspace.sessions.count == 2 && workspace.sessions[0].terminal.process.shellPid != workspace.sessions[1].terminal.process.shellPid, "Local tabs must use separate PTYs.")
                workspace.active?.terminal.send(txt: "printf duplicate-ok > duplicate.ready\r")
            case 2 where FileManager.default.fileExists(atPath: output.appendingPathComponent("duplicate.ready").path):
                workspace.window.setContentSize(NSSize(width: 1120,height: 740)); workspace.layout()
                try require(workspace.active!.terminal.terminal.cols > 50, "Terminal resize failed.")
                workspace.active?.terminal.send(txt: "exit\r"); advance(3)
            case 3 where workspace.active?.ended == true:
                try require(true, "Shell exit observed.")
                for session in workspace.sessions { workspace.close(session, confirm: false) }
                workspace.openPreview(); try capture("mac-colors"); workspace.close(workspace.active!, confirm: false)
                let env = ProcessInfo.processInfo.environment
                var host = Record(name: "Loopback key fixture", fields: ["HostName":"127.0.0.1", "UserName":"key", "PortNumber":env["WSHELL_TEST_PORT"] ?? "0", "Protocol":"ssh", "PublicKeyFile":env["WSHELL_TEST_KEY"] ?? ""])
                let testTrust = output.appendingPathComponent("trust-fixture")
                let rejected = try HostTrust.ensure(host, file: testTrust) { _ in false }
                try require(!rejected && !FileManager.default.fileExists(atPath: testTrust.path), "Rejected host keys must not be saved.")
                var matchingFingerprint = false
                let accepted = try HostTrust.ensure(host, file: testTrust) { fingerprints in
                    matchingFingerprint = fingerprints.contains { $0.contains(env["WSHELL_TEST_HOST_SHA256"] ?? "missing") }
                    return matchingFingerprint
                }
                try require(accepted && matchingFingerprint, "Host-key scan must match the fixture's independently pinned fingerprint.")
                try require(try HostTrust.isKnown(host, file: testTrust), "Confirmed host trust must persist.")
                try workspace.store.save(host); try workspace.refresh(); try workspace.openSSH(host); observe(); advance(4)
                host.name = "Loopback password fixture"; host["UserName"] = "password"; host["PublicKeyFile"] = ""
                try workspace.store.save(host)
                try CredentialStore().save(host.credentialID, password: Data((env["WSHELL_TEST_PASSWORD"] ?? "").utf8))
            case 4 where received.contains("WSHELL_SSH_READY"):
                try require(workspace.active?.terminal.process.running == true, "SSH PTY is not running.")
                try require(String(decoding: workspace.active!.terminal.terminal.getBufferAsData(), as: UTF8.self).contains("WSHELL_SSH_READY"), "SSH output did not reach the terminal screen buffer.")
                workspace.active?.terminal.send(txt: "key-input\r"); try capture("mac-ssh")
                let terminal = workspace.active!.terminal
                let noRange = NSRange(location: NSNotFound, length: 0)
                terminal.send(txt: "|ime:")
                for syllable in ["ㅎ", "하", "한"] {
                    terminal.setMarkedText(syllable, selectedRange: NSRange(location: 1, length: 0), replacementRange: noRange)
                }
                try require(terminal.hasMarkedText() && terminal.selectedRange() == NSRange(location: 1, length: 0),
                            "IME selection must be relative to the composition, not the remote screen.")
                try capture("mac-ime-composition")
                terminal.insertText(NSAttributedString(string: "한글🙂"), replacementRange: noRange)
                try require(!terminal.hasMarkedText(), "Attributed IME commit must clear composition.")
                terminal.setMarkedText("취소", selectedRange: NSRange(location: 2, length: 0), replacementRange: noRange)
                terminal.unmarkText(); terminal.send(txt: "|\r")
                try require(!terminal.hasMarkedText(), "Cancelled marked text must be removed.")
                workspace.window.setContentSize(NSSize(width: 1200,height: 780)); workspace.layout(); advance(5)
            case 5 where ticks > 8:
                workspace.close(workspace.active!, confirm: false)
                let host = try workspace.store.read().first { $0.name == "Loopback password fixture" }!
                try workspace.openSSH(host); observe(); advance(6)
            case 6 where received.contains("WSHELL_SSH_READY"):
                workspace.active?.terminal.send(txt: "password-input\r"); try capture("mac-password-ssh")
                try require(true, "Saved Keychain password authenticated through the askpass process.")
                let backup = try workspace.store.exportBackup()
                try require(!String(decoding: backup, as: UTF8.self).contains(ProcessInfo.processInfo.environment["WSHELL_TEST_PASSWORD"]!), "Password leaked into backup.")
                advance(7)
            case 7 where ticks > 8:
                workspace.close(workspace.active!, confirm: false)
                let host = try workspace.store.read().first { $0.name == "Loopback key fixture" }!
                try workspace.openSFTP(host); advance(8)
            case 8 where workspace.active?.files?.connected == true && workspace.active?.files?.busy == false:
                let files = workspace.active!.files!
                try Data("hidden fixture".utf8).write(to: output.appendingPathComponent("sftp-local/.hidden"))
                files.navigate(0, output.appendingPathComponent("sftp-local").path)
                try require(files.hiddenToggle.state == .off && files.visibleEntries[0].count == 2 && files.visibleEntries[1].count == 2, "SFTP hides local and remote dotfiles by default")
                files.hiddenToggle.state = .on; files.toggleHidden()
                try require(files.visibleEntries[0].count == 3 && files.visibleEntries[1].count == 4, "Checkbox reveals hidden files and folders in both panes")
                files.hiddenToggle.state = .off; files.toggleHidden()
                try require(files.visibleEntries[0].count == 2 && files.visibleEntries[1].count == 2, "Unchecking hides them immediately")
                files.tables[0].selectRowIndexes(IndexSet(integersIn: 0..<2), byExtendingSelection: false)
                try files.transfer(true); advance(9)
            case 9 where workspace.active?.files?.busy == false:
                let files = workspace.active!.files!
                try require(files.connected && files.entries[1].contains { $0.name == "upload-한글.bin" }, "Native SFTP upload failed: " + files.status.stringValue)
                try capture("mac-sftp")
                files.navigate(0, output.appendingPathComponent("sftp-download").path)
                let index = files.visibleEntries[1].firstIndex { $0.name == "upload-한글.bin" }!
                files.tables[1].selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
                try files.transfer(false); advance(10)
            case 10 where workspace.active?.files?.busy == false:
                let files = workspace.active!.files!
                try require(files.connected && FileManager.default.fileExists(atPath: output.appendingPathComponent("sftp-download/upload-한글.bin").path), "Native SFTP download failed: " + files.status.stringValue)
                workspace.close(workspace.active!, confirm: false)
                let host = try workspace.store.read().first { $0.name == "Loopback password fixture" }!
                try workspace.openSFTP(host); advance(11)
            case 11 where workspace.active?.files?.connected == true && workspace.active?.files?.busy == false:
                try require(true, "SFTP reuses the scoped Keychain password.")
                workspace.close(workspace.active!, confirm: false)
                for _ in 0..<4 { workspace.openLocal() }
                for count in 2...4 {
                    workspace.setSplit(count)
                    try require(workspace.panes.count == count, "Split must show \(count) panes")
                    let visible = workspace.sessions.filter { !$0.view.isHidden }
                    try require(visible.count == count, "Visible terminals match split count")
                    for (index, session) in visible.enumerated() {
                        try require(session.view.frame.width > 100 && session.view.frame.height > 80, "Usable pane geometry")
                        for other in visible.prefix(index) { try require(!session.view.frame.intersects(other.view.frame), "Panes must not overlap") }
                    }
                }
                for (index, session) in workspace.sessions.enumerated() {
                    session.host = Record(name:"Server \(index+1)", fields:["WookAlias":"Server \(index+1)", "WookTabColor":["d14d41","4385be","879a39","8b7ec8"][index]])
                    session.title = session.host!.displayName; workspace.paneHeaders[session.id]?.title = session.title
                    workspace.select(session.id)
                    session.terminal.send(txt: "export WSHELL_PANE=\(index); printf pane > split-\(index).ready\r")
                }
                advance(12)
            case 12 where (0..<4).allSatisfy({ FileManager.default.fileExists(atPath: output.appendingPathComponent("split-\($0).ready").path) }):
                try require(true, "Independent input in all four split PTYs")
                try capture("mac-split")
                let ids = workspace.panes
                workspace.moveTab(-1)
                try require(workspace.panes == ids, "Reordering tabs retains pane identities")
                workspace.setSplit(1)
                try require(workspace.sessions.filter { !$0.view.isHidden }.count == 1, "Single pane hides other sessions without closing")
                workspace.setSplit(4)
                try require(workspace.sendAll.state == .off && workspace.syncInput.state == .off,"Both broadcast modes default off")
                workspace.commandText.stringValue = "printf single > single-$WSHELL_PANE.ready"; workspace.sendCommand()
                advance(13)
            case 13 where FileManager.default.fileExists(atPath:output.appendingPathComponent("single-3.ready").path):
                try require((0..<3).allSatisfy { !FileManager.default.fileExists(atPath:output.appendingPathComponent("single-\($0).ready").path) },"Unchecked command targets active pane only")
                workspace.sendAll.state = .on; workspace.inputOptionsChanged()
                workspace.commandText.stringValue = "printf all > all-$WSHELL_PANE.ready"; workspace.sendCommand(); advance(14)
            case 14 where (0..<4).allSatisfy({FileManager.default.fileExists(atPath:output.appendingPathComponent("all-\($0).ready").path)}):
                try require(true,"Common command reaches all four panes")
                workspace.syncInput.state = .on; workspace.inputOptionsChanged()
                workspace.active!.terminal.insertText("printf live > live-$WSHELL_PANE.ready\n", replacementRange:NSRange(location:NSNotFound,length:0))
                advance(15)
            case 15 where (0..<4).allSatisfy({FileManager.default.fileExists(atPath:output.appendingPathComponent("live-\($0).ready").path)}):
                try require(true,"Committed keyboard text reaches all four panes")
                for index in 0..<4 { try require(try String(contentsOf:output.appendingPathComponent("live-\(index).ready"),encoding:.utf8) == "live","Broadcast delivers exact text once") }
                try capture("mac-broadcast")
                workspace.setSplit(2)
                try require(workspace.sendAll.state == .off && workspace.syncInput.state == .off,"Changing panes disables broadcast")
                workspace.setSplit(4); workspace.close(workspace.active!, confirm: false)
                try require(workspace.panes.count == 3 && workspace.sessions.allSatisfy { !$0.view.isHidden }, "Closing split pane keeps other sessions visible")
                finish(nil)
            default: break
            }
        } catch { finish(error.localizedDescription) }
    }
    func finish(_ error: String?) {
        timer?.invalidate()
        for session in workspace.sessions { workspace.close(session, confirm: false) }
        if let records = try? workspace.store.read() { for record in records { try? CredentialStore().remove(record.credentialID) } }
        let result: [String: Any] = ["passed":error == nil,"checks":checks,"phase":phase,"error":error ?? ""]
        if let data = try? JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted,.sortedKeys]) { try? data.write(to: output.appendingPathComponent("result.json")) }
        exit(error == nil ? 0 : 1)
    }
}
