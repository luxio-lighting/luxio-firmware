Import("env")

import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(env.subst("$PROJECT_DIR"))
RELEASE = ROOT / "build/esp32c3/release"
EXPECTED_PARTS = {
    "bootloader.bin": (0x0, 0x8000),
    "partitions.bin": (0x8000, 0x1000),
    "otadata.bin": (0xE000, 0x2000),
    "recovery.bin": (0x10000, 0x140000),
    "luxio.bin": (0x150000, 0x260000),
}


def build_firmware(source, target, env):
    subprocess.run(
        [sys.executable, "tools/build-firmware.py", "esp32c3"],
        cwd=ROOT,
        check=True,
    )


def flash_firmware(source, target, env):
    port = env.subst("$UPLOAD_PORT").strip()
    if not port:
        raise RuntimeError(
            "Set a serial port with --upload-port (for example /dev/ttyACM0)."
        )

    manifest_path = RELEASE / "manifest.local.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(
            "Build the package first with "
            "`pio run -e luxio_esp32c3 -t luxio_build`."
        )
    manifest = json.loads(manifest_path.read_text())
    if (
        manifest.get("chip") != "esp32c3"
        or manifest.get("target") != "esp32c3-recovery-v1"
        or manifest.get("minimumFlashSize") != 4 * 1024 * 1024
    ):
        raise ValueError(f"Unexpected ESP32-C3 flash manifest: {manifest_path}")

    manifest_parts = {
        part["file"]: part for part in manifest.get("parts", [])
    }
    if set(manifest_parts) != set(EXPECTED_PARTS):
        raise ValueError(f"Unexpected flash parts in {manifest_path}")

    images = []
    for filename, (expected_address, capacity) in EXPECTED_PARTS.items():
        part = manifest_parts[filename]
        if part.get("address") != expected_address:
            raise ValueError(f"Unexpected flash offset for {filename}")
        image = RELEASE / filename
        if not image.is_file():
            raise FileNotFoundError(image)
        data = image.read_bytes()
        if part.get("size") != len(data) or not 0 < len(data) <= capacity:
            raise ValueError(f"Unexpected image size for {image}")
        if filename == "otadata.bin" and data != bytes([0xFF]) * capacity:
            raise ValueError("OTA selection image must be erased")
        digest = hashlib.sha256(data).hexdigest()
        if digest != part.get("sha256"):
            raise ValueError(f"SHA-256 mismatch for {image}")
        images.append((expected_address, image))

    esptool_candidates = list(
        (ROOT / ".sdk/arduino/packages/esp32/tools/esptool_py").glob("*/esptool")
    )
    if len(esptool_candidates) != 1:
        raise FileNotFoundError(
            "Expected one SDK esptool; run tools/bootstrap-firmware.sh first."
        )

    command = [
        str(esptool_candidates[0]),
        "--chip",
        "esp32c3",
        "--port",
        port,
        "--baud",
        str(env.GetProjectOption("upload_speed")),
        "write-flash",
        "--flash-mode",
        "dio",
        "--flash-size",
        "4MB",
    ]
    for address, image in sorted(images):
        command.extend((hex(address), str(image)))
    subprocess.run(command, cwd=ROOT, check=True)


env.AddCustomTarget(
    name="luxio_build",
    dependencies=None,
    actions=[build_firmware],
    title="Build Luxio ESP32-C3 firmware",
    description="Build and package using the pinned official firmware toolchain",
)
env.AddCustomTarget(
    name="luxio_flash",
    dependencies=None,
    actions=[flash_firmware],
    title="Flash Luxio ESP32-C3 firmware",
    description="Validate the flash manifest and write the C3 images",
)
