"""Exercise download reservation policy with a simulated Windows filesystem.

This checks 64-bit sizes and error handling, not Windows disk performance.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SftpAllocationTest(unittest.TestCase):
    def test_reservation_sizes_and_failures(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('Host C++ compiler required')
        source = (ROOT / 'src/sftp.cpp').read_text()
        start = source.index('void reserveDownloadSpace(')
        function = source[start:source.index('\n}', start) + 2]
        harness = r'''
#include <cassert>
#include <cstdint>
#include <stdexcept>
using HANDLE = int;
using DWORD = unsigned long;
using LONGLONG = int64_t;
struct FILE_ALLOCATION_INFO { struct { LONGLONG QuadPart; } AllocationSize; };
enum { FileAllocationInfo, ERROR_SUCCESS=0, ERROR_INVALID_FUNCTION=1,
       ERROR_NOT_SUPPORTED=50, ERROR_INVALID_PARAMETER=87,
       ERROR_DISK_FULL=112, ERROR_ACCESS_DENIED=5 };
static DWORD error;
static int calls;
static int64_t reserved;
static DWORD GetLastError() { return error; }
static bool SetFileInformationByHandle(HANDLE file, int kind, FILE_ALLOCATION_INFO *info, unsigned size) {
    assert(file==42 && kind==FileAllocationInfo && size==sizeof(*info));
    ++calls; reserved=info->AllocationSize.QuadPart; return error==ERROR_SUCCESS;
}
// Timing/diagnostics may touch last-error after the allocation call.
struct TransferTiming { TransferTiming(const char *) {} ~TransferTiming() { error=999; } };
static void require(bool ok, const char *message) { if(!ok)throw std::runtime_error(message); }
''' + function + r'''
int main() {
    reserveDownloadSpace(42,0); assert(calls==0);
    for(uint64_t size : {1ULL,1536ULL*1024*1024+3,5ULL*1024*1024*1024}) {
        error=ERROR_SUCCESS; reserveDownloadSpace(42,size);
        assert(reserved==int64_t(size));
    }
    for(DWORD unsupported : {ERROR_INVALID_FUNCTION,ERROR_NOT_SUPPORTED,ERROR_INVALID_PARAMETER}) {
        error=unsupported; reserveDownloadSpace(42,1024);
    }
    for(DWORD failure : {ERROR_DISK_FULL,ERROR_ACCESS_DENIED}) {
        error=failure; bool failed=false;
        try { reserveDownloadSpace(42,1024); } catch(const std::runtime_error &) { failed=true; }
        assert(failed);
    }
    int previous=calls; bool failed=false;
    try { reserveDownloadSpace(42,UINT64_MAX); } catch(const std::runtime_error &) { failed=true; }
    assert(failed && calls==previous);
}
'''
        with tempfile.TemporaryDirectory(prefix='sftp-allocation-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text(harness)
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
            subprocess.run([str(folder / 'test')], check=True)
