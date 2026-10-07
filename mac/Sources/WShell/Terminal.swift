import AppKit
import SwiftTerm
import WShellCore

final class SessionTerminal: LocalProcessTerminalView {
    var output: ((String) -> Void)?
    var bridge: TerminalBridge!
    override func dataReceived(slice: ArraySlice<UInt8>) {
        super.dataReceived(slice: slice)
        output?(String(decoding: slice, as: UTF8.self))
    }
    func configure(size: CGFloat) {
        font = Theme.font(size)
        nativeBackgroundColor = Theme.background; nativeForegroundColor = Theme.text
        caretColor = Theme.orange; selectedTextBackgroundColor = Theme.border
        optionAsMetaKey = true
        let palette: [UInt32] = [0x100f0f,0xaf3029,0x66800b,0xad8301,0x205ea6,0xa02f6f,0x24837b,0xcecdc3,
                                 0x575653,0xd14d41,0x879a39,0xd0a215,0x4385be,0xce5d97,0x3aa99f,0xfffcf0]
        installColors(palette.map { Color(red8: UInt16(($0 >> 16) & 255), green8: UInt16(($0 >> 8) & 255), blue8: UInt16($0 & 255)) })
        bridge = TerminalBridge(self); terminalDelegate = bridge
    }
    func stop() {
        let pid = process.shellPid
        if process.running && pid > 0 { kill(-pid, SIGHUP); terminate() }
    }
}

// Keep terminal-driven clipboard reads disabled; keyboard Copy/Paste still uses AppKit.
final class TerminalBridge: TerminalViewDelegate {
    weak var view: SessionTerminal?
    init(_ view: SessionTerminal) { self.view = view }
    func sizeChanged(source: TerminalView, newCols: Int, newRows: Int) { view?.sizeChanged(source: source, newCols: newCols, newRows: newRows) }
    func setTerminalTitle(source: TerminalView, title: String) { view?.setTerminalTitle(source: source, title: title) }
    func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) { view?.hostCurrentDirectoryUpdate(source: source, directory: directory) }
    func send(source: TerminalView, data: ArraySlice<UInt8>) { view?.send(source: source, data: data) }
    func scrolled(source: TerminalView, position: Double) {}
    func rangeChanged(source: TerminalView, startY: Int, endY: Int) {}
    func requestOpenLink(source: TerminalView, link: String, params: [String: String]) {
        if let url = URL(string: link), ["http", "https"].contains(url.scheme?.lowercased() ?? "") { NSWorkspace.shared.open(url) }
    }
    func bell(source: TerminalView) { NSSound.beep() }
    func clipboardCopy(source: TerminalView, content: Data) {}
    func clipboardRead(source: TerminalView) -> Data? { nil }
}

final class Session {
    enum Kind { case local, ssh, preview, keygen }
    let id = UUID(), kind: Kind, host: Record?, terminal: SessionTerminal
    var title: String, ended = false, attempt: URL?
    init(kind: Kind, host: Record? = nil, title: String) {
        self.kind = kind; self.host = host; self.title = title
        terminal = SessionTerminal(frame: NSRect(x: 0, y: 0, width: 800, height: 550))
        terminal.configure(size: CGFloat(host?.fontSize ?? 13))
    }
    deinit { terminal.stop(); if let attempt { try? FileManager.default.removeItem(at: attempt) } }
}
