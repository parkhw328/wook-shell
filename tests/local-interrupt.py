"""Windows regression: sample output copy, right-click paste and actual Ctrl+C.

Runs the production terminal engine in standalone windows, not the workspace's
embedded focus routing. Only synthetic files and a loopback TCP listener are used.
No samples are copied into the repository or distribution. Clipboard text is
restored; run with a text-only clipboard and an unlocked desktop.
Also checks immediate/multiline/partial paste, raw input, fresh paste and exit.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
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
            (self.user, 'ShowWindow', [W.HWND, C.c_int], W.BOOL),
            (self.user, 'SetWindowPos', [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT], W.BOOL),
            (self.user, 'GetWindowRect', [W.HWND, C.POINTER(W.RECT)], W.BOOL),
            (self.user, 'WindowFromPoint', [W.POINT], W.HWND),
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

    def paste(self, hwnd):
        point = 20 | (20 << 16)
        self.user.SendMessageW(hwnd, 0x204, 2, point)
        self.user.SendMessageW(hwnd, 0x205, 0, point)

    def copy_output(self, hwnd):
        class GuiInfo(C.Structure):
            _fields_ = [('size', W.DWORD), ('flags', W.DWORD)] + [
                (name, W.HWND) for name in ('active', 'focus', 'capture', 'menu', 'move', 'caret')
            ] + [('rect', W.RECT)]

        info = GuiInfo()
        info.size = C.sizeof(info)
        thread = self.user.GetWindowThreadProcessId(hwnd, None)
        if not self.user.GetGUIThreadInfo(thread, C.byref(info)) or info.caret != hwnd:
            raise RuntimeError('Cannot query the test terminal character geometry')
        width, height = info.rect.right - info.rect.left, info.rect.bottom - info.rect.top
        if width <= 0 or height <= 0 or info.rect.top < height:
            raise RuntimeError('No sample output row available to select')
        y = info.rect.top - height // 2
        start = (2 + width // 2) | (y << 16)
        end = (2 + width // 2) | ((info.rect.top + height // 2) << 16)
        self.user.SendMessageW(hwnd, 0x201, 1, start)
        self.user.SendMessageW(hwnd, 0x200, 1, end)
        self.user.SendMessageW(hwnd, 0x202, 0, end)
        return self.clipboard(hwnd)

    def capture(self, hwnd, filename):
        # Capture only this diagnostic window, including when another app covers it.
        gdi = C.WinDLL('gdi32')
        for name, args, result in [
            ('CreateCompatibleDC', [W.HDC], W.HDC),
            ('CreateCompatibleBitmap', [W.HDC, C.c_int, C.c_int], W.HANDLE),
            ('SelectObject', [W.HDC, W.HANDLE], W.HANDLE),
            ('DeleteObject', [W.HANDLE], W.BOOL),
            ('DeleteDC', [W.HDC], W.BOOL),
            ('GetDIBits', [W.HDC, W.HANDLE, W.UINT, W.UINT, C.c_void_p, C.c_void_p, W.UINT], C.c_int),
        ]:
            getattr(gdi, name).argtypes, getattr(gdi, name).restype = args, result
        self.user.GetWindowDC.argtypes, self.user.GetWindowDC.restype = [W.HWND], W.HDC
        self.user.ReleaseDC.argtypes = [W.HWND, W.HDC]
        self.user.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
        bounds = W.RECT()
        self.user.GetWindowRect(hwnd, C.byref(bounds))
        width, height = bounds.right - bounds.left, bounds.bottom - bounds.top
        screen = self.user.GetWindowDC(hwnd)
        dc = gdi.CreateCompatibleDC(screen)
        bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
        previous = gdi.SelectObject(dc, bitmap)
        try:
            if not self.user.PrintWindow(hwnd, dc, 2):
                raise RuntimeError('Cannot capture the test terminal')
            gdi.SelectObject(dc, previous)
            header = struct.pack('<IiiHHIIiiII', 40, width, height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
            info = C.create_string_buffer(header)
            pixels = C.create_string_buffer(width * height * 4)
            if gdi.GetDIBits(dc, bitmap, 0, height, pixels, info, 0) != height:
                raise RuntimeError('Cannot read captured terminal pixels')
            filename.write_bytes(struct.pack('<2sIHHI', b'BM', 54 + len(pixels.raw), 0, 0, 54) + header + pixels.raw)
        finally:
            gdi.SelectObject(dc, previous)
            gdi.DeleteObject(bitmap)
            gdi.DeleteDC(dc)
            self.user.ReleaseDC(hwnd, screen)

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

        self.user.SendInput.argtypes = [W.UINT, C.POINTER(Input), C.c_int]
        self.user.ShowWindow(hwnd, 9)
        self.user.SetForegroundWindow(hwnd)
        if self.user.GetForegroundWindow() != hwnd:
            # Background launchers cannot always activate a window. Click only
            # the verified caption of this test window, as the focus tests do.
            self.user.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x13)
            try:
                bounds = W.RECT()
                self.user.GetWindowRect(hwnd, C.byref(bounds))
                point = W.POINT(bounds.left + (bounds.right - bounds.left) // 3, bounds.top + 16)
                if self.user.WindowFromPoint(point) != hwnd:
                    raise RuntimeError('Test caption is not available for native input')
                click = (Input * 3)()
                left, top, width, height = [self.user.GetSystemMetrics(i) for i in (76, 77, 78, 79)]
                click[0].payload.mouse.x = (point.x - left) * 65535 // (width - 1)
                click[0].payload.mouse.y = (point.y - top) * 65535 // (height - 1)
                for item, flags in zip(click, (0xC001, 2, 4)):
                    item.payload.mouse.flags = flags
                if self.user.SendInput(3, click, C.sizeof(Input)) != 3:
                    raise C.WinError(C.get_last_error())
                until(lambda: self.user.GetForegroundWindow() == hwnd, 'foreground test terminal', 3)
            finally:
                self.user.SetWindowPos(hwnd, -2, 0, 0, 0, 0, 0x13)
        until(lambda: self.user.GetForegroundWindow() == hwnd, 'foreground test terminal', 3)
        keys = (Input * 4)()
        for item, vk, flags in zip(keys, (0x11, ord('C'), ord('C'), 0x11), (0, 0, 2, 2)):
            item.type = 1
            item.payload.key.vk, item.payload.key.flags = vk, flags
        if self.user.SendInput(4, keys, C.sizeof(Input)) != 4:
            raise C.WinError(C.get_last_error())


def session(folder, executable, sample, port, mode, native):
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
        'CloseOnExit': '2', 'WookMouseClipboard': '1', 'WarnOnClose': '0',
    }
    contents = 'WS1\n' + ''.join(k.encode().hex() + '=' + v.encode().hex() + '\n' for k, v in settings.items())
    (data / 'sessions' / (b'local'.hex() + '.ws')).write_bytes(contents.encode('ascii'))
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
        payload = 'echo pasted>paste-executed\r\n'
        if mode == 'multiline':
            payload *= 1000
        elif mode == 'partial':
            payload = payload.rstrip('\r\n')
        elif mode == 'raw':
            payload = 'raw-paste\r\n'
        native.clipboard(hwnd, payload)
        command = f'"{sample}" -f "{fixture}"' if sample.name.lower() == 'tail.exe' else f'"{sample}" -t 127.0.0.1 {port}'
        if mode == 'raw':
            command = f'"{sample}"'
        native.text(hwnd, command + '\r')
        expected = b'output-replayed' if sample.name.lower() == 'tail.exe' else b'Probing'
        if mode == 'raw':
            expected = b'RAW-READY'
        until(lambda: expected in log.read_bytes(), 'sample output')
        if mode == 'copy':
            # Let the output update the caret before selecting its previous row.
            time.sleep(0.15)
            payload = native.copy_output(hwnd)
            if expected.decode('ascii') not in payload:
                raise RuntimeError('Mouse drag did not copy the sample output: ' + repr(payload))
        if mode != 'plain':
            native.paste(hwnd)
            if mode != 'immediate':
                time.sleep(0.3)
        pasted_before = paste_marker.exists()
        before_interrupt = log.stat().st_size
        native.interrupt(hwnd)
        if mode == 'raw':
            (folder / 'raw-read-now').write_bytes(b'read')
        time.sleep(1.5)
        native.capture(hwnd, folder / 'after-interrupt.bmp')
        after_interrupt = log.read_bytes()[before_interrupt:]
        # Ignore rendering sequences only in test evidence; production input
        # and output are never rewritten. A copied output line must not appear
        # as a command at the next prompt, even if that command would just fail.
        visible = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]|\x1b\].*?(?:\x07|\x1b\\)',
                         b'', after_interrupt, flags=re.DOTALL)
        copied_replayed = mode == 'copy' and b'>' + b''.join(payload.encode().split()) in b''.join(visible.split())
        native.text(hwnd, 'echo ready>cmd-ready\r')
        returned = True
        try:
            until(lambda: ready.exists() and ready.read_bytes().strip() == b'ready',
                  'CMD accepts a fresh command after Ctrl+C', 8)
        except RuntimeError:
            returned = False
        result = {
            'sample': sample.name, 'mode': mode, 'cmdReturned': returned,
            'copiedInputReplayed': copied_replayed,
            'outputExecuted': output_marker.exists(), 'pasteExecutedBeforeInterrupt': pasted_before,
            'pasteExecutedAfterInterrupt': paste_marker.exists() and not pasted_before,
            'clipboardUnchanged': native.clipboard(hwnd) == payload,
        }
        if mode == 'raw':
            raw = folder / 'raw-input.bin'
            result['rawInputPreserved'] = raw.exists() and raw.read_bytes() == b'raw-paste\r\x03'
        native.clipboard(hwnd, 'echo fresh>fresh-paste\r\n')
        native.paste(hwnd)
        fresh = folder / 'fresh-paste'
        try:
            until(lambda: fresh.exists() and fresh.read_bytes().strip() == b'fresh', 'fresh paste after interrupt', 5)
            result['freshPasteWorks'] = True
        except RuntimeError:
            result['freshPasteWorks'] = False
        native.interrupt(hwnd)
        time.sleep(0.1)
        native.text(hwnd, 'echo repeated>repeat-interrupt\r')
        repeat = folder / 'repeat-interrupt'
        try:
            until(lambda: repeat.exists() and repeat.read_bytes().strip() == b'repeated', 'repeated Ctrl+C', 5)
            native.clipboard(hwnd, backup)
            backup = None
            native.text(hwnd, 'exit\r')
            result['cleanExit'] = child.wait(timeout=5) == 0
        except (RuntimeError, subprocess.TimeoutExpired):
            result['cleanExit'] = False
        # Ctrl+C must discard unread input instead of replaying it at the CMD prompt.
        result['passed'] = (returned and not result['outputExecuted'] and result['clipboardUnchanged']
                            and not paste_marker.exists() and result['freshPasteWorks']
                            and result.get('rawInputPreserved', True) and result['cleanExit'] and not copied_replayed)
        return result
    finally:
        try:
            if backup is not None:
                native.clipboard(hwnd if child.poll() is None else None, backup)
        finally:
            # Kill only this diagnostic's process tree, including its CMD and sample.
            if child.poll() is None:
                subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
            child.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/native/wShell.exe')
    parser.add_argument('--raw-probe', type=Path, default=ROOT / 'build/native/local-input-probe.exe')
    parser.add_argument('--modes', nargs='+', choices=('plain', 'paste', 'immediate', 'multiline', 'partial', 'copy'),
                        default=['plain', 'paste', 'immediate', 'multiline', 'partial', 'copy'])
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Requires Windows and an unlocked interactive desktop')
    executable = args.exe.resolve(strict=True)
    samples = [(ROOT / 'resources' / name).resolve(strict=True) for name in ('tail.exe', 'tcping.exe')]
    raw_probe = args.raw_probe.resolve(strict=True)
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
    results = {'exe': str(executable), 'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [executable, *samples, raw_probe]}, 'cases': []}
    try:
        for sample in [*samples, raw_probe]:
            for mode in (['raw'] if sample == raw_probe else args.modes):
                try:
                    result = session(artifact / (sample.stem + '-' + mode),
                                     executable, sample, listener.getsockname()[1], mode, native)
                except Exception as error:
                    result = {'sample': sample.name, 'mode': mode, 'passed': False, 'error': str(error)}
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
