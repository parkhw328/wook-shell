"""Execute patched alternate-screen mode handlers; Windows UI is tested separately."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TerminalExitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler required')
        source = (ROOT / '.deps/putty/terminal/terminal.c').read_text()
        cases = []
        for mode in (47, 1047, 1049):
            start = source.index(f'          case {mode}:')
            cases.append(source[start:source.index('            break;', start) + len('            break;')])
        folder = ROOT / 'build'
        folder.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='terminal-exit-', dir=folder)
        folder = Path(cls.temp.name)
        harness = '''#include <stdbool.h>
#define OTHER 0
#define compatibility(x) ((void)0)
typedef struct {
    int alt_which, xterm_mouse, disptop, updates;
    bool no_alt_screen, xterm_extended_mouse, urxvt_extended_mouse, scroll_on_disp;
} Terminal;
static void deselect(Terminal *t) { (void)t; }
static void seen_disp_event(Terminal *t) { (void)t; }
static void save_cursor(Terminal *t, bool s) { (void)t; (void)s; }
static void term_update_raw_mouse_mode(Terminal *t) { t->updates++; }
static void swap_screen(Terminal *t, int which, bool clear, bool reset) {
    (void)clear; (void)reset; t->alt_which = which;
}
int probe(int mode, bool state, int alternate, bool disabled) {
    Terminal data = {.alt_which=alternate, .no_alt_screen=disabled, .xterm_mouse=3,
                     .xterm_extended_mouse=true, .urxvt_extended_mouse=true};
    Terminal *term = &data;
    switch(mode) {
''' + '\n'.join(cases) + '''
    }
    return data.xterm_mouse | (data.xterm_extended_mouse << 4) |
           (data.urxvt_extended_mouse << 5) | (data.updates << 6);
}
'''
        (folder / 'exit.c').write_text(harness)
        subprocess.run([compiler, '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror', str(folder / 'exit.c'), '-o', str(folder / 'exit.so')], check=True)
        cls.library = ctypes.CDLL(str(folder / 'exit.so'))
        cls.library.probe.argtypes = [ctypes.c_int, ctypes.c_bool, ctypes.c_int, ctypes.c_bool]
        cls.library.probe.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)
            del cls.library
        cls.temp.cleanup()

    def test_abandoned_mouse_reporting_stops_on_each_screen_exit(self):
        for mode in (47, 1047, 1049):
            self.assertEqual(self.library.probe(mode, False, 1, False), 64)

    def test_entry_and_normal_screen_requests_preserve_application_modes(self):
        for mode in (47, 1047, 1049):
            for state, alternate, disabled in [(True, 0, False), (False, 0, False), (False, 1, True)]:
                self.assertEqual(self.library.probe(mode, state, alternate, disabled), 51)
