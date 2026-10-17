"""Exercise the same number formatters used by SFTP lists and progress."""
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

    def test_speed_grouping_rounding_and_locale(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('Host C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='number-format-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text(r'''#include "number_format.hpp"
#include <iostream>
struct DecimalComma : std::numpunct<wchar_t> {
    wchar_t do_decimal_point() const override { return L','; }
};
int main() {
    std::locale::global(std::locale(std::locale::classic(), new DecimalComma));
    for (double value : {0.0, 0.125, 999.0, 999.94, 999.96, 1000.0,
                         12345.625, 999999.96, 1234567890.25})
        std::wcout << wook::formatTransferSpeed(value) << L'\n';
}
''')
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                            str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
            actual = subprocess.check_output([str(folder / 'test')], text=True).splitlines()
            self.assertEqual(actual, ['0.0', '0.1', '999.0', '999.9', '1,000.0', '1,000.0',
                                      '12,345.6', '1,000,000.0', '1,234,567,890.2'])
