"""Exercise release download failures without copying the full game resources."""
import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_bundle import ASSETS, build_bundle, sha, validate_tag, verify_bundle

COMMIT = "a" * 40


class ReleaseBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.write("H1PVZPortable.bda", b"tested program")
        bda_sha = sha(self.directory / "H1PVZPortable.bda")
        self.write_json("H1PVZPortable.build.json", {"sha256": bda_sha})
        self.write_json("native-verification.json", {"passed": True, "program": {
            "passed": True, "bda_sha256": bda_sha,
            "build_receipt_sha256": sha(self.directory / "H1PVZPortable.build.json")}})
        self.write("RELEASE-NOTES.md", b"release notes")
        self.write_json("release-manifest.json", {"source_commit": COMMIT, "tag": "",
            "distribution": "hardware-tested-prebuilt", "program_sha256": bda_sha,
            "resource_files": 1})
        self.make_zip()
        self.checksums()

    def write(self, name, data):
        (self.directory / name).write_bytes(data)

    def write_json(self, name, data):
        self.write(name, json.dumps(data).encode())

    def make_zip(self, resource="B/应用/数据/游戏/PVZ/images/test.png", tamper=False):
        members = {"A/应用/程序/H1PVZPortable.bda": b"tested program", resource: b"image"}
        manifest = {"includes_savedata": False,
                    "program_sha256": sha(self.directory / "H1PVZPortable.bda"),
                    "files": {name: hashlib.sha256(data).hexdigest() for name, data in members.items()}}
        with zipfile.ZipFile(self.directory / "H1PVZ-full-install.zip", "w") as archive:
            for name, data in members.items():
                archive.writestr(name, b"corrupt" if tamper and name == resource else data)
            archive.writestr("package-manifest.json", json.dumps(manifest))

    def checksums(self):
        self.write("H1PVZ-full-install.zip.sha256", (sha(self.directory / "H1PVZ-full-install.zip") + "  H1PVZ-full-install.zip\n").encode())
        self.write("SHA256SUMS.txt", "".join(sha(self.directory / name) + "  " + name + "\n" for name in sorted(ASSETS)).encode())

    def verify(self, **kwargs):
        verify_bundle(self.directory, kwargs.get("commit", COMMIT), kwargs.get("tag", ""))

    def test_valid_download(self):
        self.verify()

    def test_version_tags(self):
        for tag in ("", "v0.1.0", "v12.3.45", "v1.0.0-alpha.1", "v1.0.0-beta.2", "v1.0.0-rc.1"):
            validate_tag(tag)
        for tag in ("v01.0.0", "v1", "v1.0.0-rc.0", "v1.0.0\n", "v1.0.0;echo bad", "../v1.0.0"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                validate_tag(tag)

    def test_corrupt_program(self):
        self.write("H1PVZPortable.bda", b"bad")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify()

    def test_wrong_source_and_tag(self):
        for args in ({"commit": "b" * 40}, {"tag": "v1.0.0"}):
            with self.subTest(args=args), self.assertRaisesRegex(ValueError, "source/tag mismatch"):
                self.verify(**args)

    def test_unlisted_and_missing_files(self):
        self.write("extra.txt", b"unexpected")
        with self.assertRaisesRegex(ValueError, "Unexpected or missing"):
            self.verify()
        (self.directory / "extra.txt").unlink()
        (self.directory / "H1PVZPortable.bda").unlink()
        with self.assertRaisesRegex(ValueError, "Unexpected or missing"):
            self.verify()

    def test_unsafe_checksum_entries(self):
        valid = (self.directory / "SHA256SUMS.txt").read_bytes()
        for suffix in (b"0" * 64 + b"  ../escape\n", valid.splitlines()[0] + b"\n"):
            self.write("SHA256SUMS.txt", valid + suffix)
            with self.assertRaises(ValueError):
                self.verify()

    def test_wrong_hardware_proof_despite_matching_outer_hash(self):
        self.write_json("native-verification.json", {"passed": True, "program": {
            "passed": True, "bda_sha256": "0" * 64}})
        self.checksums()
        with self.assertRaisesRegex(ValueError, "Program/proof mismatch"):
            self.verify()

    def test_corrupt_zip_member_despite_matching_outer_hash(self):
        self.make_zip(tamper=True)
        self.checksums()
        with self.assertRaisesRegex(ValueError, "ZIP member checksum"):
            self.verify()

    def test_savedata_and_traversal_rejected(self):
        for resource in ("B/应用/数据/游戏/PVZ/userdata/user1.dat", "../escape"):
            self.make_zip(resource=resource)
            self.checksums()
            with self.subTest(resource=resource), self.assertRaisesRegex(ValueError, "Unsafe ZIP member"):
                self.verify()

    def test_output_outside_dist_is_preserved(self):
        with self.assertRaisesRegex(ValueError, "preserve existing bundles"):
            build_bundle(self.directory)
        self.assertTrue((self.directory / "H1PVZPortable.bda").exists())


if __name__ == "__main__":
    unittest.main()
