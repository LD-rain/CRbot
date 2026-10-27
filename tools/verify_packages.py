#!/usr/bin/env python3
"""Verify native installers, their checksums, and the installed/mounted payload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile

PLATFORMS = ("win64", "macos-arm64", "macos-x86_64")
TEMPLATES = ("btn_battle.png", "btn_battle2.png", "btn_ok.png", "btn_mode_1v1.png")


def package_name(version, platform):
    if platform not in PLATFORMS:
        raise ValueError("Unknown platform")
    suffix = "-setup.exe" if platform == "win64" else ".dmg"
    return f"CRBot-{version}-{platform}{suffix}"


def verify_payload(root, version, build_number, platform):
    """Check files from a real installation or a read-only mounted disk image."""
    root = Path(root)
    manifest = json.loads((root / "build-info.json").read_text(encoding="utf-8"))
    if manifest["display_version"] != version or manifest["build_number"] != int(build_number):
        raise ValueError("Package version does not match the build")
    expected_version = manifest["version"]
    if manifest["channel"] == "prerelease":
        expected_version += f"({build_number})"
    elif manifest["channel"] != "stable":
        raise ValueError("Unknown release channel")
    if version != expected_version:
        raise ValueError("Invalid display version")
    if (root / "VERSION").read_text(encoding="utf-8").strip() != manifest["version"]:
        raise ValueError("VERSION differs from the build")
    required = ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"]
    if platform == "win64":
        required.append("CRbot.exe")
        required.extend(f"assets/templates/{item}" for item in TEMPLATES)
    elif platform in PLATFORMS:
        required.extend(["CLI/CRbot", "CRBot.app/Contents/MacOS/CRBot", "INSTALL.txt"])
        required.extend(f"CLI/assets/templates/{item}" for item in TEMPLATES)
        required.extend(f"CRBot.app/Contents/Resources/assets/templates/{item}" for item in TEMPLATES)
        required.extend(f"CRBot.app/Contents/Resources/{item}" for item in ["LICENSE", "THIRD_PARTY_NOTICES.md"])
        app = root / "CRBot.app/Contents"
        with (app / "Info.plist").open("rb") as stream:
            plist = plistlib.load(stream)
        if plist["CFBundleVersion"] != str(build_number) or plist["CFBundleShortVersionString"] != manifest["version"]:
            raise ValueError("App bundle version differs from the package")
        if json.loads((app / "Resources/build-info.json").read_text(encoding="utf-8")) != manifest:
            raise ValueError("App bundle metadata differs from the package")
        if not (root / "Applications").is_symlink() or os.readlink(root / "Applications") != "/Applications":
            raise ValueError("Missing Applications installation shortcut")
        for binary in ["CLI/CRbot", "CRBot.app/Contents/MacOS/CRBot"]:
            if not (root / binary).stat().st_mode & 0o111:
                raise ValueError(f"Executable permission lost: {binary}")
    else:
        raise ValueError("Unknown platform")
    for item in required:
        if not (root / item).is_file() or (root / item).stat().st_size == 0:
            raise ValueError(f"Missing or empty resource: {item}")


def verify_checksum(archive):
    archive = Path(archive)
    digest = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    expected = digest.hexdigest() + "  " + archive.name
    if Path(str(archive) + ".sha256").read_text(encoding="utf-8").strip() != expected:
        raise ValueError("SHA256 mismatch")


def verify(directory, version, build_number, platform, payload_directory=None):
    archive = Path(directory).resolve() / package_name(version, platform)
    verify_checksum(archive)
    if platform == "win64":
        # Actual install/uninstall is driven by smoke_windows_installer.ps1.
        with archive.open("rb") as stream:
            header = stream.read(64)
            if len(header) != 64 or header[:2] != b"MZ":
                raise ValueError("Not a Windows installer executable")
            stream.seek(int.from_bytes(header[60:64], "little"))
            if stream.read(4) != b"PE\0\0":
                raise ValueError("Invalid Windows executable header")
        if payload_directory is None:
            raise ValueError("Use smoke_windows_installer.ps1 to install and verify the Windows package")
        verify_payload(payload_directory, version, build_number, platform)
    else:
        if sys.platform != "darwin":
            raise RuntimeError("DMG verification requires macOS")
        subprocess.run(["hdiutil", "verify", str(archive)], check=True)
        with tempfile.TemporaryDirectory(prefix="crbot-dmg-") as temporary:
            mount = Path(temporary) / "volume"
            subprocess.run(["hdiutil", "attach", str(archive), "-readonly", "-nobrowse",
                            "-mountpoint", str(mount)], check=True)
            try:
                verify_payload(mount, version, build_number, platform)
                for binary in [mount / "CRBot.app", mount / "CLI/CRbot"]:
                    subprocess.run(["codesign", "--verify", "--strict", str(binary)], check=True)
                for binary in [mount / "CRBot.app/Contents/MacOS/CRBot", mount / "CLI/CRbot"]:
                    arch = subprocess.check_output(["lipo", "-archs", str(binary)], text=True).strip()
                    if arch != platform.removeprefix("macos-"):
                        raise ValueError(f"Unexpected binary architecture: {arch}")
            finally:
                result = subprocess.run(["hdiutil", "detach", str(mount)])
                if result.returncode:
                    subprocess.run(["hdiutil", "detach", "-force", str(mount)], check=True)
    return archive.name


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-number", type=int, required=True)
    parser.add_argument("--platform", choices=PLATFORMS, required=True)
    parser.add_argument("--payload-dir", help="Windows installation directory created by the installer smoke test")
    args = parser.parse_args()
    print("PASS:", verify(args.directory, args.version, args.build_number, args.platform, args.payload_dir))
