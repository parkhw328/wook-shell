"""Run the production key identity functions with PuTTY's native crypto libraries.

This covers parsing and key matching on Linux, not Windows ACL, dialogs or SSH.
The Windows integration test exercises the full registration/connection path.
"""
import base64
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'linux', 'Native crypto harness runs on Linux; Windows has integration tests')
class PublicKeyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc, cls.cxx, cmake, keygen = (shutil.which(tool) for tool in ('cc', 'c++', 'cmake', 'ssh-keygen'))
        pinned_cmake = ROOT / '.tools/cmake-4.4.4-linux-x86_64/bin/cmake'
        if not cmake and pinned_cmake.is_file():
            cmake = str(pinned_cmake)
        source = ROOT / '.deps/putty'
        if not all((cls.cc, cls.cxx, cmake, keygen)) or not (source / 'sshpubk.c').exists():
            raise unittest.SkipTest('Host C/C++ compilers, CMake, ssh-keygen and bootstrapped PuTTY required')
        (ROOT / 'build').mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='public-key-', dir=ROOT / 'build')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.folder = Path(cls.temp.name)
        native = cls.folder / 'putty'
        cls.run_command([cmake, '-S', str(source), '-B', str(native),
                         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON'])
        cls.run_command([cmake, '--build', str(native), '--target', 'keygen', 'crypto', 'utils', '--parallel', '4'])
        code = (ROOT / 'src/keys.c').read_text()
        # Compile the actual production functions, with only the platform path
        # adapter and test-only PPK writer supplied by the native harness.
        functions = code[code.index('int wsKeyInspect('):code.index('static PSECURITY_DESCRIPTOR privateDescriptor(')]
        functions += code[code.index('void wsKeyFree('):]
        bridge = r'''
#include "putty.h"
#include "ssh.h"
#include "keys.h"
#include <wchar.h>
struct WsKey { ssh2_userkey *key; };
struct WsPublicKey { strbuf *blob; };
static char keyError[256];
static void fail(const char *error) { snprintf(keyError, sizeof(keyError), "%s", error ? error : "Key error"); }
const char *wsKeyError(void) { return keyError; }
static WsKey *wrap(ssh2_userkey *key) { WsKey *out = snew(WsKey); out->key = key; return out; }
static Filename *filename_from_wstr(const wchar_t *path) {
    size_t length = wcstombs(NULL, path, 0); assert(length != (size_t)-1);
    char *name = snewn(length + 1, char); wcstombs(name, path, length + 1);
    Filename *file = filename_from_str(name); sfree(name); return file;
}
void random_read(void *buffer, size_t size) {
    FILE *file = fopen("/dev/urandom", "rb"); assert(file);
    size_t read = fread(buffer, 1, size, file); assert(read == size); fclose(file);
}
void out_of_memory(void) { abort(); }
void old_keyfile_warning(void) {}
/* Test adapter for the Windows ACL/atomic writer. Crypto stays in PuTTY. */
int wsKeySave(WsKey *key, const wchar_t *path, const char *passphrase) {
    Filename *file = filename_from_wstr(path);
    bool ok = ppk_save_f(file, key->key, *passphrase ? passphrase : NULL, &ppk_save_default_parameters);
    filename_free(file); return ok;
}
'''
        harness = cls.folder / 'probe.c'
        harness.write_text(bridge + functions)
        libraries = [next(native.rglob(name)) for name in ('libkeygen.a', 'libcrypto.a', 'libutils.a', 'libcharset.a')]
        output = cls.folder / 'public-key.so'
        cls.run_command([cls.cc, '-shared', '-fPIC', '-DHAVE_CMAKE_H', '-I' + str(source),
                         '-I' + str(source / 'unix'), '-I' + str(source / 'charset'), '-I' + str(native / 'CMakeFiles'),
                         '-I' + str(ROOT / 'src'), str(harness), str(source / 'sshpubk.c'),
                         '-Wl,--start-group', *map(str, libraries), '-Wl,--end-group', '-ldl', '-lm', '-o', str(output)])
        cls.lib = ctypes.CDLL(str(output))
        signatures = {
            'wsKeyInspect': ([ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)], ctypes.c_int),
            'wsKeyLoad': ([ctypes.c_wchar_p, ctypes.c_char_p], ctypes.c_void_p),
            'wsKeyFree': ([ctypes.c_void_p], None),
            'wsPublicKeyLoad': ([ctypes.c_wchar_p], ctypes.c_void_p),
            'wsPublicKeyFree': ([ctypes.c_void_p], None),
            'wsPublicKeyMatchesKey': ([ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            'wsPublicKeyMatchesFile': ([ctypes.c_void_p, ctypes.c_wchar_p], ctypes.c_int),
            'wsKeySave': ([ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_char_p], ctypes.c_int),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(cls.lib, name); function.argtypes = arguments; function.restype = result
        cls.keys = []
        for algorithm in ('rsa', 'ecdsa', 'ed25519'):
            file = cls.folder / algorithm
            options = ['-b', '2048', '-m', 'PEM'] if algorithm == 'rsa' else []
            cls.run_command([keygen, '-q', '-t', algorithm, *options, '-N', 'fixture-passphrase', '-f', str(file)])
            cls.keys.append(file)
        cls.other = cls.folder / 'other-ed25519'
        cls.run_command([keygen, '-q', '-t', 'ed25519', '-N', 'fixture-passphrase', '-f', str(cls.other)])
        cls.build_resolver()

    @classmethod
    def build_resolver(cls):
        # Compile the complete production resolver. Only Windows UI/storage and
        # wide native-path boundaries are adapted; directory search, matching,
        # passphrase handling and registration decisions are production code.
        code = (ROOT / 'src/key_import.cpp').read_text()
        code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include'))
        bridge = r'''
#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "keys.h"
using HWND = void *;
namespace wook {
std::string utf8(const std::wstring &text) { return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(text); }
std::wstring wide(const std::string &text) { return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(text); }
}
namespace ui { void error(HWND, const std::exception &) {} }
void SecureZeroMemory(void *buffer, size_t size) { volatile char *p = (volatile char *)buffer; while (size--) *p++ = 0; }
struct GUID { unsigned value; };
int CoCreateGuid(GUID *id) { static unsigned value = 0; id->value = ++value; return 0; }
bool FAILED(int code) { return code != 0; }
int StringFromGUID2(GUID id, wchar_t *out, int capacity) { return swprintf(out, capacity, L"{00000000-0000-0000-0000-%012u}", id.value) + 1; }
int _wcsicmp(const wchar_t *a, const wchar_t *b) { return wcscasecmp(a, b); }
int _wcsicmp(const char *a, const char *b) { return _wcsicmp(wook::wide(a).c_str(), wook::wide(b).c_str()); }
int _wcsicmp(const char *a, const wchar_t *b) { return _wcsicmp(wook::wide(a).c_str(), b); }
int wsKeySave(WsKey *key, const char *path, const char *secret) { return wsKeySave(key, wook::wide(path).c_str(), secret); }
static std::wstring testRoot;
static int promptCount = 0;
wchar_t *wsRoot() { return wcsdup(testRoot.c_str()); }
int wsPrompt(HWND, const char *, const char *, int, char *, int) { ++promptCount; return 0; }
std::vector<std::wstring> registeredPrivateKeys();
std::wstring registerPrivateKey(WsKey *, const std::wstring &, const char *);
std::wstring preparePrivateKey(HWND, const std::wstring &, const char * = nullptr);
'''
        exports = r'''
static std::wstring result;
static std::string error;
extern "C" void testSetRoot(const wchar_t *path) { testRoot = path; promptCount = 0; }
extern "C" int testPromptCount() { return promptCount; }
extern "C" const char *testError() { return error.c_str(); }
extern "C" const wchar_t *testPrepare(const wchar_t *path, const char *supplied) {
    try { error.clear(); result = preparePrivateKey(nullptr, path, supplied); return result.c_str(); }
    catch (const std::exception &e) { error = e.what(); return nullptr; }
}
'''
        harness = cls.folder / 'resolver.cpp'; harness.write_text(bridge + code + exports)
        output = cls.folder / 'resolver.so'
        cls.run_command([cls.cxx, '-std=c++20', '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                         str(harness), '-L' + str(cls.folder), '-l:public-key.so', '-Wl,-rpath,$ORIGIN', '-o', str(output)])
        cls.resolver = ctypes.CDLL(str(output))
        cls.resolver.testSetRoot.argtypes = [ctypes.c_wchar_p]
        cls.resolver.testSetRoot.restype = None
        cls.resolver.testPrepare.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p]
        cls.resolver.testPrepare.restype = ctypes.c_wchar_p
        cls.resolver.testError.restype = ctypes.c_char_p

    def setUp(self):
        self.case = self.folder / self._testMethodName
        self.case.mkdir()
        self.resolver.testSetRoot(str(self.case / 'data'))

    def prepare(self, path, passphrase=None):
        return self.resolver.testPrepare(str(path), passphrase)

    def registered(self):
        return list((self.case / 'data/keys').glob('*.ppk'))

    @staticmethod
    def run_command(command):
        environment = os.environ.copy()
        host_libraries = ROOT / '.tools/linux-host-libs'
        if host_libraries.is_dir():
            environment['LD_LIBRARY_PATH'] = str(host_libraries) + os.pathsep + environment.get('LD_LIBRARY_PATH', '')
        result = subprocess.run(command, capture_output=True, text=True, env=environment)
        if result.returncode:
            raise AssertionError(result.stdout[-3000:] + result.stderr[-3000:])
        return result.stdout

    def load_public(self, path):
        key = self.lib.wsPublicKeyLoad(str(path))
        self.assertTrue(key, 'Valid public key must load')
        self.addCleanup(self.lib.wsPublicKeyFree, key)
        return key

    def load_private(self, path):
        key = self.lib.wsKeyLoad(str(path), b'fixture-passphrase')
        self.assertTrue(key, 'Private key must load with its passphrase')
        self.addCleanup(self.lib.wsKeyFree, key)
        return key

    def test_supported_algorithms_and_encrypted_ppk(self):
        for path in self.keys:
            with self.subTest(algorithm=path.name):
                public = self.load_public(path.with_suffix('.pub'))
                private = self.load_private(path)
                self.assertEqual(self.lib.wsPublicKeyMatchesKey(public, private), 1)
                ppk = path.with_suffix('.ppk')
                self.assertTrue(self.lib.wsKeySave(private, str(ppk), b'fixture-passphrase'))
                self.assertEqual(self.lib.wsPublicKeyMatchesFile(public, str(ppk)), 1)
                self.assertFalse(self.lib.wsKeyLoad(str(ppk), b'incorrect'))

    def test_other_key_is_rejected(self):
        public = self.load_public(self.keys[0].with_suffix('.pub'))
        self.assertEqual(self.lib.wsPublicKeyMatchesKey(public, self.load_private(self.keys[1])), 0)
        self.assertEqual(self.lib.wsPublicKeyMatchesFile(public, str(self.keys[1].with_suffix('.pub'))), 0)
        self.assertEqual(self.lib.wsPublicKeyMatchesKey(self.load_public(self.keys[2].with_suffix('.pub')), self.load_private(self.other)), 0)

    def test_comments_extensions_and_crlf_do_not_change_identity(self):
        path = self.folder / 'renamed.key'
        algorithm, blob, *_ = self.keys[0].with_suffix('.pub').read_text().split()
        path.write_bytes(f'{algorithm} {blob} another comment\r\n'.encode())
        self.assertEqual(self.lib.wsKeyInspect(str(path), None), 3)
        self.assertEqual(self.lib.wsPublicKeyMatchesKey(self.load_public(path), self.load_private(self.keys[0])), 1)
        self.assertFalse(self.lib.wsKeyLoad(str(path), b''))

    def test_rfc4716_public_key(self):
        path = self.folder / 'rfc4716.key'
        path.write_text(self.run_command(['ssh-keygen', '-e', '-m', 'RFC4716', '-f', str(self.keys[2].with_suffix('.pub'))]))
        self.assertEqual(self.lib.wsPublicKeyMatchesKey(self.load_public(path), self.load_private(self.keys[2])), 1)

    def test_malformed_public_keys(self):
        algorithm, blob, *_ = self.keys[0].with_suffix('.pub').read_text().split()
        trailing = base64.b64encode(base64.b64decode(blob) + b'trailing').decode()
        for index, text in enumerate(('ssh-rsa AAAAB3NzaC1yc2E= truncated\n', f'ssh-ed25519 {blob}\n',
                                      f'{algorithm} {trailing}\n', 'ssh-ed25519 !!!!\n', 'not a key\n')):
            with self.subTest(index=index):
                path = self.folder / f'invalid-{index}.pub'; path.write_text(text)
                key = self.lib.wsPublicKeyLoad(str(path))
                if key:
                    self.lib.wsPublicKeyFree(key)
                self.assertFalse(key)

    def test_wrong_private_key_passphrase(self):
        for path in self.keys:
            encrypted = ctypes.c_int()
            self.assertEqual(self.lib.wsKeyInspect(str(path), ctypes.byref(encrypted)), 2)
            self.assertEqual(encrypted.value, 1)
            self.assertFalse(self.lib.wsKeyLoad(str(path), b'incorrect'))

    def test_resolution_library_precedes_folder_and_reuses_encrypted_key(self):
        library = self.case / 'data/keys'; library.mkdir(parents=True)
        ppk = library / 'unrelated-name.ppk'
        self.assertTrue(self.lib.wsKeySave(self.load_private(self.keys[0]), str(ppk), b'fixture-passphrase'))
        public = self.case / 'identity.pub'; shutil.copyfile(self.keys[0].with_suffix('.pub'), public)
        shutil.copyfile(self.keys[1], self.case / 'identity.pem')
        for _ in range(2):
            self.assertEqual(self.prepare(public), str(ppk))
        self.assertEqual(self.resolver.testPromptCount(), 0)
        self.assertEqual(len(self.registered()), 1)

    def test_resolution_imports_matching_encrypted_key(self):
        for index, name in ((0, 'identity.pem'), (2, 'identity')):
            with self.subTest(name=name):
                folder = self.case / str(index); folder.mkdir()
                public = folder / 'identity.PUB'; shutil.copyfile(self.keys[index].with_suffix('.pub'), public)
                private = folder / name; shutil.copyfile(self.keys[index], private)
                original = private.read_bytes()
                selected = self.prepare(public, b'fixture-passphrase')
                self.assertTrue(selected, self.resolver.testError())
                encrypted = ctypes.c_int()
                self.assertEqual(self.lib.wsKeyInspect(selected, ctypes.byref(encrypted)), 1)
                self.assertEqual(encrypted.value, 1)
                self.assertEqual(self.lib.wsPublicKeyMatchesKey(self.load_public(public), self.load_private(selected)), 1)
                self.assertEqual(self.prepare(public), selected)
                self.assertEqual(private.read_bytes(), original)
        self.assertEqual(len(self.registered()), 2)
        self.assertEqual(self.resolver.testPromptCount(), 0)

    def test_resolution_cancel_and_wrong_password_leave_no_entries(self):
        public = self.case / 'identity.pub'; shutil.copyfile(self.keys[2].with_suffix('.pub'), public)
        shutil.copyfile(self.keys[2], self.case / 'identity')
        self.assertEqual(self.prepare(public), '')
        self.assertEqual(self.resolver.testPromptCount(), 1)
        self.assertIsNone(self.prepare(public, b'incorrect'))
        self.assertEqual(self.registered(), [])

    def test_resolution_mismatch_and_missing_key_leave_no_entries(self):
        public = self.case / 'identity.pub'; shutil.copyfile(self.keys[2].with_suffix('.pub'), public)
        self.assertIsNone(self.prepare(public))
        shutil.copyfile(self.keys[0], self.case / 'identity.pem')
        self.assertIsNone(self.prepare(public, b'fixture-passphrase'))
        self.assertIn(b'No matching private key', self.resolver.testError())
        self.assertEqual(self.registered(), [])

    def test_resolution_unencrypted_key_with_different_name(self):
        public = self.case / 'renamed.key'; shutil.copyfile(self.keys[0].with_suffix('.pub'), public)
        private = self.case / 'other-name'; shutil.copyfile(self.keys[0], private); private.chmod(0o600)
        self.run_command(['ssh-keygen', '-p', '-P', 'fixture-passphrase', '-N', '', '-f', str(private)])
        selected = self.prepare(public)
        self.assertTrue(selected, self.resolver.testError())
        self.assertEqual(self.lib.wsPublicKeyMatchesFile(self.load_public(public), selected), 1)
        self.assertEqual(len(self.registered()), 1)

    def test_resolution_sibling_ppk_is_reused(self):
        public = self.case / 'identity.pub'; shutil.copyfile(self.keys[0].with_suffix('.pub'), public)
        ppk = self.case / 'another-name.ppk'
        self.assertTrue(self.lib.wsKeySave(self.load_private(self.keys[0]), str(ppk), b'fixture-passphrase'))
        self.assertEqual(self.prepare(public), str(ppk))
        self.assertEqual(self.resolver.testPromptCount(), 0)
        self.assertEqual(self.registered(), [])

    def test_resolution_does_not_prompt_for_unrelated_encrypted_keys(self):
        public = self.case / 'identity.pub'; shutil.copyfile(self.keys[0].with_suffix('.pub'), public)
        shutil.copyfile(self.keys[0], self.case / 'unrelated-name')
        self.assertIsNone(self.prepare(public))
        self.assertEqual(self.resolver.testPromptCount(), 0)
        self.assertEqual(self.registered(), [])

    def test_resolution_direct_private_input_still_works(self):
        self.assertEqual(self.prepare(''), '')
        selected = self.prepare(self.keys[0], b'fixture-passphrase')
        self.assertTrue(selected, self.resolver.testError())
        self.assertEqual(self.prepare(selected), selected)
        self.assertEqual(len(self.registered()), 1)
