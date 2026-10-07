"""Produce a reviewable release manifest from the versioned platform folders."""
from pathlib import Path
import hashlib
import json
ROOT = Path(__file__).resolve().parents[1]
version = (ROOT / "VERSION").read_text().strip()
release = ROOT / "dist" / version
artifacts = []
for platform in ("windows-x64", "macos-universal"):
    for path in sorted((release / platform).glob("*")):
        if path.suffix in (".exe", ".zip"):
            artifacts.append({"platform": platform, "file": path.relative_to(release).as_posix(),
                              "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
(release / "manifest.json").write_text(json.dumps({"version": version, "createdBy": "Hyunwook Park", "artifacts": artifacts}, indent=2) + "\n")
(ROOT / "dist/README.md").write_text(f"# wShell {version}\n\n- Windows x64: `{version}/windows-x64/wShell.exe` (single executable).\n- macOS 13+, Apple Silicon and Intel: `{version}/macos-universal/wshell-{version}-macos-universal.zip` (wShell.app).\n\nSHA-256 files accompany each download. Passwords and private keys are excluded.\nWindows is unsigned; macOS is ad-hoc signed, without Developer ID notarization.\n", encoding="utf-8")
print(f"Indexed {len(artifacts)} artifacts in {release}")
