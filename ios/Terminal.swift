import SwiftUI
import SwiftTerm

enum Palette {
    static func ui(_ hex: UInt32) -> UIColor {
        UIColor(red: CGFloat((hex >> 16) & 255)/255, green: CGFloat((hex >> 8) & 255)/255, blue: CGFloat(hex & 255)/255, alpha: 1)
    }
    static let background = SwiftUI.Color(ui(0x100f0f)), panel = SwiftUI.Color(ui(0x1c1b1a)), text = SwiftUI.Color(ui(0xcecdc3)), muted = SwiftUI.Color(ui(0x878580)), orange = SwiftUI.Color(ui(0xda702c))
    static func font(_ size: CGFloat = 14, bold: Bool = false) -> Font { .custom(bold ? "JetBrainsMono-Bold" : "JetBrainsMono-Regular", size: size) }
}
final class TerminalPane: TerminalView, TerminalViewDelegate {
    weak var connection: Connection?
    private var control = false
    init(_ connection: Connection) {
        self.connection = connection
        super.init(frame: .zero, font: UIFont(name: "JetBrainsMono-Regular", size: 14))
        terminalDelegate = self
        nativeBackgroundColor = Palette.ui(0x100f0f); nativeForegroundColor = Palette.ui(0xcecdc3)
        caretColor = Palette.ui(0xda702c)
        let colors: [UInt32] = [0x100f0f,0xaf3029,0x66800b,0xad8301,0x205ea6,0xa02f6f,0x24837b,0xcecdc3,0x575653,0xd14d41,0x879a39,0xd0a215,0x4385be,0xce5d97,0x3aa99f,0xfffcf0]
        installColors(colors.map { SwiftTerm.Color(red8: UInt16(($0 >> 16) & 255), green8: UInt16(($0 >> 8) & 255), blue8: UInt16($0 & 255)) })
        let bar = UIToolbar(frame: CGRect(x: 0, y: 0, width: 600, height: 44))
        bar.barTintColor = Palette.ui(0x1c1b1a); bar.tintColor = Palette.ui(0xda702c)
        let keys = [("Esc", "\u{1b}"), ("Tab", "\t"), ("Ctrl", ""), ("↑", "\u{1b}[A"), ("↓", "\u{1b}[B"), ("←", "\u{1b}[D"), ("→", "\u{1b}[C"), ("Hide", "")]
        bar.items = keys.map { title, bytes in
            UIBarButtonItem(title: title, primaryAction: UIAction { [weak self] action in
                guard let self else { return }
                if title == "Ctrl" { control.toggle(); (action.sender as? UIBarButtonItem)?.title = control ? "Ctrl ●" : "Ctrl" }
                else if title == "Hide" { resignFirstResponder() }
                else { connection.send(Data(bytes.utf8)) }
            })
        }
        inputAccessoryView = bar
        connection.output = { [weak self] data in self?.feed(byteArray: Array(data)[...]) }
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) unavailable") }
    func send(source: TerminalView, data: ArraySlice<UInt8>) {
        var bytes = Array(data)
        if control, bytes.count == 1, bytes[0] >= 64, bytes[0] <= 127 { bytes[0] &= 31; control = false }
        connection?.send(Data(bytes))
    }
    func sizeChanged(source: TerminalView, newCols: Int, newRows: Int) { connection?.resize(newCols, newRows) }
    func setTerminalTitle(source: TerminalView, title: String) {}
    func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) {}
    func scrolled(source: TerminalView, position: Double) {}
    func rangeChanged(source: TerminalView, startY: Int, endY: Int) {}
    func clipboardCopy(source: TerminalView, content: Data) {}
    func clipboardRead(source: TerminalView) -> Data? { nil }
    func requestOpenLink(source: TerminalView, link: String, params: [String:String]) {}
}
struct TerminalSurface: UIViewRepresentable {
    let connection: Connection
    func makeUIView(context: Context) -> TerminalPane {
        if let terminal = connection.terminal { return terminal }
        let terminal = TerminalPane(connection); connection.terminal = terminal; return terminal
    }
    func updateUIView(_ uiView: TerminalPane, context: Context) {}
}
