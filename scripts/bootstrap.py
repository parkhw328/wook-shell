"""Download verified, repository-local build tools and the PuTTY source."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import platform
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = [
    ("llvm", ".tools", "https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip", "e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666"),
    ("cmake", ".tools", "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip", "bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536"),
    ("ninja", ".tools/ninja", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip", "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65"),
    ("putty", ".deps/putty", "https://the.earth.li/~sgtatham/putty/0.85/putty-src.zip", "232c5c286a5b35f445dbbf49e159469acde372a3907aef738d88e28b4b0f6da2"),
]

if sys.platform == "linux":
    if platform.machine() != "x86_64":
        raise SystemExit("Linux bootstrap currently requires x86_64.")
    PACKAGES = [
        ("llvm", ".tools", "https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz", "bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21"),
        ("cmake", ".tools", "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-linux-x86_64.tar.gz", "e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb"),
        ("ninja", ".tools/ninja-linux", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip", "5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6"),
        PACKAGES[-1],
    ]
elif sys.platform != "win32":
    raise SystemExit("Windows tool bootstrap supports Windows and Linux x86_64 only.")


def fetch(package):
    name, destination, url, expected = package
    cache = ROOT / ".tools/downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / url.rsplit("/", 1)[-1]
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        print(f"Downloading {name}...", flush=True)
        urllib.request.urlretrieve(url, archive)
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError(f"SHA-256 mismatch for {name}: {actual}")
    target = ROOT / destination
    marker = target / f".{name}-{expected[:12]}"
    if not marker.exists():
        target.mkdir(parents=True, exist_ok=True)
        if archive.name.endswith(".zip"):
            with zipfile.ZipFile(archive) as bundle:
                for member in bundle.infolist():
                    resolved = (target / member.filename).resolve()
                    if not resolved.is_relative_to(target.resolve()):
                        raise RuntimeError(f"Unsafe archive path: {member.filename}")
                bundle.extractall(target)
            if sys.platform == "linux" and name == "ninja":
                (target / "ninja").chmod(0o755)
        else:
            if not hasattr(tarfile, "data_filter"):
                raise RuntimeError("Linux bootstrap needs Python with tarfile.data_filter (Python 3.12+ recommended).")
            with tarfile.open(archive) as bundle:
                bundle.extractall(target, filter="data")
        marker.touch()
    print(f"Verified and ready: {name}", flush=True)


if __name__ == "__main__":
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(fetch, PACKAGES))
