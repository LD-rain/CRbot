import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)

    def generate(self, number="AUTO", channel="prerelease", gui=True, check=True):
        return subprocess.run([
            "cmake", f"-DCRBOT_SOURCE_DIR={ROOT}", f"-DCRBOT_BINARY_DIR={self.output}",
            f"-DCRBOT_REQUESTED_BUILD_NUMBER={number}", f"-DCRBOT_RELEASE_CHANNEL={channel}",
            "-DCRBOT_SOURCE_REVISION=" + "a" * 40, "-DCRBOT_BUILD_CONFIG=Release",
            f"-DCRBOT_MACOS_GUI={'ON' if gui else 'OFF'}", "-DCRBOT_MACOS_MIN_VERSION=14.0",
            "-P", str(ROOT / "cmake/WriteBuildMetadata.cmake")
        ], check=check, capture_output=True, text=True)

    def metadata(self):
        return json.loads((self.output / "build-info.json").read_text())

    def test_local_builds_increment_and_bundle_agrees(self):
        self.generate()
        self.assertEqual(self.metadata()["build_number"], 1)
        self.generate()
        data = self.metadata()
        self.assertEqual(data["build_number"], 2)
        self.assertEqual(data["display_version"], data["version"] + "(2)")
        plist = plistlib.loads((self.output / "CRBot.app/Contents/Info.plist").read_bytes())
        self.assertEqual(plist["CFBundleVersion"], "2")
        self.assertEqual(plist["CFBundleShortVersionString"], data["version"])

    def test_ci_retries_keep_number_and_stable_hides_it(self):
        for _ in range(2):
            self.generate("17", "stable")
            self.assertEqual(self.metadata()["build_number"], 17)
            self.assertEqual(self.metadata()["display_version"], self.metadata()["version"])
        self.generate()
        self.assertEqual(self.metadata()["build_number"], 18)

    def test_windows_metadata_does_not_generate_apple_bundle(self):
        self.generate("3", gui=False)
        self.assertTrue((self.output / "generated/crbot_version.h").exists())
        self.assertFalse((self.output / "CRBot.app").exists())

    def test_invalid_counter_and_channel_fail(self):
        for number in ["0", "-1", "01", "3;exit"]:
            self.assertNotEqual(self.generate(number, check=False).returncode, 0)
        self.assertNotEqual(self.generate(channel="invalid", check=False).returncode, 0)
        (self.output / "build-number.txt").write_text("bad")
        self.assertNotEqual(self.generate(check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
