"""Exercise the same integer formatter used by SFTP lists and progress."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NumberFormatTest(unittest.TestCase):
    def test_exact_grouping_through_uint64_max(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('Host C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='number-format-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text('''#include "number_format.hpp"
#include <iostream>
int main() {
    for (uint64_t value : {0ULL, 1ULL, 999ULL, 1000ULL, 1001ULL, 999999ULL,
                          1000000ULL, 4294967296ULL, 18446744073709551615ULL})
        std::wcout << wook::formatInteger(value) << L'\\n';
}
''')
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                            str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
            actual = subprocess.check_output([str(folder / 'test')], text=True).splitlines()
            self.assertEqual(actual, ['0', '1', '999', '1,000', '1,001', '999,999', '1,000,000',
                                      '4,294,967,296', '18,446,744,073,709,551,615'])
