"""Package only distributable files; never include live portable data."""
from pathlib import Path
import hashlib
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAME = "wook-shell-0.1.0-win-x64"
OUT = ROOT / "dist" / NAME
OUT.mkdir(parents=True, exist_ok=True)
files = [(ROOT / "build/app/WookShell.exe", "WookShell.exe")]
for binary in ("putty", "puttygen", "pageant", "plink", "pscp", "psftp"):
    files.append((ROOT / f"build/engine/{binary}.exe", "wook-putty.exe" if binary == "putty" else f"{binary}.exe"))
for path in (ROOT / "assets/fonts").glob("*.ttf"):
    files.append((path, f"fonts/{path.name}"))
for path in (ROOT / "licenses").iterdir():
    if path.is_file():
        files.append((path, f"licenses/{path.name}"))
for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md"):
    files.append((ROOT / name, name))
for source, name in files:
    target = OUT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
archive = ROOT / "dist" / f"{NAME}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for _, name in files:
        bundle.write(OUT / name, f"{NAME}/{name}")
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(archive.parent / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
print(f"Portable app: {OUT}")
print(f"ZIP: {archive.stat().st_size / 1048576:.2f} MiB")
print(f"SHA-256: {digest}")
