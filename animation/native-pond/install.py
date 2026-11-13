"""Install or upgrade Pixel Koi on the Mosaico 0.5.3 partition layout.

Run with the ESP-IDF Python environment. Upgrades use the native restore
command to boot the preserved platform before writing ota_1. Recovery files
are saved in C:/esp.
"""

import argparse
import binascii
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import serial
from serial.tools.list_ports import comports


ROOT = Path(__file__).resolve().parent
DIST = ROOT.parent / "dist"
PLATFORM_RELEASES = Path.home() / "Desktop/hf/应用平台-双板卡/Mosaico版/dist/ota"
PLATFORM_PROJECT = "esp_mini_platform"
PLATFORM_VERSION = "0.5.3"
POND_PROJECT = "pixel_koi_pond"
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")
SECTOR = 0x1000
TABLE = 0x8000
OTA_DATA = 0xF000
OTA_0 = 0x20000
OTA_1 = 0x520000
SLOT_SIZE = 0x500000
NEW, PENDING_VERIFY, VALID = range(3)
VID = 0x303A
APP_PIDS = {0x4001}
ROM_PIDS = {0x0020, 0x1001}
PARTITIONS = [
    (b"nvs", 1, 2, 0x9000, 0x6000, 0),
    (b"otadata", 1, 0, OTA_DATA, SECTOR * 2, 0),
    (b"phy_init", 1, 1, 0x11000, 0x1000, 0),
    (b"ota_0", 0, 16, OTA_0, SLOT_SIZE, 0),
    (b"ota_1", 0, 17, OTA_1, SLOT_SIZE, 0),
    (b"apps", 1, 130, 0xA20000, 0x5E0000, 0),
]


def image_info(path):
    image = path.read_bytes()
    if (not 112 < len(image) <= SLOT_SIZE or image[0] != 0xE9
            or struct.unpack_from("<H", image, 12)[0] != 32
            or image[32:36] != bytes.fromhex("3254cdab")):
        raise RuntimeError(f"Unexpected ESP32-S31 app image: {path}")
    version = image[48:80].split(b"\0", 1)[0].decode("ascii", errors="replace")
    project = image[80:112].split(b"\0", 1)[0].decode("ascii", errors="replace")
    if project not in (POND_PROJECT, PLATFORM_PROJECT) or not VERSION.fullmatch(version):
        raise RuntimeError(f"Unexpected ESP32-S31 app descriptor: {path}")
    return project, version, hashlib.sha256(image).hexdigest()


def check_partitions(data):
    if len(data) != SECTOR:
        raise RuntimeError("Incomplete partition table backup")
    for index, expected in enumerate(PARTITIONS):
        magic, kind, subtype, address, size, name, flags = struct.unpack_from(
            "<HBBII16sI", data, index * 32)
        if magic != 0x50AA or (name.rstrip(b"\0"), kind, subtype, address, size, flags) != expected:
            raise RuntimeError("The device does not have the expected platform A/B layout")
    end = len(PARTITIONS) * 32
    if (data[end:end + 16] != b"\xeb\xeb" + b"\xff" * 14
            or data[end + 16:end + 32] != hashlib.md5(data[:end]).digest()
            or data[end + 32:] != b"\xff" * (len(data) - end - 32)):
        raise RuntimeError("Partition table checksum or end marker differs")


def selector_update(data):
    if len(data) != SECTOR * 2:
        raise RuntimeError("Incomplete OTA selector backup")
    records = []
    for offset in (0, SECTOR):
        seq, = struct.unpack_from("<I", data, offset)
        state, crc = struct.unpack_from("<II", data, offset + 24)
        if 0 < seq < 0xFFFFFFFF and crc == selector_crc(seq):
            records.append((seq, state, offset))
    bootable = [record for record in records if record[1] in (NEW, PENDING_VERIFY, VALID)]
    if not bootable:
        raise RuntimeError("No bootable selector record")
    seq, state, active_offset = max(bootable)
    if state != VALID or (seq - 1) % 2 != 0:
        raise RuntimeError("The preserved selector does not select confirmed ota_0")
    next_seq = max(record[0] for record in records) + 1
    if next_seq % 2 != 0:
        next_seq += 1
    if next_seq >= 0xFFFFFFFF:
        raise RuntimeError("OTA selector sequence exhausted")
    sector = bytearray(b"\xff" * SECTOR)
    struct.pack_into("<I", sector, 0, next_seq)
    struct.pack_into("<II", sector, 24, NEW, selector_crc(next_seq))
    return SECTOR - active_offset, bytes(sector)


