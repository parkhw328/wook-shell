"""Record the tools and settings used for a particular Windows executable."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(build):
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line and ':' in line.split('=', 1)[0]:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    compiler = Path(cache['CMAKE_CXX_COMPILER'])
    suffix = '.exe' if platform.system() == 'Windows' else ''
    toolchain = compiler.parent.parent

    def version(tool):
        return subprocess.check_output([str(tool), '--version'], text=True, encoding='utf-8').splitlines()[0]

    commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    terminal = next(item for item in commands if item['file'].replace('\\', '/').endswith('/terminal/terminal.c'))
    if re.search(r'(?:^|\s)-DNDEBUG(?:\s|$)', terminal['command']):
        raise ValueError('PuTTY C assertions must remain enabled')
    info = {
        'version': (ROOT / 'VERSION').read_text().strip(),
        'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'sourceDirty': bool(subprocess.check_output(
            ['git', 'status', '--porcelain', '--', '.', ':(exclude)release'], cwd=ROOT, text=True).strip()),
        'host': platform.system().lower(),
        'compiler': version(compiler),
        'linker': version(compiler.parent / ('ld.lld' + suffix)),
        'cmake': version(Path(cache['CMAKE_COMMAND'])),
        'profileSha256': sha256(ROOT / 'scripts/windows-release.cmake'),
        'configuredFlags': {key: cache[key] for key in (
            'CMAKE_BUILD_TYPE', 'CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS', 'CMAKE_RC_FLAGS',
            'CMAKE_C_FLAGS_RELEASE', 'CMAKE_CXX_FLAGS_RELEASE', 'CMAKE_EXE_LINKER_FLAGS',
            'CMAKE_EXE_LINKER_FLAGS_RELEASE', 'CMAKE_INTERPROCEDURAL_OPTIMIZATION')},
        'puttyAssertions': 'enabled',
        'runtimeSha256': {name: sha256(toolchain / 'x86_64-w64-mingw32/lib' / name)
                          for name in ('libc++.a', 'libc++abi.a', 'libunwind.a', 'libmingw32.a', 'libmingwex.a')},
        'binarySha256': sha256(build / 'wShell.exe'),
    }
    (build / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Recorded build tools, release flags and executable hash: {build / "build-info.json"}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    record(parser.parse_args().build.resolve())
