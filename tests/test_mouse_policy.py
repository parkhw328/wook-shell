"""Run the patched PuTTY mouse-routing predicate on the Linux host.

This checks dispatch policy, not the Windows clipboard or OS event routing.
"""
import ctypes
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MousePolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler required')
        source = (ROOT / '.deps/putty/terminal/terminal.c').read_text()
        start = source.index('bool wshellApplicationMouse(')
        helper = source[start:source.index('void term_mouse(', start)]
        predicate = re.search(r'bool raw_mouse = .*?;', source, re.S).group()
        folder = ROOT / 'build'
        folder.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='mouse-policy-', dir=folder)
        folder = Path(cls.temp.name)
        harness = '''#include <stdbool.h>
enum { MBT_NOTHING, MBT_LEFT, MBT_MIDDLE, MBT_RIGHT,
       MBT_WHEEL_UP, MBT_WHEEL_DOWN, MBT_WHEEL_LEFT, MBT_WHEEL_RIGHT };
enum { CONF_wshell_mouse_clipboard };
typedef struct {
    bool *conf;
    int alt_which, disptop, xterm_mouse;
    bool no_mouse_rep, mouse_override;
} Terminal;
static bool conf_get_bool(bool *conf, int key) { (void)key; return *conf; }
''' + helper + '''
int report(int braw, int preference, int screen, int history, int mode,
           int blocked, int shift, int override) {
    bool clipboard = preference;
    Terminal data = {&clipboard, screen, history, mode, blocked, override};
    Terminal *term = &data;
''' + predicate + '''
    return raw_mouse;
}
'''
        (folder / 'policy.c').write_text(harness)
        subprocess.run([compiler, '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        str(folder / 'policy.c'), '-o', str(folder / 'policy.so')], check=True)
        cls.library = ctypes.CDLL(str(folder / 'policy.so'))
        cls.library.report.argtypes = [ctypes.c_int] * 8
        cls.library.report.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def report(self, button, preference=1, screen=1, history=0, mode=3, blocked=0, shift=0, override=1):
        return self.library.report(button, preference, screen, history, mode, blocked, shift, override)

    def test_buttons_and_motion_stay_local_in_both_screens(self):
        for screen in (0, 1):
            for mode in (0, 1, 2, 3):
                for button in range(4):
                    for shift in (0, 1):
                        self.assertEqual(self.report(button, screen=screen, mode=mode, shift=shift), 0)

    def test_all_wheel_directions_still_reach_the_application(self):
        for button in range(4, 8):
            self.assertEqual(self.report(button), 1)
            for kw in [dict(screen=0), dict(history=-5), dict(mode=0), dict(blocked=1), dict(shift=1, override=0)]:
                self.assertEqual(self.report(button, **kw), 0)

    def test_explicit_opt_out_preserves_raw_input_and_shift_override(self):
        for button in range(8):
            self.assertEqual(self.report(button, preference=0), 1)
            self.assertEqual(self.report(button, preference=0, shift=1), 0)
            self.assertEqual(self.report(button, preference=0, shift=1, override=0), 1)
            self.assertEqual(self.report(button, preference=0, blocked=1), 0)

    def test_live_preference_change_restores_local_selection(self):
        for button in (1, 2, 3):
            self.assertEqual([self.report(button, preference=p) for p in (1, 0, 1)], [0, 1, 0])
