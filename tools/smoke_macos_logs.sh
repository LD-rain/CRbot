#!/usr/bin/env bash
# Verify the production log tee with fragmented writes and the actual disk/sink bytes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
xcrun clang++ -std=c++23 -Wall -Wextra -pthread -I"$ROOT/include" \
    "$ROOT/tests/macos/LogTeeSmoke.cpp" "$BUILD_DIR/libcrbot_core.a" \
    -o "$TEST_DIR/log_smoke"
python3 - "$TEST_DIR" <<'PY'
from datetime import datetime, timedelta
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1])
before = datetime.now() - timedelta(seconds=1)
subprocess.run([str(root / 'log_smoke'), str(root / 'disk.log'), str(root / 'sink.log')], check=True, timeout=20)
after = datetime.now() + timedelta(seconds=1)
disk = (root / 'disk.log').read_bytes().decode('utf-8-sig')
sink = (root / 'sink.log').read_bytes().decode('utf-8')
assert disk == sink, 'File and UI sink must have identical timestamps and contents'
lines = disk.split('\n')
expected = ['first', 'fragmented 中文', '', 'batch-one', 'batch-two\r', 'iostream', 'stderr', 'long:' + 'x' * 9000, 'tail']
assert len(lines) == len(expected), 'Lost or duplicated log lines'
for line, message in zip(lines, expected):
    match = re.fullmatch(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\] (.*)', line)
    assert match and match[2] == message, 'Missing/duplicate timestamp or damaged log contents'
    timestamp = datetime.strptime(match[1], '%Y-%m-%d %H:%M:%S.%f')
    assert before <= timestamp <= after, 'Timestamp does not use the current local time'
print('PASS: millisecond timestamps, fragmented UTF-8, CRLF, multiple/empty/long lines, trailing bytes, disk/sink equality')
PY
