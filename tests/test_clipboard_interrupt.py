"""Execute patched clipboard delivery and ETX entry points on the host.

Clipboard reads and key translation are stubbed; this is not a Windows UI test.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


class ClipboardInterruptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler required')
        terminal = (ROOT / '.deps/putty/terminal/terminal.c').read_text()
        window = (ROOT / '.deps/putty/windows/window.c').read_text()
        integration = (ROOT / 'patches/wook-window.h').read_text()
        start = window.index('      case WM_GOT_CLIPDATA:')
        delivery = window[start:window.index('      default:', start)]
        harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef struct { int line_codepage; } UnicodeData;
typedef struct {
    uint64_t wshell_paste_epoch;
    size_t wshell_local_paste_pending;
    int xterm_mouse, mouse_is_down, raw_mouse_reported_x, raw_mouse_reported_y;
    void (*wshell_input)(void *, int, int, const void *, int);
    void *wshell_input_context;
    UnicodeData *ucsdata;
    wchar_t *last_selected_text;
    size_t last_selected_len;
} Terminal;
typedef struct { Terminal *term; } WinGuiSeat;
typedef struct { uint64_t epoch; void *data; } WsClipboardRequest;
typedef struct { char *s; size_t len; } strbuf;
static int inputs, pastes, lastByte, lastUnicode;
static void term_update_raw_mouse_mode(Terminal *term) { (void)term; }
static strbuf *term_input_data_from_unicode(Terminal *term, const wchar_t *data, int len) {
    (void)term; strbuf *b = malloc(sizeof(*b)); b->s = malloc(len); b->len = len;
    for (int i = 0; i < len; i++) b->s[i] = (char)data[i];
    return b;
}
static strbuf *term_input_data_from_charset(Terminal *term, int cp, const void *data, int len) {
    (void)term; (void)cp; strbuf *b = malloc(sizeof(*b));
    b->s = malloc(len); b->len = len; memcpy(b->s, data, len); return b;
}
static void strbuf_free(strbuf *b) { free(b->s); free(b); }
static void term_keyinput_internal(Terminal *term, const void *data, int len, bool interactive) {
    (void)term; (void)interactive; inputs += len; lastByte = ((const char *)data)[0];
}
static void term_do_paste(Terminal *term, const wchar_t *data, size_t len) {
    (void)term; (void)data; (void)len; pastes++;
}
static void process_clipdata(WinGuiSeat *wgs, void *data, bool unicode) {
    (void)wgs; (void)data; pastes++; lastUnicode = unicode;
}
'''
        for signature in ('static void wshellInterruptMouse(', 'void term_keyinputw(',
                          'void term_keyinput(', 'static void paste_from_clip_local('):
            harness += function(terminal, signature) + '\n'
        harness += function(integration, 'static bool wookClipboardCurrent(')
        harness += '\n#define WM_GOT_CLIPDATA 1\nstatic int deliver(WinGuiSeat *wgs, bool wParam, uintptr_t lParam) { switch (1) {\n'
        harness += delivery + '\n} return -1; }\n'
        harness += r'''
int probe(int wide, int unicode, int interrupt) {
    UnicodeData ucs = {0}; Terminal term = {.ucsdata=&ucs, .wshell_local_paste_pending=true};
    WinGuiSeat seat = {&term}; WsClipboardRequest old = {term.wshell_paste_epoch, NULL};
    inputs = pastes = lastByte = 0;
    char key = interrupt ? 3 : 'c'; wchar_t wkey = key;
    if (wide) term_keyinputw(&term, &wkey, 1);
    else term_keyinput(&term, -1, &key, 1);
    if (inputs != 1 || lastByte != key) return 1;
    deliver(&seat, unicode, (uintptr_t)&old);
    paste_from_clip_local(&term);
    if (pastes != (interrupt ? 0 : 2)) return 2;
    // A later deliberate paste must still work, including ANSI clipboard data.
    WsClipboardRequest fresh = {term.wshell_paste_epoch, NULL};
    int before = pastes;
    deliver(&seat, unicode, (uintptr_t)&fresh);
    if (pastes != before + 1 || lastUnicode != unicode) return 3;
    term.wshell_local_paste_pending = 2;
    paste_from_clip_local(&term);
    paste_from_clip_local(&term); // Two deliberate requests must remain two pastes.
    paste_from_clip_local(&term); // An old queued callback must not duplicate it.
    if (pastes != before + 3) return 4;
    return 0;
}
'''
        (ROOT / 'build').mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='clipboard-interrupt-', dir=ROOT / 'build')
        folder = Path(cls.temp.name)
        (folder / 'test.c').write_text(harness)
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

    def test_delayed_paste_is_cancelled_but_new_paste_is_allowed(self):
        for wide in (0, 1):
            for unicode in (0, 1):
                with self.subTest(wide=wide, unicode=unicode):
                    self.assertEqual(self.library.probe(wide, unicode, 1), 0)

    def test_ordinary_typing_does_not_invalidate_clipboard_requests(self):
        for wide in (0, 1):
            for unicode in (0, 1):
                self.assertEqual(self.library.probe(wide, unicode, 0), 0)
