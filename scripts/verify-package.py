"""Verify the x64 portable contract and embedded Windows icon, without running code."""
from pathlib import Path
import ctypes
import hashlib
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
NAME = f"wshell-{VERSION}-windows-x64"
folder = ROOT / "dist" / VERSION / "windows-x64"
archive = folder / f"{NAME}.zip"
expected = archive.with_suffix(".zip.sha256").read_text().split()[0]
assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected, "ZIP checksum mismatch"
with zipfile.ZipFile(archive) as bundle:
    entries = bundle.namelist()
    assert entries == ["wShell.exe"], "Single executable distribution must contain exactly one file"
    assert not any(set(name.lower().split("/")) & {"data", "node_modules", "build", ".tools"} for name in entries), "User/development data leaked into ZIP"
    assert not any(name.lower().endswith((".ppk", ".pem", ".key", ".ws", ".lock")) for name in entries), "Credential/session material leaked into ZIP"
    assert bundle.testzip() is None, "ZIP CRC error"
    binary = folder / "wShell.exe"
    assert bundle.read("wShell.exe") == binary.read_bytes(), "ZIP and standalone executable differ"
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary.with_suffix(".exe.sha256").read_text().split()[0], "EXE checksum mismatch"

# Ensure every shipped executable is x64 and imports only Windows system DLLs.
allowed = {"advapi32.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll", "dwmapi.dll", "gdi32.dll", "gdiplus.dll",
           "imm32.dll", "kernel32.dll", "msvcrt.dll", "netapi32.dll", "normaliz.dll", "ole32.dll", "oleaut32.dll",
           "secur32.dll", "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll", "uxtheme.dll", "version.dll",
           "winmm.dll", "winspool.drv", "ws2_32.dll", "wtsapi32.dll", "bcrypt.dll", "ncrypt.dll"}
inspector = ROOT / ".tools/llvm-mingw-20260922-ucrt-x86_64/bin/llvm-readobj.exe"
for binary in folder.glob("*.exe"):
    data = binary.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0" and struct.unpack_from("<H", data, pe + 4)[0] == 0x8664, f"Not x64: {binary.name}"
    imports = subprocess.check_output([str(inspector), "--coff-imports", str(binary)], text=True)
    for library in re.findall(r"^  Name: (.+)$", imports, re.MULTILINE):
        assert library.lower() in allowed or library.lower().startswith("api-ms-win-crt-"), f"External runtime required: {binary.name}: {library}"

# Ask the Windows resource loader to read the actual executable's icon group.
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
kernel.LoadLibraryExW.restype = ctypes.c_void_p
kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
kernel.FindResourceW.restype = ctypes.c_void_p
kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
kernel.LoadResource.restype = ctypes.c_void_p
kernel.LockResource.argtypes = [ctypes.c_void_p]
kernel.LockResource.restype = ctypes.c_void_p
kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
kernel.SizeofResource.restype = ctypes.c_uint32
kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
module = kernel.LoadLibraryExW(str(folder / "wShell.exe"), None, 2)  # LOAD_LIBRARY_AS_DATAFILE
assert module, "Cannot load executable resources"
try:
    resource = kernel.FindResourceW(module, 101, 14)  # RT_GROUP_ICON
    assert resource, "Executable icon is missing"
    size = kernel.SizeofResource(module, resource)
    pointer = kernel.LockResource(kernel.LoadResource(module, resource))
    payload = ctypes.string_at(pointer, size)
    count = struct.unpack_from("<H", payload, 4)[0]
    dimensions = {payload[6 + i * 14] or 256 for i in range(count)}
    assert dimensions == {16, 24, 32, 48, 64, 128, 256}, f"Missing icon resolutions: {dimensions}"
    for resource_id, source in ((103, ROOT / "assets/fonts/JetBrainsMono-Regular.ttf"),
                                (104, ROOT / "assets/fonts/JetBrainsMono-Bold.ttf"),
                                (105, ROOT / "build/legal-notices.txt")):
        resource = kernel.FindResourceW(module, resource_id, 10)
        assert resource, f"Missing embedded asset {resource_id}"
        size = kernel.SizeofResource(module, resource)
        pointer = kernel.LockResource(kernel.LoadResource(module, resource))
        assert ctypes.string_at(pointer, size) == source.read_bytes(), f"Embedded asset changed: {source.name}"
finally:
    kernel.FreeLibrary(module)
print(f"PASS: single EXE, embedded fonts/branding/licenses, x64 system-only imports, seven icon resolutions ({archive.stat().st_size / 1048576:.2f} MiB ZIP).")