def selector_crc(sequence):
    return binascii.crc32(struct.pack("<I", sequence), 0xFFFFFFFF) & 0xFFFFFFFF


def wait_port(pids, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ports = [port.device for port in comports() if port.vid == VID and port.pid in pids]
        if len(ports) == 1:
            return ports[0]
        if len(ports) > 1:
            raise RuntimeError(f"More than one matching Espressif USB device: {ports}")
        time.sleep(0.3)
    raise TimeoutError(f"Espressif USB device did not enumerate (PIDs {sorted(pids)})")


def connect(port):
    link = serial.Serial(port, 115200, timeout=0.2, write_timeout=4)
    try:
        link.dtr = True
        link.rts = False
        time.sleep(0.45)
        link.reset_input_buffer()
    except BaseException:
        link.close()
        raise
    return link


def command(link, request, timeout=6):
    link.write((request + "\n").encode("ascii"))
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = link.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("result: "):
            if line != "result: ESP_OK":
                raise RuntimeError(f"{request}: {line}")
            return lines
        if line:
            lines.append(line)
    raise TimeoutError(f"No acknowledgement for {request}")


def diagnostic(link, request, prefix):
    lines = command(link, request)
    for line in lines:
        if line.startswith(prefix + " "):
            return line, dict(part.split("=", 1) for part in line.split()[1:] if "=" in part)
    raise RuntimeError(f"Missing {prefix} diagnostic: {lines}")


def request_restart(link, request, acknowledgement):
    link.write((request + "\n").encode("ascii"))
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        line = link.readline().strip()
        if acknowledgement in line:
            return
        if line.startswith(b"result: "):
            raise RuntimeError(f"{request}: {line.decode(errors='replace')}")
    raise TimeoutError(f"No confirmation for {request} before restart")


def esptool(port, operation, *args, reset=False, quiet=False, check=True):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s31", "--port", port,
           "--baud", "115200", "--before", "no-reset", "--after",
           "hard-reset" if reset else "no-reset", "--no-stub", operation, *map(str, args)]
    result = subprocess.run(cmd, check=check, capture_output=quiet, text=True, encoding="utf-8", errors="replace")
    time.sleep(1.3)
    return result


def inactive_archives(explicit, previous_version, image):
    paths = [explicit] if explicit else []
    if previous_version:
        paths.extend((DIST / f"pixel-koi-pond-{previous_version}.bin", image))
    else:
        paths.extend((image, *sorted(DIST.glob("pixel-koi-pond-*.bin"), reverse=True),
                      PLATFORM_RELEASES / "esp_mini_platform-0.5.2.bin"))
    return list(dict.fromkeys(paths))


def backup_inactive(port, archives, previous_version, backup):
    for archive in archives:
        try:
            project, version, digest = image_info(archive)
        except (OSError, RuntimeError):
            continue
        if previous_version and (project, version) != (POND_PROJECT, previous_version):
            continue
        name = "pixel-koi-pond" if project == POND_PROJECT else "platform"
        saved = backup / f"ota_1-{name}-{version}.bin"
        shutil.copyfile(archive, saved)
        if image_info(saved) != (project, version, digest):
            raise RuntimeError(f"Archive changed while backing it up: {archive}")
        verified = esptool(port, "verify-flash", hex(OTA_1), saved, quiet=True, check=False)
        if verified.returncode == 0:
            print(f"Verified inactive slot against {saved.name} (SHA-256 {digest}).", flush=True)
            return saved
        saved.unlink()
    slot = backup / "ota_1-full.bin"
    print("Reading the inactive slot because its archived digest was unavailable.", flush=True)
    esptool(port, "read-flash", hex(OTA_1), hex(SLOT_SIZE), slot)
    if slot.stat().st_size != SLOT_SIZE:
        raise RuntimeError("Incomplete inactive slot backup")
    esptool(port, "verify-flash", hex(OTA_1), slot)
    if previous_version and image_info(slot)[:2] != (POND_PROJECT, previous_version):
        raise RuntimeError("The inactive slot does not contain the previously running pond")
    return slot


