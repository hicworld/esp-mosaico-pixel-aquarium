"""Check and install Pixel Koi 1.3.0 on ESP-Mosaico through ROM USB."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
APP_NAME = "pixel-koi-pond-1.3.0.bin"
FLASH_FILES = (
    ("0x2000", "bootloader.bin"),
    ("0x8000", "partition-table.bin"),
    ("0xf000", "ota_data_initial.bin"),
    ("0x20000", APP_NAME),
)
PARTITIONS = (
    (b"nvs", 1, 2, 0x9000, 0x6000),
    (b"otadata", 1, 0, 0xF000, 0x2000),
    (b"phy_init", 1, 1, 0x11000, 0x1000),
    (b"ota_0", 0, 16, 0x20000, 0x500000),
    (b"ota_1", 0, 17, 0x520000, 0x500000),
    (b"apps", 1, 130, 0xA20000, 0x5E0000),
)


def verify_files() -> None:
    expected = json.loads((ROOT / "SHA256SUMS.json").read_text(encoding="utf-8"))
    for _, name in FLASH_FILES:
        digest = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if digest != expected[name]:
            raise RuntimeError(f"Firmware checksum mismatch: {name}")

    app = (ROOT / APP_NAME).read_bytes()
    if (not 112 < len(app) <= 0x500000 or app[0] != 0xE9
            or struct.unpack_from("<H", app, 12)[0] != 32
            or app[32:36] != bytes.fromhex("3254cdab")):
        raise RuntimeError("Unexpected ESP32-S31 application image.")
    version = app[48:80].split(b"\0", 1)[0].decode("ascii")
    project = app[80:112].split(b"\0", 1)[0].decode("ascii")
    if version != "1.3.0" or project != "pixel_koi_pond":
        raise RuntimeError("The application must be pixel_koi_pond version 1.3.0.")
    bootloader = (ROOT / "bootloader.bin").read_bytes()
    if bootloader[0] != 0xE9 or struct.unpack_from("<H", bootloader, 12)[0] != 32:
        raise RuntimeError("Unexpected ESP32-S31 bootloader image.")

    table = (ROOT / "partition-table.bin").read_bytes()
    for index, expected_partition in enumerate(PARTITIONS):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from(
            "<HBBII16sI", table, index * 32)
        actual = (label.rstrip(b"\0"), kind, subtype, offset, size)
        if magic != 0x50AA or actual != expected_partition or flags != 0:
            raise RuntimeError("Unexpected partition table.")
    if (ROOT / "ota_data_initial.bin").read_bytes() != b"\xff" * 0x2000:
        raise RuntimeError("Unexpected initial OTA selection data.")


def rom_port(port_name: str) -> str:
    import serial
    from serial.tools import list_ports

    device = next((p for p in list_ports.comports() if p.device.upper() == port_name.upper()), None)
    if not device or (device.vid, device.pid) not in (
            (0x303A, 0x0020), (0x303A, 0x1001), (0x303A, 0x4001)):
        raise RuntimeError("Select an ESP-Mosaico USB CDC or ROM USB port.")
    if device.pid in (0x0020, 0x1001):
        target = device.device
    else:
        physical = (device.location or "").split(":")[0]
        if not physical:
            raise RuntimeError("USB location is unavailable. Enter ROM download mode manually.")
        with serial.Serial(device.device, 115200, timeout=0.2, write_timeout=3) as connection:
            connection.dtr = True
            connection.rts = False
            time.sleep(0.5)
            connection.write(b"\ndownload\n")
            connection.flush()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    connection.read(4096)
                except serial.SerialException:
                    break
        deadline = time.monotonic() + 15
        target = None
        while time.monotonic() < deadline:
            match = next((
                p for p in list_ports.comports()
                if p.vid == 0x303A and p.pid in (0x0020, 0x1001)
                and (p.location or "").split(":")[0] == physical
            ), None)
            if match:
                target = match.device
                break
            time.sleep(0.3)
        if target is None:
            raise RuntimeError("ROM port not found. Enter download mode manually and retry.")

    deadline = time.monotonic() + 20
    while True:
        probe = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=3)
        probe.dtr = False
        probe.rts = False
        probe.port = target
        try:
            probe.open()
            probe.close()
            time.sleep(0.3)
            return target
        except serial.SerialException:
            probe.close()
            if time.monotonic() >= deadline:
                raise RuntimeError("ROM USB port did not become ready.")
            time.sleep(0.3)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--check", action="store_true", help="Check files without opening a serial port")
    args = parser.parse_args()
    verify_files()
    print("Verified all SHA-256 values, partitions and ESP32-S31 images.", flush=True)
    print("Project pixel_koi_pond, version 1.3.0, startup partition ota_0.", flush=True)
    if args.check:
        return
    if not args.port:
        parser.error("--port is required when flashing")

    print("Target must be ESP-Mosaico / ESP32-S31 with 16 MB internal Flash.", flush=True)
    print("This installs standalone Pixel Koi and replaces the application platform.", flush=True)
    print("The platform app in ota_0 and the OTA boot selection will be overwritten.", flush=True)
    print("The restore command will not restore the old platform after this installation.", flush=True)
    print("NVS and external NAND are retained by this flasher.", flush=True)
    if input("Type INSTALL to continue: ").strip() != "INSTALL":
        raise SystemExit("Cancelled.")

    target = rom_port(args.port)
    print(f"ROM USB port: {target}", flush=True)
    command = [
        sys.executable, "-m", "esptool", "--chip", "esp32s31",
        "--port", target, "--baud", "115200",
        "--before", "no-reset", "--after", "hard-reset", "--no-stub",
        "write-flash", "--flash-mode", "dio", "--flash-freq", "80m",
        "--flash-size", "16MB",
    ]
    for offset, name in FLASH_FILES:
        command.extend((offset, str(ROOT / name)))
    subprocess.run(command, check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, struct.error, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
