"""Verify the x64 portable contract and embedded Windows icon, without running code."""
from pathlib import Path
import ctypes
import hashlib
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAME = "wshell-0.1.0-win-x64"
folder = ROOT / "dist" / NAME
archive = ROOT / "dist" / f"{NAME}.zip"
expected = archive.with_suffix(".zip.sha256").read_text().split()[0]
assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected, "ZIP checksum mismatch"
with zipfile.ZipFile(archive) as bundle:
    entries = bundle.namelist()
    assert all(name.startswith(NAME + "/") and ".." not in name.split("/") for name in entries)
    assert not any(set(name.lower().split("/")) & {"data", "node_modules", "build", ".tools"} for name in entries), "User/development data leaked into ZIP"
    assert not any(name.lower().endswith((".ppk", ".pem", ".key", ".ws", ".lock")) for name in entries), "Credential/session material leaked into ZIP"
    assert bundle.testzip() is None, "ZIP CRC error"
    for name in ("wShell.exe", "wook-putty.exe", "puttygen.exe", "pageant.exe", "plink.exe", "pscp.exe", "psftp.exe",
                 "LICENSE", "THIRD_PARTY_NOTICES.md", "fonts/JetBrainsMono-Regular.ttf", "fonts/JetBrainsMono-Bold.ttf",
                 "assets/branding/wshell-wordmark.png", "assets/branding/README.md"):
        assert f"{NAME}/{name}" in entries, f"Missing required artifact: {name}"
    for license_file in (ROOT / "licenses").iterdir():
        assert bundle.read(f"{NAME}/licenses/{license_file.name}") == license_file.read_bytes(), "License changed during packaging"

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
finally:
    kernel.FreeLibrary(module)
print(f"PASS: portable ZIP, licenses, x64 system-only imports, seven embedded icon resolutions ({archive.stat().st_size / 1048576:.2f} MiB).")