def write_candidate(port, image):
    try:
        esptool(port, "write-flash", "--compress", "--no-progress", hex(OTA_1), image)
    except subprocess.CalledProcessError:
        esptool(port, "write-flash", "--no-compress", "--no-progress", hex(OTA_1), image)
    esptool(port, "verify-flash", hex(OTA_1), image)


def stage(port, image, platform, archives, previous_version, backup):
    before_path = backup / "otadata-before.bin"
    table_path = backup / "partition-table.bin"
    esptool(port, "read-flash", hex(TABLE), hex(SECTOR), table_path)
    check_partitions(table_path.read_bytes())
    esptool(port, "read-flash", hex(OTA_DATA), hex(SECTOR * 2), before_path)
    before = before_path.read_bytes()
    offset, candidate_sector = selector_update(before)
    address = OTA_DATA + offset
    old_record = backup / "ota-selector-sector-before.bin"
    old_record.write_bytes(before[offset:offset + SECTOR])
    new_record = backup / "ota-selector-sector-new.bin"
    new_record.write_bytes(candidate_sector)
    (backup / "ota-selector-address.txt").write_text(f"{address:#x}\n", encoding="ascii")

    platform_backup = backup / f"ota_0-platform-{PLATFORM_VERSION}.bin"
    shutil.copyfile(platform, platform_backup)
    if image_info(platform_backup)[:2] != (PLATFORM_PROJECT, PLATFORM_VERSION):
        raise RuntimeError("The application platform archive changed before flash verification")
    esptool(port, "verify-flash", hex(OTA_0), platform_backup)
    previous = backup_inactive(port, archives, previous_version, backup)
    if previous.read_bytes() == image.read_bytes():
        print("The exact candidate already matches ota_1; keeping its verified image.", flush=True)
    else:
        write_candidate(port, image)

    # Select only after the app digest is verified. Read both selector sectors
    # back, so a successful update also proves that ota_0 remains a fallback.
    after_path = backup / "otadata-after.bin"
    try:
        esptool(port, "write-flash", "--no-progress", hex(address), new_record)
        esptool(port, "read-flash", hex(OTA_DATA), hex(SECTOR * 2), after_path)
        expected = before[:offset] + candidate_sector + before[offset + SECTOR:]
        if after_path.read_bytes() != expected:
            raise RuntimeError("OTA selector readback differs")
    except BaseException:
        print(f"Restoring selector sector {address:#x} after the failed write.", flush=True)
        esptool(port, "write-flash", "--no-progress", hex(address), old_record)
        esptool(port, "verify-flash", hex(address), old_record)
        raise


def platform_ready(line, fields):
    if fields.get("project") == POND_PROJECT and fields.get("partition") == "ota_1":
        return False
    if fields.get("version") != PLATFORM_VERSION or fields.get("partition") != "ota_0":
        raise RuntimeError(f"Unexpected application platform console: {line}")
    return fields.get("ota_state") == "2"


def native_ready(line, fields, version):
    if fields.get("partition") == "ota_0":
        raise RuntimeError(f"The board booted its preserved platform: {line}")
    if fields.get("project") != POND_PROJECT or fields.get("version") != version:
        raise RuntimeError(f"Unexpected app console after reboot: {line}")
    return fields.get("partition") == "ota_1" and fields.get("ota_state") == "2"


