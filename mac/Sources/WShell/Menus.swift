import AppKit
import WShellCore

extension Workspace {
    func buildMenu() {
        let menu = NSMenu(), app = NSMenu(), file = NSMenu(), edit = NSMenu(), tabs = NSMenu()
        for (title, submenu) in [("wShell", app), ("File", file), ("Edit", edit), ("Tabs", tabs)] {
            let item = NSMenuItem(title: title, action: nil, keyEquivalent: ""); item.submenu = submenu; menu.addItem(item)
        }
        func item(_ menu: NSMenu, _ name: String, _ action: String, _ key: String = "", shift: Bool = false) {
            let entry = NSMenuItem(title: name, action: #selector(menuAction(_:)), keyEquivalent: key)
            entry.representedObject = action; entry.target = self; entry.keyEquivalentModifierMask = shift ? [.command,.shift] : [.command]; menu.addItem(entry)
        }
        item(app,"About wShell","about"); app.addItem(.separator()); item(app,"Quit wShell","quit","q")
        item(file,"New connection","new","t",shift: true); item(file,"Local terminal","local","l",shift: true)
        item(file,"New host","host","n"); item(file,"Connection settings","settings",",")
        file.addItem(.separator()); item(file,"Export settings","export"); item(file,"Import settings","import"); item(file,"SSH key manager","keys")
        for (title, action, key) in [("Copy",#selector(NSText.copy(_:)),"c"),("Paste",#selector(NSText.paste(_:)),"v"),("Select All",#selector(NSText.selectAll(_:)),"a")] { edit.addItem(withTitle: title, action: action, keyEquivalent: key) }
        item(tabs,"Duplicate tab","duplicate","d",shift: true); item(tabs,"Close tab","close","w")
        item(tabs,"Reconnect","restart","r",shift: true); item(tabs,"Next tab","next","]",shift: true)
        item(tabs,"Previous tab","previous","[",shift: true); item(tabs,"Move tab left","left"); item(tabs,"Move tab right","right")
        NSApp.mainMenu = menu
    }
    @objc func menuAction(_ sender: NSMenuItem) {
        switch sender.representedObject as? String {
        case "about": about()
        case "quit": NSApp.terminate(nil)
        case "new": select(nil)
        case "local": openLocal()
        case "host": edit(nil)
        case "settings": edit(active?.host ?? selectedHost)
        case "sftp": if let host = active?.host ?? selectedHost { safely { try openSFTP(host) } }
        case "duplicate": duplicate()
        case "restart": restart()
        case "close": if let session = active { close(session) } else { window.performClose(nil) }
        case "next", "previous":
            if !sessions.isEmpty {
                let index = sessions.firstIndex(where: { $0.id == selected }) ?? -1
                select(sessions[(index + (sender.representedObject as? String == "next" ? 1 : sessions.count-1) + sessions.count) % sessions.count].id)
            }
        case "left": moveTab(-1)
        case "right": moveTab(1)
        case "export": exportSettings()
        case "import": importSettings()
        case "folder": NSWorkspace.shared.open(store.root)
        case "keys": keyManager()
        case "delete":
            if let host = selectedHost {
                let alert = Theme.alert("Delete \(host.name)?", "The host and its saved password will be removed.")
                alert.addButton(withTitle: "Delete host"); alert.addButton(withTitle: "Cancel")
                if alert.runModal() == .alertFirstButtonReturn { safely { try store.remove(host); try CredentialStore().remove(host.credentialID); try refresh() } }
            }
        case "copy-host":
            if var host = selectedHost { host.name += " copy"; safely { try store.save(host); try refresh() } }
        case "licenses":
            if let url = Bundle.main.url(forResource: "legal-notices", withExtension: "txt") { NSWorkspace.shared.open(url) }
        default: break
        }
    }
    func tools() {
        let menu = NSMenu()
        for (title, command) in [("Local terminal", "local"),("Open SFTP", "sftp"),("Duplicate tab", "duplicate"),("Reconnect", "restart"),
            ("Move tab left", "left"),("Move tab right", "right"),("SSH key manager", "keys"),
            ("Duplicate saved host", "copy-host"),("Delete saved host", "delete"),
            ("Export settings", "export"),("Import settings", "import"),("Open data folder", "folder"),("Licenses", "licenses")] {
            let item = NSMenuItem(title: title, action: #selector(menuAction(_:)), keyEquivalent: "")
            item.target = self; item.representedObject = command; menu.addItem(item)
        }
        menu.popUp(positioning: nil, at: NSPoint(x: 18,y: sidebar.bounds.height-54), in: sidebar)
    }
    func about() {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "Development"
        let alert = Theme.alert("wShell \(version)", "Created by Hyunwook Park\n\nNative SSH and local terminal for macOS.\nFlexoki Dark · JetBrains Mono\n\nMIT License. SwiftTerm and third-party licenses are included in the app.\n\nPasswords are stored in this Mac's login Keychain and never included in Export / Import.")
        alert.addButton(withTitle: "Close"); alert.addButton(withTitle: "Licenses")
        if alert.runModal() == .alertSecondButtonReturn, let url = Bundle.main.url(forResource: "legal-notices", withExtension: "txt") { NSWorkspace.shared.open(url) }
    }
    func exportSettings() {
        let panel = NSSavePanel(); panel.title = "Export settings — passwords are excluded"; panel.message = Backup.notice
        panel.nameFieldStringValue = "wShell-settings.wshell"
        if panel.runModal() == .OK, let url = panel.url {
            safely { try store.exportBackup().write(to: url, options: .atomic); _ = Theme.alert("Settings exported", Backup.notice).runModal() }
        }
    }
    func importSettings() {
        let panel = NSOpenPanel(); panel.title = "Import settings — enter passwords again"; panel.message = Backup.notice
        panel.allowsMultipleSelection = false; panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            safely {
                let count = try store.importBackup(Data(contentsOf: url)); try refresh()
                _ = Theme.alert("Imported \(count) records", "Existing hosts and host keys were preserved. Review connection settings before connecting.\n\n" + Backup.notice + "\n\nWindows PPK keys need an OpenSSH-format key on macOS. Host trust is separate for the two SSH engines.").runModal()
            }
        }
    }
    func keyManager() {
        let alert = Theme.alert("SSH key manager", "Generate an OpenSSH private key and .pub public key. The terminal asks for a passphrase; it is never placed in process arguments. Use an OpenSSH-format key in Connection settings.")
        alert.addButton(withTitle: "Ed25519"); alert.addButton(withTitle: "RSA 4096"); alert.addButton(withTitle: "Cancel")
        let response = alert.runModal(); guard response != .alertThirdButtonReturn else { return }
        let panel = NSSavePanel(); panel.title = "Choose a new private-key file"; panel.nameFieldStringValue = "wshell_ed25519"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        safely {
            guard !FileManager.default.fileExists(atPath: url.path), !FileManager.default.fileExists(atPath: url.path + ".pub") else { throw WShellError("Choose a new file name. Existing keys are preserved.") }
            let session = Session(kind: .keygen, title: "SSH key generation"); try attach(session)
            let algorithm = response == .alertFirstButtonReturn ? ["-t","ed25519"] : ["-t","rsa","-b","4096"]
            session.terminal.startProcess(executable: "/usr/bin/ssh-keygen", args: algorithm + ["-f",url.path,"-C","wShell"], environment: environment().map { "\($0.key)=\($0.value)" })
        }
    }
}
