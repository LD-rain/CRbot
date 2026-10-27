"""Failure cases that must prevent publishing an incomplete native installer."""
import hashlib
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_packages import TEMPLATES, package_name, verify, verify_checksum, verify_payload


class InstallerVerificationTests(unittest.TestCase):
    def test_names_checksums_and_renamed_zip_rejection(self):
        self.assertEqual(package_name("1.1.0(3)", "win64"), "CRBot-1.1.0(3)-win64-setup.exe")
        self.assertEqual(package_name("1.1.1", "macos-arm64"), "CRBot-1.1.1-macos-arm64.dmg")
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / package_name("1.1.0(3)", "win64")
            archive.write_bytes(b"PK" + bytes(100))
            checksum = Path(str(archive) + ".sha256")
            checksum.write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name)
            verify_checksum(archive)
            with self.assertRaisesRegex(ValueError, "executable"):
                verify(temporary, "1.1.0(3)", 3, "win64")
            archive.write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "SHA256"):
                verify_checksum(archive)

    def test_macos_payload_requires_install_link_resources_and_matching_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "CRBot.app/Contents"
            manifest = dict(version="1.1.0", build_number=3, display_version="1.1.0(3)", channel="prerelease")
            for base in [root, app / "Resources"]:
                base.mkdir(parents=True, exist_ok=True)
                (base / "build-info.json").write_text(json.dumps(manifest))
                for name in ["LICENSE", "THIRD_PARTY_NOTICES.md"]:
                    (base / name).write_text("license fixture")
            (root / "VERSION").write_text("1.1.0")
            for name in ["README.md", "CHANGELOG.md", "INSTALL.txt"]:
                (root / name).write_text("fixture")
            for base in [root / "CLI", app / "Resources"]:
                (base / "assets/templates").mkdir(parents=True)
                for name in TEMPLATES:
                    (base / "assets/templates" / name).write_bytes(b"template fixture")
            for binary in [root / "CLI/CRbot", app / "MacOS/CRBot"]:
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b"non-executable fixture")
                binary.chmod(0o755)
            (root / "Applications").symlink_to("/Applications")
            plist = dict(CFBundleVersion="3", CFBundleShortVersionString="1.1.0")
            (app / "Info.plist").write_bytes(plistlib.dumps(plist))
            verify_payload(root, "1.1.0(3)", 3, "macos-arm64")
            with self.assertRaisesRegex(ValueError, "does not match"):
                verify_payload(root, "1.1.0(4)", 4, "macos-arm64")
            plist["CFBundleVersion"] = "2"
            (app / "Info.plist").write_bytes(plistlib.dumps(plist))
            with self.assertRaisesRegex(ValueError, "bundle version"):
                verify_payload(root, "1.1.0(3)", 3, "macos-arm64")
            plist["CFBundleVersion"] = "3"
            (app / "Info.plist").write_bytes(plistlib.dumps(plist))
            (root / "Applications").unlink()
            with self.assertRaisesRegex(ValueError, "Applications"):
                verify_payload(root, "1.1.0(3)", 3, "macos-arm64")
            (root / "Applications").symlink_to("/Applications")
            (app / "Resources/assets/templates/btn_ok.png").unlink()
            with self.assertRaisesRegex(ValueError, "resource"):
                verify_payload(root, "1.1.0(3)", 3, "macos-arm64")

    def test_dmg_detaches_when_payload_validation_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / package_name("1.1.1", "macos-arm64")
            archive.write_bytes(b"fake dmg")
            Path(str(archive) + ".sha256").write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name)
            with patch("verify_packages.sys.platform", "darwin"), \
                 patch("verify_packages.subprocess.run") as run, \
                 patch("verify_packages.verify_payload", side_effect=ValueError("bad payload")):
                run.return_value.returncode = 0
                with self.assertRaisesRegex(ValueError, "bad payload"):
                    verify(temporary, "1.1.1", 4, "macos-arm64")
                self.assertEqual(run.call_args_list[-1].args[0][:2], ["hdiutil", "detach"])
