"""Build and sign Pixel Koi with the local MicroPixel developer kit."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", type=Path, default=Path("C:/esp/mini-app-platform"))
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get(
        "WASI_SDK_PATH", "C:/esp/mini-app-sdk/wasi-sdk-34.0-x86_64-windows")))
    parser.add_argument("--key", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    tools = args.platform.resolve() / "tools"
    sys.path.insert(0, str(tools))
    from app_package import create
    from cryptography.hazmat.primitives import serialization
    from node_runtime import node_executable

    source = ROOT / "pixel-fish"
    compiler = args.sdk / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    args.output.mkdir(parents=True, exist_ok=True)
    wasm = args.output / "pixel-fish.wasm"
    subprocess.run([
        str(compiler), "--target=wasm32-unknown-unknown", "-mcpu=mvp", "-mbulk-memory",
        "-std=c23", "-nostdlib", "-ffreestanding", "-O3", "-Wall", "-Wextra", "-Werror",
        "-I", str(tools.parent / "sdk" / "include"), str(source / "main.c"),
        "-Wl,--no-entry,--export=app_init,--export=app_event,--export-memory",
        "-Wl,--stack-first,-z,stack-size=8192,--initial-memory=262144,--max-memory=262144,--strip-all",
        "-o", str(wasm),
    ], check=True)
    subprocess.run([node_executable(), str(tools / "abi_check.mjs"), str(wasm)], check=True)
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    key = args.key or tools.parent / "local" / "dev_private.pem"
    signing_key = serialization.load_pem_private_key(key.read_bytes(), password=None)
    package = create(manifest, wasm.read_bytes(), signing_key)
    (args.output / "pixel-fish.app").write_bytes(package)
    print(f"Built and signed Pixel Koi {manifest['version']} ({len(package):,} bytes)")


if __name__ == "__main__":
    main()
