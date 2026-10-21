"""Exercise the C codec with a compressed archive and deterministic response delays.

The clock models SFTP response latency, not SSH, Windows, disk or real throughput.
All packets are replies to actual requests; a blocked window cannot read ahead.
"""
import ctypes
import os
import gzip
import hashlib
import heapq
import io
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
READ = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)
WRITE_AT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int)
PROGRESS = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64)


def archive_payload():
    # Incompressible, reproducible data inside a real tar.gz, without user files.
    payload = hashlib.shake_256(b"wShell SFTP pipeline regression").digest(8 * 1024 * 1024 + 31)
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode="w") as bundle:
        entry = tarfile.TarInfo("payload.bin")
        entry.size = len(payload)
        bundle.addfile(entry, io.BytesIO(payload))
    return gzip.compress(archive.getvalue(), mtime=0)


class DelayedServer:
    def __init__(self, payload, read_limit=32768, stragglers=True):
        self.payload = payload
        self.read_limit = read_limit
        self.stragglers = stragglers
        self.requests = bytearray()
        self.reply = bytearray()
        self.pending = []
        self.now = 0
        self.sequence = 0
        self.inflight = 0
        self.peak_inflight = 0
        self.output = bytearray(len(payload))
        self.position = 0
        self.samples = []
        self.error = None

    def enqueue(self, kind, request_id, payload=b"", delay=0):
        body = bytes([kind]) + struct.pack(">I", request_id) + payload
        self.sequence += 1
        heapq.heappush(self.pending, (self.now + delay, self.sequence,
                                     struct.pack(">I", len(body)) + body, kind == 103))

    def write(self, _, pointer, size):
        try:
            self.requests.extend(ctypes.string_at(pointer, size))
            while len(self.requests) >= 4:
                length = struct.unpack_from(">I", self.requests)[0]
                if len(self.requests) < length + 4:
                    break
                body = bytes(self.requests[4:4 + length])
                del self.requests[:4 + length]
                kind, request_id = struct.unpack_from(">BI", body)
                if kind == 1:
                    self.enqueue(2, 3)
                elif kind == 3:
                    self.enqueue(102, request_id, b"\0\0\0\x01h")
                elif kind == 5:
                    handle_length = struct.unpack_from(">I", body, 5)[0]
                    offset, count = struct.unpack_from(">QI", body, 9 + handle_length)
                    data = self.payload[offset:offset + min(count, self.read_limit)]
                    if not data:
                        raise AssertionError("READ beyond advertised file size")
                    delay = 300 if self.stragglers and offset % (64 * 32768) == 0 else 40
                    self.enqueue(103, request_id, struct.pack(">I", len(data)) + data, delay)
                    self.inflight += 1
                    self.peak_inflight = max(self.peak_inflight, self.inflight)
                elif kind == 4:
                    if self.inflight:
                        raise AssertionError("CLOSE with outstanding reads")
                    self.enqueue(101, request_id, struct.pack(">III", 0, 0, 0))
                else:
                    raise AssertionError(f"Unexpected request: {kind}")
            return size
        except Exception as error:
            self.error = error
            return -1

    def read(self, _, pointer, size):
        try:
            if not self.reply:
                when, _, reply, is_data = heapq.heappop(self.pending)
                self.now = max(self.now, when)
                self.reply.extend(reply)
                if is_data:
                    self.inflight -= 1
            count = min(size, len(self.reply))
            ctypes.memmove(pointer, bytes(self.reply[:count]), count)
            del self.reply[:count]
            return count
        except Exception as error:
            self.error = error
            return -1

    def write_at(self, _, offset, pointer, size):
        count = min(size, 701)
        if offset + count > len(self.output):
            self.error = AssertionError("Destination offset exceeds file size")
            return -1
        self.output[offset:offset + count] = ctypes.string_at(pointer, count)
        return count

    def write_sequential(self, context, pointer, size):
        count = self.write_at(context, self.position, pointer, size)
        if count > 0:
            self.position += count
        return count

    def progress(self, _, done, total):
        if total != len(self.payload) or done > total or (self.samples and done < self.samples[-1][1]):
            self.error = AssertionError("Progress is not bounded and monotonic")
            return 0
        self.samples.append((self.now, done))
        return 1

    def download(self, library, positioned):
        read, write, progress = READ(self.read), READ(self.write), PROGRESS(self.progress)
        output = WRITE_AT(self.write_at) if positioned else READ(self.write_sequential)
        library.wsftp_create.argtypes = [ctypes.c_void_p, READ, READ]
        library.wsftp_create.restype = ctypes.c_void_p
        library.wsftp_init.argtypes = [ctypes.c_void_p]
        library.wsftp_free.argtypes = [ctypes.c_void_p]
        library.wsftp_error.argtypes = [ctypes.c_void_p]
        library.wsftp_error.restype = ctypes.c_char_p
        download = library.wsftp_download_at if positioned else library.wsftp_download
        download.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                             WRITE_AT if positioned else READ, PROGRESS, ctypes.c_void_p]
        client = library.wsftp_create(None, read, write)
        if not client:
            raise AssertionError("Codec allocation failed")
        try:
            if not library.wsftp_init(client) or not download(client, b"archive.tar.gz", len(self.payload), output, progress, None):
                raise AssertionError(self.error or library.wsftp_error(client).decode())
        finally:
            library.wsftp_free(client)
        if self.error:
            raise self.error
        if self.output != self.payload or self.samples[-1][1] != len(self.payload):
            raise AssertionError("Downloaded archive/progress differs from source")
        return self


class SftpPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("Host C compiler required for SFTP pipeline tests")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="sftp-pipeline-", dir=ROOT / "build")
        library = Path(cls.temp.name) / "wsftp.so"
        subprocess.run([compiler, "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "mac/Sources/CSFTP/include"),
                        str(ROOT / "mac/Sources/CSFTP/wsftp.c"), "-o", str(library)], check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.payload = archive_payload()

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)
            del cls.library
        cls.temp.cleanup()

    def test_delayed_blocks_do_not_drain_the_request_window(self):
        ordered = DelayedServer(self.payload).download(self.library, False)
        positioned = DelayedServer(self.payload).download(self.library, True)
        self.assertLess(positioned.now, ordered.now)
        self.assertEqual(next(time for time, done in positioned.samples if done), 40)
        self.assertEqual(next(time for time, done in ordered.samples if done), 300)
        self.assertLessEqual(positioned.peak_inflight, 64)
        self.assertEqual(positioned.inflight, 0)

    def test_short_replies_update_progress_before_the_tail_arrives(self):
        ordered = DelayedServer(self.payload, 17003, False).download(self.library, False)
        positioned = DelayedServer(self.payload, 17003, False).download(self.library, True)
        self.assertEqual(next(done for _, done in positioned.samples if done), 17003)
        self.assertEqual(next(time for time, done in positioned.samples if done), 40)
        self.assertEqual(next(time for time, done in ordered.samples if done), 80)
        self.assertLessEqual(positioned.peak_inflight, 64)


if __name__ == "__main__":
    unittest.main()
