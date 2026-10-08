"""Cross-build Windows x64 on Linux; optionally collect a new version in dist."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", action="store_true", help="Download SHA-256 pinned build tools and PuTTY")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--dist", action="store_true", help="Collect a new version in dist with explicit runtime-test status; requires committed source")
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() != "x86_64":
        parser.error("Requires Linux x86_64")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    version = (ROOT / "VERSION").read_text().strip()
    destination = ROOT / "dist" / version / "windows-x64"

    def check_release_source():
        if destination.parent.exists():
            parser.error("This dist version already exists; increment VERSION rather than overwrite it")
        if subprocess.check_output(["git", "status", "--porcelain", "--", ".", ":(exclude)dist"], cwd=ROOT, text=True).strip():
            parser.error("Commit source changes before using --dist")

    if args.dist:
        check_release_source()

    def run(*command):
        subprocess.run([str(value) for value in command], cwd=ROOT, check=True)

    if args.bootstrap:
        run(sys.executable, ROOT / "scripts/bootstrap.py")
    llvm = ROOT / ".tools/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64/bin"
    cmake = ROOT / ".tools/cmake-4.4.4-linux-x86_64/bin/cmake"
    ninja = ROOT / ".tools/ninja-linux/ninja"
    for tool in (cmake, ninja, llvm / "x86_64-w64-mingw32-gcc"):
        if not tool.is_file():
            parser.error("Missing build tools; rerun with --bootstrap")
    os.environ["PATH"] = str(llvm) + os.pathsep + os.environ.get("PATH", "")
    # Optional host compatibility libraries; never linked into the Windows EXE.
    host_libraries = ROOT / ".tools/linux-host-libs"
    if host_libraries.is_dir():
        os.environ["LD_LIBRARY_PATH"] = str(host_libraries) + (
            os.pathsep + os.environ["LD_LIBRARY_PATH"] if os.environ.get("LD_LIBRARY_PATH") else "")
    build = ROOT / "build/windows-linux"
    run(sys.executable, ROOT / "scripts/patch-putty.py")
    run(sys.executable, ROOT / "scripts/embed-notices.py")
    run(cmake, "-S", ROOT / ".deps/putty", "-B", build, "-G", "Ninja",
        "-DCMAKE_SYSTEM_NAME=Windows", "-DCMAKE_SYSTEM_PROCESSOR=AMD64",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DCMAKE_C_COMPILER={llvm}/x86_64-w64-mingw32-gcc",
        f"-DCMAKE_CXX_COMPILER={llvm}/x86_64-w64-mingw32-g++",
        f"-DCMAKE_RC_COMPILER={llvm}/x86_64-w64-mingw32-windres",
        "-DCMAKE_BUILD_TYPE=Release", f"-DWSHELL_ROOT={ROOT}",
        "-DCMAKE_EXE_LINKER_FLAGS=-static -Wl,--nxcompat,--dynamicbase,--high-entropy-va")
    run(cmake, "--build", build, "--target", "WookShell", "ui-smoke-tests", "core-tests",
        "ime-tests", "sftp-tests", "sftp-codec-tests", "launch-relay-tests", "plink", "--parallel", args.jobs)
    output = ROOT / "build/packages" / version / "windows-x64"
    run(sys.executable, ROOT / "scripts/package.py", "--binary", build / "wShell.exe", "--output", output)
    run(sys.executable, ROOT / "scripts/verify-package.py", "--folder", output,
        "--inspector", llvm / "llvm-readobj")
    print(f"Cross-build and static package checks passed: {output}", flush=True)
    if args.dist:
        check_release_source()
        shutil.copytree(output, destination)
        run(sys.executable, ROOT / "scripts/index-dist.py", "--cross-built")
        print(f"Collected in {destination}; Windows runtime tests remain unrun.", flush=True)
    print("NOT RUN: Windows executable tests, SSH/UI/IME/DPAPI runtime validation.")


if __name__ == "__main__":
    main()
