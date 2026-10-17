"""Check the patched default and execute PuTTY's actual key-event scroll path."""
import ctypes
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class KeyScrollTest(unittest.TestCase):
    def test_default_uses_new_persistent_key(self):
        source = (ROOT / '.deps/putty/conf.h').read_text()
        option = re.search(r'CONF_OPTION\(scroll_on_key,\n(.*?)\n\)', source, re.S).group(1)
        self.assertIn('DEFAULT_BOOL(true)', option)
        self.assertIn('SAVE_KEYWORD("WookScrollOnKey")', option)
        self.assertNotIn('SAVE_KEYWORD("ScrollOnKey")', option)

    def test_key_event_returns_to_live_view_and_respects_opt_out(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler required')
        source = (ROOT / '.deps/putty/terminal/terminal.c').read_text()
        begin = source.index('void term_seen_key_event(')
        function = source[begin:source.index('\n}', begin) + 2]
        harness = '''#include <stdbool.h>
#include <stdlib.h>
#define sfree free
struct beeptime { struct beeptime *next; };
typedef struct {
    bool beep_overloaded, scroll_on_key, win_scrollbar_update_pending;
    struct beeptime *beephead, *beeptail;
    int nbeeps, disptop, updates;
} Terminal;
static void term_schedule_update(Terminal *term) { term->updates++; }
''' + function + '''
int probe(int enabled, int position) {
    Terminal term = {.scroll_on_key=enabled, .disptop=position};
    term_seen_key_event(&term);
    if (!enabled || position == 0)
        return term.disptop == position && !term.updates && !term.win_scrollbar_update_pending;
    return term.disptop == 0 && term.updates == 1 && term.win_scrollbar_update_pending;
}
'''
        with tempfile.TemporaryDirectory(prefix='key-scroll-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.c').write_text(harness)
            subprocess.run([compiler, '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                            str(folder / 'test.c'), '-o', str(folder / 'test.so')], check=True)
            library = ctypes.CDLL(str(folder / 'test.so'))
            for enabled in (0, 1):
                for position in (-100, -1, 0):
                    self.assertEqual(library.probe(enabled, position), 1)
