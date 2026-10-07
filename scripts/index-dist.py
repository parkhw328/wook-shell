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
downloads = [f"- {item['platform']}: `{version}/{item['file']}`" for item in artifacts]
notes = []
if any(item["platform"] == "windows-x64" for item in artifacts):
    notes.append("Windows is a single executable, without code signing.")
if any(item["platform"] == "macos-universal" for item in artifacts):
    notes.append("macOS is an ad-hoc signed wShell.app bundle, without Developer ID notarization.")
(ROOT / "dist/README.md").write_text(f"# wShell {version}\n\n" + "\n".join(downloads) +
    "\n\nOnly artifacts present for this version are listed. SHA-256 files accompany each download. Passwords and private keys are excluded.\n" +
    "\n".join(notes) + "\n", encoding="utf-8")
print(f"Indexed {len(artifacts)} artifacts in {release}")
