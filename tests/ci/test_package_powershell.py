"""Exercise the zip packaging end to end with real PowerShell."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_packages import package_name, verify


POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


@unittest.skipUnless(POWERSHELL, "PowerShell is not installed")
class PowerShellPackageTests(unittest.TestCase):
    def test_prerelease_and_stable_multi_config_packages(self):
        with tempfile.TemporaryDirectory(prefix="crbot-package-") as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            shutil.copy2(ROOT / "tools/package.ps1", root / "tools/package.ps1")
            shutil.copytree(ROOT / "assets", root / "assets")
            shutil.copytree(ROOT / "docs", root / "docs")
            for name in ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"]:
                shutil.copy2(ROOT / name, root / name)
            build = root / "build with spaces"
            (build / "Release").mkdir(parents=True)
            # Package layout test only; this fixture is never executed.
            (build / "Release/CRbot.exe").write_bytes(b"package test fixture, not an executable")
            command = [POWERSHELL, "-NoProfile", "-File", str(root / "tools/package.ps1"),
                       "-SkipBuild", "-BuildDir", str(build), "-Config", "Release"]
            metadata = {}
            for channel, version, number, display in [
                ("prerelease", "1.1.0", 3, "1.1.0(3)"),
                ("stable", "1.1.1", 4, "1.1.1")
            ]:
                metadata = {"channel": channel, "version": version, "build_number": number,
                            "display_version": display, "configuration": "Release",
                            "source_revision": "a" * 40}
                (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
                result = subprocess.run(command, capture_output=True,
                                        encoding="utf-8", errors="replace")
                self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
                # 校验解压后的内容（资源、VERSION、build-info.json）与 SHA256
                self.assertEqual(verify(root / "dist", display, number, "win64"),
                                 package_name(display, "win64"))
                self.assertTrue((root / "dist" / f"CRBot-{display}-win64.zip.sha256").is_file())
                # 只产出 zip，不再产出安装包
                self.assertFalse(list((root / "dist").glob("*.exe")))
            # 配置不匹配要失败
            metadata["configuration"] = "Debug"
            (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
