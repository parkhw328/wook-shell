"""Release integrity checks use disposable fixtures, never the user's binaries."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
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

    def test_cross_build_status_survives_catalog_refresh(self):
        artifacts = release.artifacts_for(self.folder)
        provenance = {"sourceCommit": "fixture", "sourceDirty": False, "buildHost": "linux",
                      "validation": {"staticPackage": "passed", "windowsRuntime": "not-run", "ngs": "not-run"}}
        manifest = release.manifest_for(self.folder, artifacts, provenance)
        (self.folder / "manifest.json").write_text(json.dumps(manifest))
        refreshed = release.manifest_for(self.folder, artifacts)
        self.assertEqual(refreshed, manifest)
        self.assertIn("has not been run", release.validation_note(refreshed))
        self.assertNotIn("workflow", refreshed)

    def test_release_catalog_selects_latest_per_platform_and_preserves_manifest(self):
        root = Path(self.temp.name)
        releases = root / "release"
        releases.mkdir()
        artifacts = release.artifacts_for(self.folder)
        manifest = release.manifest_for(self.folder, artifacts, {"sourceCommit": "original", "sourceDirty": False})
        original = json.dumps(manifest, indent=4).encode()
        (self.folder / "manifest.json").write_bytes(original)
        self.folder.rename(releases / "0.13.0")
        (root / "VERSION").write_text("0.13.0\n")
        mac = releases / "0.11.0" / "macos-universal"
        mac.mkdir(parents=True)
        archive = mac / "wshell-0.11.0-macos-universal.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("wShell.app/Contents/MacOS/wShell", b"mac fixture")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_name(archive.name + ".sha256").write_text(f"{digest}  {archive.name}\n")
        with patch.object(release, "ROOT", root), patch("builtins.print"):
            release.index(catalog_only=True)
        catalog = (releases / "README.md").read_text(encoding="utf-8")
        self.assertIn("| windows-x64 | 0.13.0 |", catalog)
        self.assertIn("| macos-universal | 0.11.0 |", catalog)
        self.assertIn("0.11.0/macos-universal/wshell-0.11.0-macos-universal.zip", catalog)
        self.assertEqual((releases / "0.13.0/manifest.json").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
