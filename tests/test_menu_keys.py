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
    unsigned key; int control, shift, alt, character;
    while (scanf("%u %d %d %d %d", &key, &control, &shift, &alt, &character) == 5)
        printf("%d\\n", character ? wsMenuAccessChar(key, control, shift) : wsMenuAccessKey(key, control, shift, alt));
    return 0;
}
''')
        cls.probe = folder / 'probe'
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                        str(source), '-o', str(cls.probe)], check=True)

    def resolve(self, cases, character=False):
        data = ''.join(' '.join(map(str, (*case, int(character)))) + '\n' for case in cases)
        return [int(value) for value in subprocess.check_output([str(self.probe)], input=data, text=True).splitlines()]

    def test_f10_and_all_menu_mnemonics(self):
        keys = 'FHSVTE'
        self.assertEqual(self.resolve([(0x79, 0, 0, 0)] + [(ord(key), 0, 0, 1) for key in keys]),
                         [0] + [ord(key.upper()) for key in keys])

    def test_alt_function_keys_and_numpad_are_not_lowercase_letters(self):
        keys = [*range(0x70, 0x88), *range(0x60, 0x6a)]  # VK_F1..F24, VK_NUMPAD0..9
        self.assertEqual(self.resolve([(key, 0, 0, 1) for key in keys]), [-1] * len(keys))

    def test_syschar_mnemonics_normalize_text_without_changing_virtual_keys(self):
        keys = 'FHSVTEfhsvte'
        self.assertEqual(self.resolve([(ord(key), 0, 0, 1) for key in keys], character=True),
                         [ord(key.upper()) for key in keys])
        cases = [(ord(key), control, shift, 1) for key in keys for control, shift in [(1, 0), (0, 1), (1, 1)]]
        self.assertEqual(self.resolve(cases, character=True), [-1] * len(cases))

    def test_altgr_and_modified_function_keys_reach_terminal(self):
        cases = [(key, control, shift, alt) for key in [0x79, *map(ord, 'FHSVTE')]
                 for control, shift, alt in [(1, 0, 1), (1, 1, 1), (0, 1, 1), (1, 0, 0), (0, 1, 0)]]
        self.assertEqual(self.resolve(cases), [-1] * len(cases))

    def test_typing_tab_shortcuts_and_other_terminal_keys_are_preserved(self):
        cases = [(key, 0, 0, 0) for key in range(32, 127) if key != 0x79]
        cases += [(ord(key), 0, 0, 1) for key in '123456789ABDGIJKLMNO PQRUWXYZ']
        cases += [(key, 0, 0, 0) for key in [0x70, 0x78, 0x7a, 0x7b]]
        self.assertEqual(self.resolve(cases), [-1] * len(cases))
