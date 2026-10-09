"""Verify versioned downloads, record build provenance, and refresh the Git catalog."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def checksum(path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    expected = path.with_name(path.name + ".sha256").read_text().split()
    if expected != [digest, path.name]:
        raise ValueError(f"Checksum mismatch: {path}")
    return digest


def verify_archive(path, executable=None):
    with zipfile.ZipFile(path) as bundle:
        if bundle.testzip() is not None:
            raise ValueError(f"Archive CRC failure: {path}")
        for name in bundle.namelist():
            entry = PurePosixPath(name.lower())
            if (entry.is_absolute() or ".." in entry.parts or "\\" in name or
                    set(entry.parts) & {"data", "keys", ".ssh", "sessions", "build", ".tools", "node_modules"} or
                    entry.suffix in {".ppk", ".pem", ".key", ".ws", ".lock"}):
                raise ValueError(f"Unexpected archive entry in {path.name}: {name}")
        if executable is not None and (bundle.namelist() != ["wShell.exe"] or
                                      bundle.read("wShell.exe") != executable.read_bytes()):
            raise ValueError(f"ZIP must contain the matching single executable: {path}")


def artifacts_for(release):
    artifacts = []
    for platform in ("windows-x64", "macos-universal"):
        folder = release / platform
        if not folder.is_dir():
            continue
        names = [f"wshell-{release.name}-{platform}.zip"]
        if platform == "windows-x64":
            names.insert(0, "wShell.exe")
        allowed = set(names + [name + ".sha256" for name in names])
        if {path.name for path in folder.iterdir()} != allowed:
            raise ValueError(f"Unexpected or missing release files: {folder}")
        for name in names:
            path = folder / name
            digest = checksum(path)
            if path.suffix == ".zip":
                verify_archive(path, folder / "wShell.exe" if platform == "windows-x64" else None)
            artifacts.append({"platform": platform, "file": path.relative_to(release).as_posix(),
                              "bytes": path.stat().st_size, "sha256": digest})
    if not artifacts:
        raise ValueError(f"No release artifacts: {release}")
    return artifacts


def manifest_for(release, artifacts, provenance=None):
    path = release / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    if manifest and manifest.get("version") != release.name:
        raise ValueError(f"Manifest version mismatch: {path}")
    if manifest and provenance is None:
        old = sorted(manifest["artifacts"], key=lambda item: item["file"])
        if old != sorted(artifacts, key=lambda item: item["file"]):
            raise ValueError(f"Published manifest mismatch: {path}")
        return manifest
    manifest.update(version=release.name, createdBy="Hyunwook Park", artifacts=artifacts)
    if provenance is not None:
        for key in ("sourceCommit", "sourceDirty", "workflow", "buildHost", "validation"):
            manifest.pop(key, None)
        manifest.update(provenance)
    return manifest


def build_provenance():
    commit = os.environ.get("GITHUB_SHA") or subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(
        ["git", "status", "--porcelain", "--", ".", ":(exclude)release", ":(exclude)dist"], cwd=ROOT, text=True).strip())
    result = {"sourceCommit": commit, "sourceDirty": dirty}
    if os.environ.get("GITHUB_RUN_ID"):
        result["workflow"] = (f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
                              f"/actions/runs/{os.environ['GITHUB_RUN_ID']}")
    return result


def validation_note(manifest):
    if manifest.get("validation", {}).get("windowsRuntime") == "not-run":
        return "Linux cross-build; static package checks passed. Windows runtime / NGS testing has not been run."
    return ""


def index(catalog_only=False, cross_built=False):
    version = (ROOT / "VERSION").read_text().strip()
    folders = [path for path in (ROOT / "release").iterdir() if path.is_dir() and re.fullmatch(r"\d+\.\d+\.\d+", path.name)]
    folders.sort(key=lambda path: tuple(map(int, path.name.split("."))), reverse=True)
    pending, rows = [], []
    latest = {}
    current_note = ""
    for release in folders:
        artifacts = artifacts_for(release)
        provenance = build_provenance() if release.name == version and not catalog_only else None
        if provenance is not None and cross_built:
            if provenance["sourceDirty"]:
                raise ValueError("Commit source changes before indexing a cross-built release")
            provenance.pop("workflow", None)
            provenance.update(buildHost="linux", validation={"staticPackage": "passed", "windowsRuntime": "not-run", "ngs": "not-run"})
        manifest = manifest_for(release, artifacts, provenance)
        note = validation_note(manifest)
        if release.name == version:
            current_note = note
        pending.append((release / "manifest.json", manifest))
        for item in artifacts:
            if item["file"].endswith(".exe"):
                continue
            path = f"{release.name}/{item['file']}"
            latest.setdefault(item["platform"], (release.name, path))
            status = f" · {note}" if item["platform"] == "windows-x64" and note else ""
            rows.append(f"| {release.name} | {item['platform']} | [ZIP]({path}) | [SHA-256]({path}.sha256) · [manifest]({release.name}/manifest.json){status} |")
    ipad_count = 0
    for path in sorted((ROOT / "tests/ipad").glob("*/*.ipa")):
        digest = checksum(path)
        verify_archive(path)
        manifest = json.loads((path.parent / "manifest.json").read_text())
        if manifest["sha256"] != digest or manifest["bytes"] != path.stat().st_size:
            raise ValueError(f"iPad manifest mismatch: {path}")
        ipad_count += 1
    # Write only after all releases validate. Catalog-only preserves provenance.
    for path, manifest in pending:
        if not path.exists() or json.loads(path.read_text(encoding="utf-8")) != manifest:
            path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    latest_rows = []
    for platform in ("windows-x64", "macos-universal"):
        if platform not in latest:
            continue
        latest_version, latest_path = latest[platform]
        download = f"[ZIP]({latest_path})"
        if platform == "windows-x64":
            download = f"[EXE]({latest_version}/windows-x64/wShell.exe) · " + download
        latest_rows.append(f"| {platform} | {latest_version} | {download} |")
    catalog = ("# wShell downloads\n\n"
               f"Current Windows release: **{version}**. [Standalone EXE]({version}/windows-x64/wShell.exe) · "
               f"[ZIP]({version}/windows-x64/wshell-{version}-windows-x64.zip)\n\n"
               + (f"**Validation status: {current_note}**\n\n" if current_note else "") +
               "## Latest available builds\n\n"
               "| Platform | Version | Download |\n| --- | --- | --- |\n" + "\n".join(latest_rows) + "\n\n"
               "Latest means the newest published build for each platform. The macOS build predates the new Windows icon and features. "
               "See the [Windows release notes](../docs/releases/" + version + ".md) for test results and limitations. iPad is on hold.\n\n"
               "Versioned binaries, checksums and manifests are tracked in Git. Open a file and select **Download raw file** to download it. "
               "Each Windows ZIP contains only wShell.exe. Windows releases are unsigned.\n\n"
               "| Version | Platform | Download | Verification |\n| --- | --- | --- | --- |\n" + "\n".join(rows) +
               "\n\niPad preview files are archived under [tests/ipad](../tests/ipad/README.md) for future testing, outside the release downloads." +
               "\n\nmacOS and iPad builds are paused. These older artifacts do not include newer Windows changes. "
               "The macOS app is ad-hoc signed, without Developer ID notarization. "
               "The unsigned IPA requires separate Apple-account signing before installation; it is not a directly installable release.\n\n"
               "Manifests record file sizes and SHA-256 values; new builds also record the source commit, plus the CI run when built on Actions. "
               "Historical manifests retain known metadata only. Build tools, caches, passwords and private keys are excluded.\n")
    (ROOT / "release/README.md").write_text(catalog, encoding="utf-8")
    print(f"Verified and indexed {len(folders)} desktop releases; verified {ipad_count} iPad test archives.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--catalog-only", action="store_true", help="Verify downloads without changing build provenance")
    mode.add_argument("--cross-built", action="store_true", help="Record Linux static verification and unrun Windows runtime tests")
    args = parser.parse_args()
    index(args.catalog_only, args.cross_built)
