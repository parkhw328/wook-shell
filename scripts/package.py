"""Ship exactly one executable; resources and licenses are embedded."""
from pathlib import Path
import argparse
import hashlib
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
NAME = f"wshell-{VERSION}-windows-x64"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, default=ROOT / "build/native/wShell.exe")
parser.add_argument("--output", type=Path, default=ROOT / "release" / VERSION / "windows-x64")
args = parser.parse_args()
OUT = args.output
OUT.mkdir(parents=True, exist_ok=True)
binary = OUT / "wShell.exe"
shutil.copyfile(args.binary, binary)
archive = OUT / f"{NAME}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    bundle.write(binary, "wShell.exe")
for artifact in (archive, binary):
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    artifact.with_name(artifact.name + ".sha256").write_text(f"{digest}  {artifact.name}\n", encoding="utf-8")
print(f"Single executable: {binary} ({binary.stat().st_size / 1048576:.2f} MiB)")
print(f"ZIP: {archive.stat().st_size / 1048576:.2f} MiB; contains only wShell.exe")
