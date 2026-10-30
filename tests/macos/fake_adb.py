#!/usr/bin/env python3
"""Deterministic ADB fixture; never contacts a device or starts a real server."""
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib

root = Path(os.environ["CRBOT_TEST_FIXTURE"])
mode = (root / "mode").read_text().strip()
args = sys.argv[1:]
with (root / "commands.jsonl").open("a") as log:
    log.write(json.dumps(args) + "\n")
serial = ""
if args[:1] == ["-s"]:
    serial, args = args[1], args[2:]


def respond(command):
    if "settings get secure android_id" in command:
        return "second-id" if serial == "fake-2" else "same-id"
    if "getprop ro.serialno" in command:
        return "SECOND" if serial == "fake-2" else "SAME"
    if "getprop ro.product.model" in command:
        return "测试 Phone"
    if "pm list packages" in command:
        return "package:com.tencent.tmgp.supercell.clashroyale"
    if "pidof" in command:
        return "4242"
    return ""


command = args[0] if args else ""
if command == "start-server":
    if mode == "server-error":
        print("fake server failure", flush=True)
        sys.exit(1)
    if mode == "slow-server":
        print("partial output", flush=True)
        time.sleep(120)
elif command == "devices":
    print("List of devices attached")
    if mode != "empty":
        print("fake-1\tdevice\nfake-alias\tdevice\nfake-2\tdevice")
    print("unauthorized-phone\tunauthorized")
elif command == "connect":
    if args[1] == "127.0.0.1:5555":
        print("connected to " + args[1])
    else:
        print("failed to connect: Connection refused")
elif command == "shell":
    if len(args) == 1:
        for line in sys.stdin:
            print(respond(line))
            print("__CRBOT_DONE_7f3a__", flush=True)
    else:
        print(respond(" ".join(args[1:])))
elif command == "exec-out":
    if mode == "slow-capture":
        (root / "capture-started").write_text(serial)
        time.sleep(120)
    width, height = (800, 1280) if mode == "bad-resolution" else (720, 1280)
    if "-p" in args:
        def chunk(kind, value):
            return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value) & 0xffffffff)
        pixels = (b"\0" + bytes([20, 20, 20, 255]) * width) * height
        sys.stdout.buffer.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                                + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
    else:
        sys.stdout.buffer.write(struct.pack("<III", width, height, 1) + bytes([20, 20, 20, 255]) * width * height)
