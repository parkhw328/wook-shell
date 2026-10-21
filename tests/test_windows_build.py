"""Exercise release packaging and reused CMake caches without building the app."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class WindowsBuildTest(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='build-policy-', dir=ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def test_zip_does_not_inherit_binary_timestamp(self):
        binary = self.folder / 'input.exe'
        binary.write_bytes(b'isolated package fixture\n' * 500)
        archives = []
        for index, timestamp in enumerate((946684800, 1767225600)):
            os.utime(binary, (timestamp, timestamp))
            output = self.folder / str(index)
            subprocess.run([sys.executable, str(ROOT / 'scripts/package.py'),
                            '--binary', str(binary), '--output', str(output)],
                           check=True, capture_output=True)
            archive = next(output.glob('*.zip'))
            archives.append(archive.read_bytes())
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(bundle.namelist(), ['wShell.exe'])
                self.assertEqual(bundle.read('wShell.exe'), binary.read_bytes())
                self.assertEqual(bundle.infolist()[0].date_time, (1980, 1, 1, 0, 0, 0))
        self.assertEqual(*archives)

    def test_release_profile_replaces_stale_cache_flags(self):
        host, suffix = ('windows', '.exe') if os.name == 'nt' else ('linux', '')
        cmake = ROOT / f'.tools/cmake-4.4.4-{host}-x86_64/bin/cmake{suffix}'
        if not cmake.exists():
            self.skipTest('Pinned CMake required')
        source, build = self.folder / 'source', self.folder / 'cmake'
        source.mkdir()
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(Probe NONE)\n')
        configure = [str(cmake), '-S', str(source), '-B', str(build)]
        subprocess.run(configure + ['-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_C_FLAGS=-g',
            '-DCMAKE_CXX_FLAGS_RELEASE=-O0', '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-g',
            '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON'], check=True, capture_output=True)
        subprocess.run(configure + ['-C', str(ROOT / 'scripts/windows-release.cmake')],
                       check=True, capture_output=True)
        cache = (build / 'CMakeCache.txt').read_text()
        for expected in ('CMAKE_BUILD_TYPE:STRING=Release', 'CMAKE_C_FLAGS:STRING=\n',
                         'CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG',
                         'CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=\n',
                         'CMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=OFF', '--no-insert-timestamp'):
            self.assertIn(expected, cache)
