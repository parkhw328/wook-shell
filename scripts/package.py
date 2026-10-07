"""Ship exactly one executable; resources and licenses are embedded."""
from pathlib import Path
import hashlib
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAME = "wshell-0.4.0-win-x64"
OUT = ROOT / "dist" / NAME
OUT.mkdir(parents=True, exist_ok=True)
binary = OUT / "wShell.exe"
shutil.copyfile(ROOT / "build/native/wShell.exe", binary)
shutil.copyfile(binary, ROOT / "dist/wShell.exe")
archive = ROOT / "dist" / f"{NAME}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    bundle.write(binary, f"{NAME}/wShell.exe")
for artifact in (archive, ROOT / "dist/wShell.exe"):
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    artifact.with_name(artifact.name + ".sha256").write_text(f"{digest}  {artifact.name}\n", encoding="utf-8")
print(f"Single executable: {binary} ({binary.stat().st_size / 1048576:.2f} MiB)")
print(f"ZIP: {archive.stat().st_size / 1048576:.2f} MiB; contains only wShell.exe")
