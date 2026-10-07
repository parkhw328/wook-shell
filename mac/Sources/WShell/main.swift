import AppKit
import WShellCore

if ProcessInfo.processInfo.environment["WSHELL_ASKPASS"] == "1" { runAskpass() }
let application = NSApplication.shared
application.setActivationPolicy(.regular)
do {
    let delegate = Workspace(store: try SettingsStore())
    application.delegate = delegate
    withExtendedLifetime(delegate) { application.run() }
} catch { _ = Theme.alert("wShell", error.localizedDescription).runModal(); exit(1) }