def wait_for_console(link, ready, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line, fields = diagnostic(link, "status", "STATUS")
        if ready(line, fields):
            print(line, flush=True)
            return line
        time.sleep(0.5)
    raise TimeoutError("Firmware did not confirm its boot")


def connect_ready(ready, timeout=40):
    deadline = time.monotonic() + timeout
    last_error = None
    while (remaining := deadline - time.monotonic()) > 0:
        try:
            port = wait_port(APP_PIDS, min(4, remaining))
            link = connect(port)
            try:
                line = wait_for_console(link, ready, max(0, deadline - time.monotonic()))
            except BaseException:
                link.close()
                raise
            return link, line
        except (serial.SerialException, TimeoutError) as error:
            last_error = error
            time.sleep(0.4)
    raise TimeoutError(f"The requested console did not return after reset: {last_error}")


def check_weather(link, weather, timeout=15):
    command(link, f"scene {weather}")
    rain = str(int(weather == "rain"))
    deadline = time.monotonic() + timeout
    previous = None
    while time.monotonic() < deadline:
        state_line, state = diagnostic(link, "status", "STATUS")
        audio_line, audio = diagnostic(link, "audio_status", "POND_AUDIO")
        lcd_line, lcd = diagnostic(link, "pond_fps", "POND_LCD")
        counts = (int(audio["blocks"]), int(lcd["frames"]))
        if (state.get("weather") == weather and state.get("startup") == "ESP_OK"
                and audio.get("worker") == "1" and audio.get("codec") == "0"
                and audio.get("requested_rain") == rain and audio.get("active") == rain
                and int(lcd["flush_fps"]) > 0 and int(lcd["pixels"]) > 0):
            if previous and all(value > old for value, old in zip(counts, previous)):
                print(state_line, audio_line, lcd_line, sep="\n", flush=True)
                return
            previous = counts
        time.sleep(0.4)
    raise RuntimeError(f"No advancing LCD and audio diagnostics in {weather} mode: "
                       f"{state_line}\n{audio_line}\n{lcd_line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=DIST / "pixel-koi-pond-1.3.0.bin")
    parser.add_argument("--platform-image", type=Path,
                        default=ROOT.parent / "firmware-60hz/esp_mini_platform-0.5.3-koi60.bin")
    parser.add_argument("--inactive-archive", type=Path, help="An additional verified backup candidate for ota_1")
    args = parser.parse_args()
    project, version, digest = image_info(args.image)
    if project != POND_PROJECT:
        raise RuntimeError(f"Expected a native pond image: {args.image}")
    if image_info(args.platform_image)[:2] != (PLATFORM_PROJECT, PLATFORM_VERSION):
        raise RuntimeError(f"Expected the platform {PLATFORM_VERSION} archive: {args.platform_image}")
    print(f"Candidate {version}, SHA-256 {digest}", flush=True)

    previous_version = None
    with connect(wait_port(APP_PIDS, 12)) as link:
        line, fields = diagnostic(link, "status", "STATUS")
        if fields.get("project") == POND_PROJECT and fields.get("partition") == "ota_1":
            previous_version = fields.get("version", "")
            if fields.get("ota_state") != "2" or not VERSION.fullmatch(previous_version):
                raise RuntimeError(f"Expected a confirmed native pond before upgrade: {line}")
        elif not platform_ready(line, fields):
            raise RuntimeError(f"Expected confirmed {PLATFORM_VERSION} platform in ota_0: {line}")
        print(line, flush=True)
        backup = Path(tempfile.mkdtemp(prefix="koi-pond-recovery-", dir="C:/esp"))
        print(f"Recovery files: {backup}", flush=True)
        (backup / "initial-status.txt").write_text(line + "\n", encoding="utf-8")
        image = backup / f"candidate-pixel-koi-pond-{version}.bin"
        shutil.copyfile(args.image, image)
        if image_info(image) != (project, version, digest):
            raise RuntimeError("Candidate changed while preparing the recovery snapshot")
        if previous_version:
            request_restart(link, "restore", b"Restoring the application platform")

    link, line = connect_ready(platform_ready)
    with link:
        (backup / "platform-status.txt").write_text(line + "\n", encoding="utf-8")
        listing = command(link, "list")
        (backup / "platform-apps.txt").write_text("\n".join(listing) + "\n", encoding="utf-8")
        command(link, "stop")
        request_restart(link, "download", b"Entering ROM download mode")

    port = wait_port(ROM_PIDS, 15)
    print(f"ESP32-S31 ROM port: {port}", flush=True)
    try:
        archives = inactive_archives(args.inactive_archive, previous_version, image)
        stage(port, image, args.platform_image, archives, previous_version, backup)
    except BaseException:
        esptool(port, "flash-id", reset=True, check=False)
        raise
    esptool(port, "flash-id", reset=True)

    link, _ = connect_ready(lambda line, fields: native_ready(line, fields, version))
    with link:
        check_weather(link, "sunny")
        try:
            check_weather(link, "rain")
        finally:
            command(link, "scene sunny")
        check_weather(link, "sunny")
    print(f"Pixel Koi installed and left sunny. Recovery files: {backup}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"Install failed: {error}")
