# wShell for iPad — personal preview

Native iPadOS 17+ app; intended for private installation. The requested iPadOS 26.6 device has not been physically tested. Versioning is independent of the Windows/macOS release (`ios/VERSION`).

## Features

- Flexoki Dark, orange controls, bundled JetBrains Mono and the wShell icon.
- Multiple SSH terminal tabs, duplication, resizing, ANSI/256/true color, UIKit text input and Ctrl/Esc/Tab/arrow accessory keys.
- Split existing tabs into 2–4 fixed panes. Tap a pane to focus it; selecting a hidden tab replaces the active pane. Single-pane mode keeps the other connections running.
- Password and public-key authentication. Import an OpenSSH/PEM **private** key from Files; a `.pub` file alone cannot authenticate. Export PPK to OpenSSH on desktop first. Encrypted keys request a passphrase each connection.
- SHA-256 server identity verification **before** sending credentials; changed keys are blocked.
- Optional passwords and imported private keys in this device's non-synchronizing Keychain. Settings reside in the private Application Support directory.
- Dedicated SFTP tab: local/remote panes, file import/share, upload/download, directory navigation, create folder, delete file/empty folder. Transfers stage files before publication; remote replacement requires atomic rename support.
- **Show hidden files** starts unchecked and controls both file lists. Dotfiles and local hidden flags stay hidden until enabled.
- Settings export/import explicitly excludes passwords, private keys and trusted host keys. The iPad JSON backup is separate from desktop WSB1. Existing hosts are retained.

## Build and validation

On an Apple Silicon Mac with Xcode, CMake, Python 3.12+ and Node.js:

```sh
python3 scripts/build-ios.py
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/ios-integration.cjs
```

Alternatively run **iPad personal preview** in GitHub Actions. Outputs are in `dist/ipad/<version>/`; the generated Xcode project is `build/ios/wShell.xcodeproj`. Sources are compiled locally from checksummed archives; no third-party binary SSH framework is downloaded. libssh2 is pinned to a development snapshot (see third-party notices), so this remains a preview.

CI builds both simulator and arm64 device apps. An isolated loopback server verifies encrypted RSA/SHA-2, Ed25519, password authentication, host rejection, Keychain, Unicode input and exact SFTP bytes. Simulator evidence does not establish real-device installation or physical Korean keyboard behavior.

## Install with a free Apple account

The `*-unsigned.ipa` **cannot be installed directly**. Sign it locally with your own Apple account. Never send the Apple password or a signing certificate to this repository or to chat.

Windows route: use [Sideloadly's official site](https://sideloadly.io/). Follow its current Windows prerequisites (Apple's standalone iTunes/iCloud), connect and trust the iPad, select the IPA, and sign/install with your account. Follow the tool's account verification prompts locally. Enable Developer Mode and trust your developer profile on the iPad when prompted. A Mac can use the same signing tool, or Xcode with a Personal Team and a unique bundle identifier.

A free Apple account's development provisioning expires after **7 days**, requiring refresh/reinstallation; keep the same account and app identifier to retain app data where possible. Export settings before reinstalling or deleting the app, and keep original private keys separately: settings backups deliberately contain no secrets. Check [Apple's Personal Team limits](https://developer.apple.com/help/account/basics/about-your-developer-account) and [Sideloadly's FAQ](https://sideloadly.io/faq) for current requirements. No paid account or App Store publication is required for this personal workflow.

For the requested iPadOS 26.6 device, see the [Derpy 1.0.1 and free signing assessment](ipad-signing.md). Derpy's release supports App Store downloads; adding a development signature to this app does not meet that requirement. Sideloadly's auto-refresh can reduce manual renewal while a paired computer remains available; it does not remove the seven-day expiry. The repository already includes `.github/workflows/ipad.yml` and `rules/ipad.md`.

## Current boundaries

Connections disconnect when the app goes into the background; return and reconnect. No iPad local system shell, Telnet/serial, forwarding/jump hosts, SSH agent, keyboard-interactive/OTP, on-device key generation, folder-recursive SFTP, synchronization or transfer resume in this preview. Cancel closes the SFTP connection and may leave a remote `.wshell-*.part` file. Delete removes remote files permanently and asks for confirmation. Imported local files are available through Files → On My iPad → wShell.

Created by **Hyunwook Park**. wShell is MIT licensed; third-party license texts are bundled in About.
