"""Exercise the bridge after a transport queue drains without another sent event.

This is a host-side flow-control regression, not a Windows/SSH runtime test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SftpResumeTest(unittest.TestCase):
    def test_post_callback_check_resumes_stalled_input(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler required')
        source = (ROOT / 'patches/wook-sftp.h').read_text()
        start = source.index('static void wookSftpResumeInput(')
        function = source[start:source.index('\n}', start) + 2]
        # Use PuTTY's actual busy/defunct/backlog handling, with only its OS
        # event primitive replaced. Rechecking must never duplicate a read.
        source = (ROOT / '.deps/putty/windows/handle-io.c').read_text()
        start = source.index('static void handle_throttle(')
        throttle = source[start:source.index('\n}', start) + 2]
        harness = '''#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define MAX_BACKLOG 32768
struct handle_input { bool defunct, busy; void *ev_from_main; };
struct handle { struct handle_input input; };
typedef struct { size_t backlog; } Backend;
typedef struct { Backend *backend; } WinGuiSeat;
static bool wookSftp;
static struct handle *wookSftpInput;
static int reads;
static void SetEvent(void *event) { (void)event; reads++; }
static size_t backend_sendbuffer(Backend *backend) { return backend->backlog; }
''' + throttle + '''
static void handle_unthrottle(struct handle *handle, size_t backlog) {
    handle_throttle(&handle->input, backlog);
}
''' + function + '''
int main(void) {
    Backend backend = {65536}; WinGuiSeat wgs = {&backend};
    struct handle input = {0}; wookSftp = true; wookSftpInput = &input;
    wookSftpResumeInput(&wgs); assert(reads == 0);
    // The key-exchange queue drains after the last sent callback.
    backend.backlog = 0;
    wookSftpResumeInput(&wgs); assert(reads == 1 && input.input.busy);
    wookSftpResumeInput(&wgs); assert(reads == 1);
    input.input.busy = false; backend.backlog = MAX_BACKLOG;
    wookSftpResumeInput(&wgs); assert(reads == 1);
    backend.backlog = 0; input.input.defunct = true;
    wookSftpResumeInput(&wgs); assert(reads == 1);
    input.input.defunct = false; wookSftp = false;
    wookSftpResumeInput(&wgs); assert(reads == 1);
    wookSftp = true; wgs.backend = NULL;
    wookSftpResumeInput(&wgs); assert(reads == 1);
    wgs.backend = &backend; wookSftpInput = NULL;
    wookSftpResumeInput(&wgs); assert(reads == 1);
}
'''
        with tempfile.TemporaryDirectory(prefix='sftp-resume-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.c').write_text(harness)
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(folder / 'test.c'), '-o', str(folder / 'test')], check=True)
            subprocess.run([str(folder / 'test')], check=True)

    def test_rechecks_after_transport_callbacks_in_real_loop(self):
        source = (ROOT / '.deps/putty/windows/window.c').read_text()
        self.assertIn('run_toplevel_callbacks();\n        wookSftpResumeInput(wgs);', source)
