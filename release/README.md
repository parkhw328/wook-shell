# wShell downloads

Current Windows release: **0.14.1**. [Standalone EXE](0.14.1/windows-x64/wShell.exe) · [ZIP](0.14.1/windows-x64/wshell-0.14.1-windows-x64.zip)

## Latest available builds

| Platform | Version | Download |
| --- | --- | --- |
| windows-x64 | 0.14.1 | [EXE](0.14.1/windows-x64/wShell.exe) · [ZIP](0.14.1/windows-x64/wshell-0.14.1-windows-x64.zip) |
| macos-universal | 0.11.0 | [ZIP](0.11.0/macos-universal/wshell-0.11.0-macos-universal.zip) |

Latest means the newest published build for each platform. The macOS build predates the new Windows icon and features. See the [Windows release notes](../docs/releases/0.14.1.md) for test results and limitations. iPad is on hold.

Versioned binaries, checksums and manifests are tracked in Git. Open a file and select **Download raw file** to download it. Each Windows ZIP contains only wShell.exe. Windows releases are unsigned.

| Version | Platform | Download | Verification |
| --- | --- | --- | --- |
| 0.14.1 | windows-x64 | [ZIP](0.14.1/windows-x64/wshell-0.14.1-windows-x64.zip) | [SHA-256](0.14.1/windows-x64/wshell-0.14.1-windows-x64.zip.sha256) · [manifest](0.14.1/manifest.json) |
| 0.14.0 | windows-x64 | [ZIP](0.14.0/windows-x64/wshell-0.14.0-windows-x64.zip) | [SHA-256](0.14.0/windows-x64/wshell-0.14.0-windows-x64.zip.sha256) · [manifest](0.14.0/manifest.json) · Linux cross-build; static package checks passed. Windows runtime / NGS testing has not been run. |
| 0.13.0 | windows-x64 | [ZIP](0.13.0/windows-x64/wshell-0.13.0-windows-x64.zip) | [SHA-256](0.13.0/windows-x64/wshell-0.13.0-windows-x64.zip.sha256) · [manifest](0.13.0/manifest.json) |
| 0.12.0 | windows-x64 | [ZIP](0.12.0/windows-x64/wshell-0.12.0-windows-x64.zip) | [SHA-256](0.12.0/windows-x64/wshell-0.12.0-windows-x64.zip.sha256) · [manifest](0.12.0/manifest.json) |
| 0.11.0 | windows-x64 | [ZIP](0.11.0/windows-x64/wshell-0.11.0-windows-x64.zip) | [SHA-256](0.11.0/windows-x64/wshell-0.11.0-windows-x64.zip.sha256) · [manifest](0.11.0/manifest.json) |
| 0.11.0 | macos-universal | [ZIP](0.11.0/macos-universal/wshell-0.11.0-macos-universal.zip) | [SHA-256](0.11.0/macos-universal/wshell-0.11.0-macos-universal.zip.sha256) · [manifest](0.11.0/manifest.json) |
| 0.10.0 | windows-x64 | [ZIP](0.10.0/windows-x64/wshell-0.10.0-windows-x64.zip) | [SHA-256](0.10.0/windows-x64/wshell-0.10.0-windows-x64.zip.sha256) · [manifest](0.10.0/manifest.json) |
| 0.9.1 | windows-x64 | [ZIP](0.9.1/windows-x64/wshell-0.9.1-windows-x64.zip) | [SHA-256](0.9.1/windows-x64/wshell-0.9.1-windows-x64.zip.sha256) · [manifest](0.9.1/manifest.json) |
| 0.9.0 | windows-x64 | [ZIP](0.9.0/windows-x64/wshell-0.9.0-windows-x64.zip) | [SHA-256](0.9.0/windows-x64/wshell-0.9.0-windows-x64.zip.sha256) · [manifest](0.9.0/manifest.json) |
| 0.8.0 | windows-x64 | [ZIP](0.8.0/windows-x64/wshell-0.8.0-windows-x64.zip) | [SHA-256](0.8.0/windows-x64/wshell-0.8.0-windows-x64.zip.sha256) · [manifest](0.8.0/manifest.json) |

iPad preview files are archived under [tests/ipad](../tests/ipad/README.md) for future testing, outside the release downloads.

macOS and iPad builds are paused. These older artifacts do not include newer Windows changes. The macOS app is ad-hoc signed, without Developer ID notarization. The unsigned IPA requires separate Apple-account signing before installation; it is not a directly installable release.

Manifests record file sizes and SHA-256 values; new builds also record the source commit, plus the CI run when built on Actions. Historical manifests retain known metadata only. Build tools, caches, passwords and private keys are excluded.
