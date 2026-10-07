import AppKit
import CoreText

enum Theme {
    static func color(_ hex: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255,
                blue: CGFloat(hex & 255) / 255, alpha: 1)
    }
    static let background = color(0x100f0f), panel = color(0x1c1b1a), raised = color(0x282726)
    static let border = color(0x343331), text = color(0xcecdc3), bright = color(0xfffcf0), muted = color(0x878580)
    static let orange = color(0xda702c)
    static func loadFonts() {
        for name in ["JetBrainsMono-Regular", "JetBrainsMono-Bold"] {
            if let url = Bundle.main.url(forResource: name, withExtension: "ttf") {
                CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
            }
        }
    }
    static func font(_ size: CGFloat = 13, bold: Bool = false) -> NSFont {
        NSFont(name: bold ? "JetBrainsMono-Bold" : "JetBrainsMono-Regular", size: size)
            ?? NSFont.monospacedSystemFont(ofSize: size, weight: bold ? .bold : .regular)
    }
    static func label(_ text: String, size: CGFloat = 13, color: NSColor = Theme.text, bold: Bool = false) -> NSTextField {
        let field = NSTextField(labelWithString: text)
        field.font = font(size, bold: bold); field.textColor = color; field.lineBreakMode = .byTruncatingTail
        return field
    }
    static func input(_ placeholder: String = "") -> NSTextField {
        let field = NSTextField(); field.font = font(); field.textColor = text; field.backgroundColor = raised
        field.placeholderString = placeholder; field.focusRingType = .none
        field.isBezeled = false; field.wantsLayer = true; field.layer?.cornerRadius = 5
        return field
    }
    static func alert(_ title: String, _ message: String) -> NSAlert {
        let alert = NSAlert(); alert.messageText = title; alert.informativeText = message
        alert.window.appearance = NSAppearance(named: .darkAqua)
        return alert
    }
}

final class ActionButton: NSButton {
    var invoke: (() -> Void)?
    var accent = false
    init(_ title: String, accent: Bool = false, action: @escaping () -> Void) {
        self.accent = accent; self.invoke = action
        super.init(frame: .zero)
        self.title = title; target = self; self.action = #selector(performAction)
        isBordered = false; font = Theme.font(); setButtonType(.momentaryPushIn)
        setAccessibilityLabel(title)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    @objc private func performAction() { invoke?() }
    override func draw(_ dirtyRect: NSRect) {
        let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 6, yRadius: 6)
        (isHighlighted ? Theme.border : accent ? Theme.orange : Theme.raised).setFill(); path.fill()
        Theme.border.setStroke(); path.stroke()
        let attributes: [NSAttributedString.Key: Any] = [.font: Theme.font(), .foregroundColor: !isEnabled ? Theme.muted : accent ? Theme.background : Theme.text]
        let size = (title as NSString).size(withAttributes: attributes)
        (title as NSString).draw(at: NSPoint(x: (bounds.width - size.width) / 2, y: (bounds.height - size.height) / 2), withAttributes: attributes)
    }
}

final class Canvas: NSView {
    var place: (() -> Void)?
    override var isFlipped: Bool { true }
    override func layout() { super.layout(); place?() }
}
