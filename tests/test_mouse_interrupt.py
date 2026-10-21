"""Run actual patched keyboard entry checks without a Windows desktop."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MouseInterruptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler required')
        source = (ROOT / '.deps/putty/terminal/terminal.c').read_text()
        begin = source.index('static void wshellInterruptMouse(')
        helper = source[begin:source.index('void term_keyinputw(', begin)]
        probes = []
        for function, kind, value in [('term_keyinput', 'unsigned char', 'str'), ('term_keyinputw', 'wchar_t', 'widebuf')]:
            start = source.index('void ' + function + '(')
            entry = source[source.index('{', start) + 1:source.index('    if (term->wshell_input)', start)]
            probes.append(f'''int {function}_probe(int key, int len) {{
                Terminal data = {{.xterm_mouse=3, .mouse_is_down=1, .raw_mouse_reported_x=66, .raw_mouse_reported_y=13}}; Terminal *term = &data;
                {kind} {value}[2] = {{key, 'x'}};
                {entry}
                return data.xterm_mouse | (data.updates << 4) |
                    ((!data.xterm_mouse && (data.mouse_is_down || data.raw_mouse_reported_x!=-1 || data.raw_mouse_reported_y!=-1)) << 8);
            }}''')
        cls.temp = tempfile.TemporaryDirectory(prefix='mouse-interrupt-', dir=ROOT / 'build')
        folder = Path(cls.temp.name)
        (folder / 'test.c').write_text('''#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { int xterm_mouse, updates, mouse_is_down, raw_mouse_reported_x, raw_mouse_reported_y;
    uint64_t wshell_paste_epoch; size_t wshell_local_paste_pending; } Terminal;
static void term_update_raw_mouse_mode(Terminal *term) { term->updates++; }
''' + helper + '\n'.join(probes))
        subprocess.run([compiler, '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        str(folder / 'test.c'), '-o', str(folder / 'test.so')], check=True)
        cls.library = ctypes.CDLL(str(folder / 'test.so'))

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)
            del cls.library
        cls.temp.cleanup()

    def test_etx_stops_reports_for_both_keyboard_paths(self):
        for name in ('term_keyinput_probe', 'term_keyinputw_probe'):
            self.assertEqual(getattr(self.library, name)(3, 1), 16)

    def test_other_keys_and_multi_character_input_do_not_reset_modes(self):
        for name in ('term_keyinput_probe', 'term_keyinputw_probe'):
            for key, length in [(ord('c'), 1), (13, 1), (3, 2), (3, 0)]:
                self.assertEqual(getattr(self.library, name)(key, length), 3)
