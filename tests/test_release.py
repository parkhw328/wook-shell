"""Release integrity checks use disposable fixtures, never the user's binaries."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("release", Path(__file__).resolve().parents[1] / "scripts/index-dist.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name) / "0.13.0"
        self.platform = self.folder / "windows-x64"
        self.platform.mkdir(parents=True)
        self.exe = self.platform / "wShell.exe"
        self.exe.write_bytes(b"release fixture")
        self.zip = self.platform / "wshell-0.13.0-windows-x64.zip"
        self.bundle()

    def bundle(self, payload=None, extra=None):
        with zipfile.ZipFile(self.zip, "w") as archive:
            archive.writestr("wShell.exe", self.exe.read_bytes() if payload is None else payload)
            if extra:
                archive.writestr(extra, b"fixture")
        for path in (self.exe, self.zip):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            path.with_name(path.name + ".sha256").write_text(f"{digest}  {path.name}\n")

    def test_matching_downloads(self):
        artifacts = release.artifacts_for(self.folder)
        self.assertEqual(len(artifacts), 2)
        self.assertEqual(artifacts[0]["sha256"], hashlib.sha256(self.exe.read_bytes()).hexdigest())

    def test_corrupted_download_rejected(self):
        self.exe.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.artifacts_for(self.folder)

    def test_different_executable_with_valid_hash_rejected(self):
        self.bundle(payload=b"a different executable")
        with self.assertRaisesRegex(ValueError, "matching single executable"):
            release.artifacts_for(self.folder)

    def test_unexpected_release_file_rejected(self):
        (self.platform / "private.key").write_bytes(b"test fixture")
        with self.assertRaisesRegex(ValueError, "Unexpected or missing"):
            release.artifacts_for(self.folder)

    def test_sensitive_or_traversing_archive_entries_rejected(self):
        for name in ("private.key", "data/passwords", "../outside", "keys/id_rsa"):
            with self.subTest(name=name):
                self.bundle(extra=name)
                with self.assertRaisesRegex(ValueError, "Unexpected archive entry"):
                    release.verify_archive(self.zip)

    def test_catalog_preserves_build_provenance(self):
        artifacts = release.artifacts_for(self.folder)
        manifest = release.manifest_for(self.folder, artifacts, {"sourceCommit": "fixture", "sourceDirty": False, "workflow": "fixture-run"})
        (self.folder / "manifest.json").write_text(json.dumps(manifest))
        self.assertEqual(release.manifest_for(self.folder, artifacts), manifest)
        changed = [dict(item) for item in artifacts]
        changed[0]["sha256"] = "invalid"
        with self.assertRaisesRegex(ValueError, "Published manifest mismatch"):
            release.manifest_for(self.folder, changed)


if __name__ == "__main__":
    unittest.main()
