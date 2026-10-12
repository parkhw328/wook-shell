"""Execute the actual patched PuTTY arrow encoder on the build host.

Run scripts/patch-putty.py first. This tests wire bytes, not Windows key routing.
"""
import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ArrowKeysTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("Host C compiler required for encoder tests")
        source = (ROOT / ".deps/putty/terminal/terminal.c").read_text()
        encoder = source[source.index("static int shift_bitmap("):source.index("int format_function_key(")]
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        harness = '''#include <stdbool.h>
#include <stdio.h>
enum { SHARROW_APPLICATION, SHARROW_BITMAP };
typedef struct { bool vt52_mode, app_cursor_keys, no_applic_c; int sharrow_type; } Terminal;
''' + encoder + '''
int encode(char *buf, int key, int modifiers, int app, int disabled, int vt52, int legacy, bool *consumed) {
    Terminal term = { vt52, app, disabled, legacy ? SHARROW_APPLICATION : SHARROW_BITMAP };
    return format_arrow_key(buf, &term, key, modifiers & 1, modifiers & 4, modifiers & 2, consumed);
}
'''
        (folder / "encoder.c").write_text(harness)
        subprocess.run([compiler, "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(folder / "encoder.c"), "-o", str(folder / "encoder.so")], check=True)
        cls.library = ctypes.CDLL(str(folder / "encoder.so"))
        cls.library.encode.argtypes = [ctypes.c_char_p] + [ctypes.c_int] * 6 + [ctypes.POINTER(ctypes.c_bool)]
        cls.library.encode.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def encode(self, key="D", modifiers=0, app=0, disabled=0, vt52=0, legacy=0):
        buffer = ctypes.create_string_buffer(32)
        consumed = ctypes.c_bool(False)
        size = self.library.encode(buffer, ord(key), modifiers, app, disabled, vt52, legacy, ctypes.byref(consumed))
        return buffer.raw[:size], consumed.value

    def test_modifiers_survive_normal_and_application_cursor_modes(self):
        for key in "ABCD":
            for app in (0, 1):
                for disabled in (0, 1):
                    for modifiers in range(1, 8):
                        with self.subTest(key=key, app=app, disabled=disabled, modifiers=modifiers):
                            self.assertEqual(self.encode(key, modifiers, app, disabled),
                                             (f"\x1b[1;{modifiers + 1}{key}".encode(), bool(modifiers & 2)))

    def test_unmodified_arrows_keep_application_mode(self):
        self.assertEqual(self.encode(app=0), (b"\x1b[D", False))
        self.assertEqual(self.encode(app=1), (b"\x1bOD", False))
        self.assertEqual(self.encode(app=1, disabled=1), (b"\x1b[D", False))

    def test_legacy_ctrl_toggle_and_vt52_unchanged(self):
        self.assertEqual(self.encode(modifiers=1, legacy=1), (b"\x1b[D", False))
        self.assertEqual(self.encode(modifiers=4, legacy=1), (b"\x1bOD", False))
        self.assertEqual(self.encode(modifiers=4, app=1, legacy=1), (b"\x1b[D", False))
        self.assertEqual(self.encode(modifiers=7, vt52=1), (b"\x1bD", False))


if __name__ == "__main__":
    unittest.main()
