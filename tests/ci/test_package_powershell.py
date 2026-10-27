"""Exercise staging/ISCC arguments with a fake compiler; native CI tests installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_packages import verify


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell is not installed")
class PowerShellPackageTests(unittest.TestCase):
    def test_prerelease_and_stable_multi_config_packages(self):
        with tempfile.TemporaryDirectory(prefix="crbot-package-") as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            shutil.copy2(ROOT / "tools/package.ps1", root / "tools/package.ps1")
            shutil.copytree(ROOT / "packaging", root / "packaging")
            shutil.copytree(ROOT / "assets", root / "assets")
            shutil.copytree(ROOT / "docs", root / "docs")
            for name in ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"]:
                shutil.copy2(ROOT / name, root / name)
            compiler = root / "fake-iscc.ps1"
            compiler.write_text(r'''
if ($env:CRBOT_TEST_ISCC_FAIL -eq '1') { exit 7 }
$defines = @{}
foreach ($argument in $args) {
    if ($argument -match '^/D([^=]+)=(.*)$') { $defines[$Matches[1]] = $Matches[2] }
}
if (-not (Test-Path $args[-1])) { throw 'Missing Inno Setup script' }
$defines | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'compiler-args.json')
# A deliberately non-runnable PE fixture. Native compilation is tested on Windows.
$header = [byte[]]::new(68)
$header[0] = 77; $header[1] = 90; $header[60] = 64; $header[64] = 80; $header[65] = 69
[System.IO.File]::WriteAllBytes((Join-Path $defines.InstallerDir ($defines.InstallerName + '.exe')), $header)
exit 0
''', encoding="utf-8")
            build = root / "build with spaces"
            (build / "Release").mkdir(parents=True)
            # Package layout test only; this fixture is never executed.
            (build / "Release/CRbot.exe").write_bytes(b"package test fixture, not an executable")
            command = ["pwsh", "-NoProfile", "-File", str(root / "tools/package.ps1"),
                       "-SkipBuild", "-BuildDir", str(build), "-Config", "Release", "-IsccPath", str(compiler)]
            for channel, version, number, display in [
                ("prerelease", "1.1.0", 3, "1.1.0(3)"),
                ("stable", "1.1.1", 4, "1.1.1")
            ]:
                metadata = {"channel": channel, "version": version, "build_number": number,
                            "display_version": display, "configuration": "Release", "source_revision": "a" * 40}
                (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                payload = build / "installer-stage" / f"CRBot-{display}-win64"
                verify(root / "dist", display, number, "win64", payload)
                defines = json.loads((root / "compiler-args.json").read_text(encoding="utf-8-sig"))
                self.assertEqual(Path(defines["PayloadDir"]), payload)
                self.assertEqual(defines["AppVersion"], version)
                self.assertEqual(defines["DisplayVersion"], display)
                self.assertEqual(defines["InstallerName"], f"CRBot-{display}-win64-setup")
                self.assertFalse(list((root / "dist").glob("*.zip")))
            metadata["configuration"] = "Debug"
            (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            metadata["configuration"] = "Release"
            (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
            failure = subprocess.run(command, capture_output=True, env={**os.environ, "CRBOT_TEST_ISCC_FAIL": "1"})
            self.assertNotEqual(failure.returncode, 0)
            self.assertFalse((root / "dist" / f"CRBot-{display}-win64-setup.exe").exists())
            self.assertFalse((root / "dist" / f"CRBot-{display}-win64-setup.exe.sha256").exists())
