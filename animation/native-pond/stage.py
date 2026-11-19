"""Stage the native pond and its local Mosaico BSP into an ASCII build path."""
import argparse
from pathlib import Path
import re
import shutil

PROJECT = Path(__file__).resolve().parent


def copy_changed(source, target):
    if not target.is_file() or target.read_bytes() != source.read_bytes():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)


def copy_tree(source, target, exclude=()):
    if not source.is_dir():
        raise ValueError(f"Missing source directory: {source}")
    for entry in source.rglob("*"):
        relative = entry.relative_to(source)
        if relative.as_posix() not in exclude and entry.is_file() and not {".git", "__pycache__"}.intersection(relative.parts):
            copy_changed(entry, target / relative)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--platform", type=Path, help="Use the BSP and optional managed components from another platform firmware directory")
    parser.add_argument("--adapter", type=Path, default=PROJECT / "components/esp_lvgl_adapter")
    args = parser.parse_args()
    components = args.platform / "components" if args.platform else PROJECT / "components"
    cache = args.cache.resolve()
    if (not str(cache).isascii() or cache == PROJECT
            or PROJECT in cache.parents or cache in PROJECT.parents):
        raise ValueError("Use a separate ASCII-only ESP-IDF build directory")
    marker = cache / "pond-project.txt"
    if cache.exists() and any(cache.iterdir()) and (
        not marker.is_file() or marker.read_text(encoding="utf-8") != str(PROJECT)
    ):
        raise ValueError("Build directory belongs to another project")
    cache.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(PROJECT), encoding="utf-8")
    target = cache / "src"
    for name in ("CMakeLists.txt", "partitions.csv", "sdkconfig.defaults"):
        copy_changed(PROJECT / name, target / name)
    copy_tree(PROJECT / "main", target / "main")
    copy_tree(PROJECT.parent / "pixel-fish", target / "pixel-fish")
    copy_changed(PROJECT.parent / "pixel-fish/koi_poses.bin", target / "main/koi_poses.bin")
    for name in ("esp-mosaico-bsp", "esp_lcd_co5300"):
        exclude = ("idf_component.yml",) if name == "esp-mosaico-bsp" else ()
        copy_tree(components / name, target / "components" / name, exclude)
    copy_tree(args.adapter, target / "components/esp_lvgl_adapter")
    source = components / "esp-mosaico-bsp/idf_component.yml"
    manifest = source.read_text(encoding="utf-8")
    needle = '  espressif/esp_lvgl_adapter:\n    version: "0.6.2"\n'
    if manifest.count(needle) != 1:
        raise ValueError("The BSP does not use the expected 0.6.2 LVGL adapter")
    start = manifest.index(needle)
    rest = manifest[start + len(needle):]
    next_dependency = re.search(r"(?m)^  [^\s#][^\n]*:", rest)
    block_end = start + len(needle) + (next_dependency.start() if next_dependency else len(rest))
    block = manifest[start:block_end]
    overrides = re.findall(r"(?m)^    override_path:.*\n", block)
    if len(overrides) > 1:
        raise ValueError("The BSP LVGL adapter has duplicate override paths")
    if overrides:
        block = block.replace(overrides[0], "    override_path: ../esp_lvgl_adapter\n")
    else:
        block = block.replace(needle, needle + "    override_path: ../esp_lvgl_adapter\n")
    manifest = manifest[:start] + block + manifest[block_end:]
    staged_manifest = target / "components/esp-mosaico-bsp/idf_component.yml"
    if not staged_manifest.is_file() or staged_manifest.read_text(encoding="utf-8") != manifest:
        staged_manifest.write_text(manifest, encoding="utf-8")
    managed = target / "managed_components"
    source_managed = args.platform / "managed_components" if args.platform else None
    if source_managed and source_managed.is_dir() and not managed.exists():
        shutil.copytree(source_managed, managed, ignore=shutil.ignore_patterns(
            "espressif__esp_lvgl_adapter", "__pycache__"
        ))
    print(target)


if __name__ == "__main__":
    main()
