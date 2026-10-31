"""Run the shared navigation sizing policy on the host, including small windows."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NavigationTest(unittest.TestCase):
    def test_layout_preserves_workspace_and_restores_preference(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('Host C++ compiler required')
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='navigation-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text(r'''#include "navigation.hpp"
#include <cassert>
#include <climits>
int main() {
    for (int client = 920; client <= 3840; ++client) {
        for (int request : {INT_MIN, 0, 219, 220, 252, 400, 520, 521, INT_MAX}) {
            int width = wook::navigationWidth(request, client);
            assert(width >= 220 && width <= 520);
            assert(client - width >= 700);
        }
    }
    assert(wook::navigationWidth(420, 980) == 280);
    assert(wook::navigationWidth(420, 1400) == 420);
    assert(wook::navigationWidth(252, 1230) == 252);
    for (int client : {0, 1, 700, 919})
        assert(wook::navigationWidth(520, client) == 220);
}
''')
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src'), str(folder / 'test.cpp'),
                            '-o', str(folder / 'test')], check=True)
            subprocess.run([str(folder / 'test')], check=True)
