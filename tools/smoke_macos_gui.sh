#!/usr/bin/env bash
# Tests the same C++ bridge and snapshot models used by SwiftUI, with fake ADB.
# Includes the real 60-second login prompt. No real devices are contacted.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"
bash "$ROOT/tools/smoke_macos_logs.sh" "$BUILD_DIR"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
cp "$ROOT/tests/macos/fake_adb.py" "$TEST_DIR/adb"
chmod +x "$TEST_DIR/adb"
printf 'normal' > "$TEST_DIR/mode"
xcrun swiftc -parse-as-library -swift-version 6 \
    -module-cache-path "$BUILD_DIR/swift-module-cache" \
    -import-objc-header "$ROOT/macos/CRBotBridge.h" \
    "$ROOT/macos/Models.swift" "$ROOT/tests/macos/BridgeSmoke.swift" \
    "$BUILD_DIR/libcrbot_macos_bridge.a" "$BUILD_DIR/libcrbot_core.a" \
    -lc++ -framework ImageIO -framework CoreGraphics -framework CoreFoundation \
    -o "$TEST_DIR/bridge_smoke"
CRBOT_ADB="$TEST_DIR/adb" CRBOT_TEST_FIXTURE="$TEST_DIR" \
    "$TEST_DIR/bridge_smoke" "$TEST_DIR" || { cat "$TEST_DIR/result.txt"; exit 1; }
cat "$TEST_DIR/result.txt"
