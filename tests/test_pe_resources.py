"""Resource reader rejects absent, malformed and truncated PE resources."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("pe_resources", Path(__file__).resolve().parents[1] / "scripts/pe_resources.py")
pe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pe)


class ResourceTest(unittest.TestCase):
    def fixture(self):
        data = bytearray(1024)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 70, 1)
        struct.pack_into("<H", data, 84, 240)
        struct.pack_into("<H", data, 88, 0x20B)
        struct.pack_into("<II", data, 216, 0x1000, 256)
        struct.pack_into("<IIII", data, 336, 512, 0x1000, 512, 512)
        for offset, name, target in ((0, 10, 0x80000020), (32, 103, 0x80000040), (64, 1033, 96)):
            struct.pack_into("<HH", data, 512 + offset + 12, 0, 1)
            struct.pack_into("<II", data, 512 + offset + 16, name, target)
        struct.pack_into("<IIII", data, 608, 0x1080, 5, 0, 0)
        data[640:645] = b"asset"
        return data

    def test_reads_resource_bytes(self):
        self.assertEqual(pe.read_resource(self.fixture(), 10, 103), b"asset")

    def test_missing_resource(self):
        with self.assertRaisesRegex(ValueError, "Missing PE resource"):
            pe.read_resource(self.fixture(), 10, 104)

    def test_bad_directory_pointer(self):
        data = self.fixture()
        struct.pack_into("<I", data, 532, 0x8000FFFF)
        with self.assertRaisesRegex(ValueError, "out of bounds"):
            pe.read_resource(data, 10, 103)

    def test_bad_payload_pointer(self):
        data = self.fixture()
        struct.pack_into("<I", data, 608, 0x9000)
        with self.assertRaisesRegex(ValueError, "outside"):
            pe.read_resource(data, 10, 103)

    def test_truncated_file(self):
        with self.assertRaises(ValueError):
            pe.read_resource(self.fixture()[:600], 10, 103)


if __name__ == "__main__":
    unittest.main()
