"""Build, verify and package a native macOS universal app. Requires macOS/Xcode CLI tools."""
from pathlib import Path
import hashlib
import plistlib
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text().strip()
MAC = ROOT / "mac"
BUILD = ROOT / "build" / "mac"
APP = BUILD / "wShell.app"

def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)

if sys.platform != "darwin":
    raise SystemExit("Run on macOS, or use the macOS job in GitHub Actions.")
BUILD.mkdir(parents=True, exist_ok=True)
run(sys.executable, ROOT / "scripts/embed-notices.py")
run("swift", "package", "--package-path", MAC, "resolve", "--force-resolved-versions")
run("swift", "test", "--package-path", MAC)
paths = []
for arch in ("arm64", "x86_64"):
    command = ["swift", "build", "--package-path", MAC, "--arch", arch, "-c", "release"]
    run(*command, "--product", "wShell", "--disable-automatic-resolution")
    paths.append(Path(subprocess.check_output(command + ["--show-bin-path"], text=True).strip()))
if APP.exists():
    assert APP.resolve().parent == BUILD.resolve() and not APP.is_symlink()
    shutil.rmtree(APP)
binary = APP / "Contents/MacOS/wShell"
resources = APP / "Contents/Resources"
binary.parent.mkdir(parents=True); resources.mkdir(parents=True)
run("lipo", "-create", *(path / "wShell" for path in paths), "-output", binary)
run("lipo", binary, "-verify_arch", "arm64", "x86_64")
for path in paths[0].glob("*.bundle"):
    shutil.copytree(path, resources / path.name)
for name in ("JetBrainsMono-Regular.ttf", "JetBrainsMono-Bold.ttf"):
    shutil.copyfile(ROOT / "assets/fonts" / name, resources / name)
shutil.copyfile(ROOT / "build/legal-notices.txt", resources / "legal-notices.txt")
iconset = BUILD / "wShell.iconset"; iconset.mkdir(exist_ok=True)
for size in (16, 32, 128, 256, 512):
    for scale in (1, 2):
        name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
        run("sips", "-z", size * scale, size * scale, ROOT / "assets/branding/wshell-icon.png", "--out", iconset / name, stdout=subprocess.DEVNULL)
run("iconutil", "-c", "icns", iconset, "-o", resources / "wShell.icns")
info = {"CFBundleName":"wShell", "CFBundleDisplayName":"wShell", "CFBundleExecutable":"wShell",
        "CFBundleIdentifier":"com.wshell.desktop", "CFBundlePackageType":"APPL", "CFBundleIconFile":"wShell.icns",
        "CFBundleShortVersionString":VERSION, "CFBundleVersion":VERSION, "LSMinimumSystemVersion":"13.0",
        "NSHighResolutionCapable":True, "NSPrincipalClass":"NSApplication",
        "NSHumanReadableCopyright":"Created by Hyunwook Park. MIT License. Third-party notices included."}
(APP / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
run("codesign", "--force", "--deep", "--sign", "-", APP)
run("codesign", "--verify", "--deep", "--strict", APP)
imports = subprocess.check_output(["otool", "-L", str(binary)], text=True)
for line in imports.splitlines():
    if "compatibility version" in line:
        library = line.strip().split(" (")[0]
        assert library.startswith(("/usr/lib/", "/System/Library/", "@rpath/libswift")), library
output = ROOT / "dist" / VERSION / "macos-universal"; output.mkdir(parents=True, exist_ok=True)
archive = output / f"wshell-{VERSION}-macos-universal.zip"
if archive.exists(): archive.unlink()
run("ditto", "-c", "-k", "--keepParent", APP, archive)
with zipfile.ZipFile(archive) as bundle:
    assert bundle.testzip() is None
    assert "wShell.app/Contents/MacOS/wShell" in bundle.namelist()
    assert not any(name.endswith((".ppk", ".pem", ".key", ".wshell", "settings.json")) for name in bundle.namelist())
archive.with_suffix(".zip.sha256").write_text(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n")
print(f"PASS: macOS 13+, arm64 + x86_64, fonts/icon/licenses embedded. {archive.stat().st_size / 1048576:.2f} MiB ZIP.")
print("Ad-hoc signed; Developer ID signing and notarization are not configured.")
