"""Check the menu/terminal input boundary shared by both Windows processes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MenuKeysTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler required')
        (ROOT / 'build').mkdir(exist_ok=True)
        cls.folder = tempfile.TemporaryDirectory(prefix='menu-keys-', dir=ROOT / 'build')
        cls.addClassCleanup(cls.folder.cleanup)
        folder = Path(cls.folder.name)
        source = folder / 'probe.c'
        source.write_text('''#include "menu_keys.h"
#include <stdio.h>
int main(void) {
    unsigned key; int control, shift, alt;
    while (scanf("%u %d %d %d", &key, &control, &shift, &alt) == 4)
        printf("%d\\n", wsMenuAccessKey(key, control, shift, alt));
    return 0;
}
''')
        cls.probe = folder / 'probe'
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                        str(source), '-o', str(cls.probe)], check=True)

    def resolve(self, cases):
        data = ''.join(' '.join(map(str, case)) + '\n' for case in cases)
        return [int(value) for value in subprocess.check_output([str(self.probe)], input=data, text=True).splitlines()]

    def test_f10_and_all_menu_mnemonics(self):
        keys = 'FHSVTEfhsvte'
        self.assertEqual(self.resolve([(0x79, 0, 0, 0)] + [(ord(key), 0, 0, 1) for key in keys]),
                         [0] + [ord(key.upper()) for key in keys])

    def test_altgr_and_modified_function_keys_reach_terminal(self):
        cases = [(key, control, shift, alt) for key in [0x79, *map(ord, 'FHSVTE')]
                 for control, shift, alt in [(1, 0, 1), (1, 1, 1), (0, 1, 1), (1, 0, 0), (0, 1, 0)]]
        self.assertEqual(self.resolve(cases), [-1] * len(cases))

    def test_typing_tab_shortcuts_and_other_terminal_keys_are_preserved(self):
        cases = [(key, 0, 0, 0) for key in range(32, 127) if key != 0x79]
        cases += [(ord(key), 0, 0, 1) for key in '123456789ABDGIJKLMNO PQRUWXYZ']
        cases += [(key, 0, 0, 0) for key in [0x70, 0x78, 0x7a, 0x7b]]
        self.assertEqual(self.resolve(cases), [-1] * len(cases))
