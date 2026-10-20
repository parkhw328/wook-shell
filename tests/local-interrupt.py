"""Windows-only sample diagnostic: actual Ctrl+C, with/without right-click paste.

Runs the production terminal engine in standalone windows, not the workspace's
embedded focus routing. Only synthetic files and a loopback TCP listener are used.
No samples are copied into the repository or distribution. Clipboard text is
restored; run with a text-only clipboard and an unlocked desktop.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def until(check, description, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.05)
    raise RuntimeError('Timed out: ' + description)


class Native:
    def __init__(self):
        self.user = C.WinDLL('user32', use_last_error=True)
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        for dll, name, args, result in [
            (self.user, 'SendMessageW', [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.LPARAM),
            (self.user, 'GetForegroundWindow', [], W.HWND),
            (self.user, 'IsWindowVisible', [W.HWND], W.BOOL),
            (self.user, 'SetForegroundWindow', [W.HWND], W.BOOL),
            (self.user, 'GetWindowThreadProcessId', [W.HWND, C.POINTER(W.DWORD)], W.DWORD),
            (self.user, 'OpenClipboard', [W.HWND], W.BOOL),
            (self.user, 'GetClipboardData', [W.UINT], W.HANDLE),
            (self.user, 'SetClipboardData', [W.UINT, W.HANDLE], W.HANDLE),
            (self.kernel, 'GlobalAlloc', [W.UINT, C.c_size_t], W.HANDLE),
            (self.kernel, 'GlobalLock', [W.HANDLE], C.c_void_p),
            (self.kernel, 'GlobalUnlock', [W.HANDLE], W.BOOL),
            (self.kernel, 'GlobalFree', [W.HANDLE], W.HANDLE),
        ]:
            function = getattr(dll, name)
            function.argtypes, function.restype = args, result

    def window(self, pid):
        found = []
        callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

        @callback_type
        def visit(hwnd, _):
            owner = W.DWORD()
            self.user.GetWindowThreadProcessId(hwnd, C.byref(owner))
            if owner.value == pid and self.user.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True

        self.user.EnumWindows(visit, 0)
        return found[0] if found else None

    def clipboard(self, hwnd, value=None):
        until(lambda: self.user.OpenClipboard(hwnd), 'open test clipboard', 3)
        try:
            if value is None:
                handle = self.user.GetClipboardData(13)  # CF_UNICODETEXT
                if not handle:
                    if self.user.CountClipboardFormats():
                        raise RuntimeError('Use an empty or text clipboard for this diagnostic')
                    return ''
                pointer = self.kernel.GlobalLock(handle)
                if not pointer:
                    raise C.WinError(C.get_last_error())
                try:
                    return C.wstring_at(pointer)
                finally:
                    self.kernel.GlobalUnlock(handle)
            text = C.create_unicode_buffer(value)
            handle = self.kernel.GlobalAlloc(2, C.sizeof(text))
            pointer = self.kernel.GlobalLock(handle)
            if not pointer:
                if handle:
                    self.kernel.GlobalFree(handle)
                raise C.WinError(C.get_last_error())
            C.memmove(pointer, text, C.sizeof(text))
            self.kernel.GlobalUnlock(handle)
            if not self.user.EmptyClipboard() or not self.user.SetClipboardData(13, handle):
                self.kernel.GlobalFree(handle)
                raise C.WinError(C.get_last_error())
        finally:
            self.user.CloseClipboard()

    def text(self, hwnd, text):
        # Commands use explicit WM_CHAR; Ctrl+C below uses OS keyboard routing.
        for value in text:
            self.user.SendMessageW(hwnd, 0x102, ord(value), 1)

    def interrupt(self, hwnd):
        class Key(C.Structure):
            _fields_ = [('vk', W.WORD), ('scan', W.WORD), ('flags', W.DWORD),
                        ('time', W.DWORD), ('extra', C.c_size_t)]

        class Mouse(C.Structure):
            _fields_ = [('x', W.LONG), ('y', W.LONG), ('data', W.DWORD),
                        ('flags', W.DWORD), ('time', W.DWORD), ('extra', C.c_size_t)]

        class Payload(C.Union):
            _fields_ = [('key', Key), ('mouse', Mouse)]

        class Input(C.Structure):
            _fields_ = [('type', W.DWORD), ('payload', Payload)]

        self.user.SetForegroundWindow(hwnd)
        until(lambda: self.user.GetForegroundWindow() == hwnd, 'foreground test terminal', 3)
        keys = (Input * 4)()
        for item, vk, flags in zip(keys, (0x11, ord('C'), ord('C'), 0x11), (0, 0, 2, 2)):
            item.type = 1
            item.payload.key.vk, item.payload.key.flags = vk, flags
        self.user.SendInput.argtypes = [W.UINT, C.POINTER(Input), C.c_int]
        if self.user.SendInput(4, keys, C.sizeof(Input)) != 4:
            raise C.WinError(C.get_last_error())


def session(folder, executable, sample, port, right_click, native):
    folder.mkdir()
    data = folder / 'data'
    (data / 'sessions').mkdir(parents=True)
    log = folder / 'terminal.log'
    output_marker, paste_marker, ready = [folder / name for name in ('output-executed', 'paste-executed', 'cmd-ready')]
    fixture = folder / 'tail.txt'
    fixture.write_text('echo output-replayed>output-executed\n', encoding='ascii')
    settings = {
        'HostName': 'cmd', 'RemoteCommand': f'"{os.environ["SystemRoot"]}\\System32\\cmd.exe" /D /Q',
        'LogType': '2', 'LogFileName': str(log), 'LogFlush': '1', 'LogHeader': '0',
        'CloseOnExit': '0', 'WookMouseClipboard': '1', 'WarnOnClose': '0',
    }
    contents = 'WS1\n' + ''.join(k.encode().hex() + '=' + v.encode().hex() + '\n' for k, v in settings.items())
    (data / 'sessions' / (b'local'.hex() + '.ws')).write_text(contents, encoding='ascii')
    env = {key: value for key, value in os.environ.items() if not key.startswith('WOOK_')}
    env['WOOK_DATA_DIR'] = str(data)
    child = subprocess.Popen([str(executable), '--terminal', '-load', 'local', '-wook-local'],
                             cwd=folder, env=env)
    backup = None
    hwnd = None
    try:
        hwnd = until(lambda: native.window(child.pid), 'terminal window')
        until(lambda: log.exists() and log.stat().st_size, 'initial CMD output')
        backup = native.clipboard(hwnd)
        native.clipboard(hwnd, 'echo pasted>paste-executed\r\n')
        command = f'"{sample}" -f "{fixture}"' if sample.name.lower() == 'tail.exe' else f'"{sample}" -t 127.0.0.1 {port}'
        native.text(hwnd, command + '\r')
        expected = b'output-replayed' if sample.name.lower() == 'tail.exe' else b'Probing'
        until(lambda: expected in log.read_bytes(), 'sample output')
        if right_click:
            point = 20 | (20 << 16)
            native.user.SendMessageW(hwnd, 0x204, 2, point)  # WM_RBUTTONDOWN
            native.user.SendMessageW(hwnd, 0x205, 0, point)
            time.sleep(0.3)
        pasted_before = paste_marker.exists()
        native.interrupt(hwnd)
        time.sleep(1.5)
        native.text(hwnd, 'echo ready>cmd-ready\r')
        returned = True
        try:
            until(ready.exists, 'CMD accepts a fresh command after Ctrl+C', 8)
        except RuntimeError:
            returned = False
        result = {
            'sample': sample.name, 'rightClick': right_click, 'cmdReturned': returned,
            'outputExecuted': output_marker.exists(), 'pasteExecutedBeforeInterrupt': pasted_before,
            'pasteExecutedAfterInterrupt': paste_marker.exists() and not pasted_before,
            'clipboardUnchanged': native.clipboard(hwnd) == 'echo pasted>paste-executed\r\n',
        }
        # A deliberate right-click may already have delivered input to ConPTY.
        # Record its timing rather than mislabel it as spontaneous Ctrl+C paste.
        result['passed'] = returned and not result['outputExecuted'] and result['clipboardUnchanged'] and (right_click or not paste_marker.exists())
        return result
    finally:
        try:
            if backup is not None:
                native.clipboard(hwnd, backup)
        finally:
            # Kill only this diagnostic's process tree, including its CMD and sample.
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
            child.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/native/wShell.exe')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Requires Windows and an unlocked interactive desktop')
    executable = args.exe.resolve(strict=True)
    samples = [(ROOT / 'resources' / name).resolve(strict=True) for name in ('tail.exe', 'tcping.exe')]
    artifact = ROOT / 'build' / ('local-interrupt-' + str(time.time_ns()))
    artifact.mkdir(parents=True)
    native = Native()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(0.2)
    stop = threading.Event()

    def accept():
        while not stop.is_set():
            try:
                connection, _ = listener.accept()
                connection.close()
            except socket.timeout:
                pass

    thread = threading.Thread(target=accept)
    thread.start()
    results = {'exe': str(executable), 'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [executable, *samples]}, 'cases': []}
    try:
        for sample in samples:
            for paste in (False, True):
                try:
                    result = session(artifact / (sample.stem + ('-paste' if paste else '-plain')),
                                     executable, sample, listener.getsockname()[1], paste, native)
                except Exception as error:
                    result = {'sample': sample.name, 'rightClick': paste, 'passed': False, 'error': str(error)}
                results['cases'].append(result)
    finally:
        stop.set()
        thread.join()
        listener.close()
        (artifact / 'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps({'artifact': str(artifact), **results}, indent=2))
    return 0 if all(case['passed'] for case in results['cases']) else 1


if __name__ == '__main__':
    sys.exit(main())
