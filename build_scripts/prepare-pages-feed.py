#!/usr/bin/env python3
"""Generate unsigned opkg feeds from published Keenetic Release assets."""

import importlib.util
import re
import shutil
import sys
from pathlib import Path

ARCHITECTURES = {
    "mipsel-3.4", "mips-3.4", "aarch64-3.10", "armv7-3.2", "x64-3.2"
}
PATTERN = re.compile(
    r"^(keen-pbr(?:-headless)?)_[^_]+_keenetic_(3[.]4|3[.]10|3[.]2)_(mipsel|mips|aarch64|armv7|x64)[.]ipk$"
)


def build(source: Path, output: Path) -> None:
    spec = importlib.util.spec_from_file_location(
        "ipk_index", Path(__file__).with_name("ipk-make-index.py")
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("Missing IPK index generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    variants: dict[str, set[str]] = {}
    for package in sorted(source.glob("*.ipk")):
        match = PATTERN.fullmatch(package.name)
        if not match:
            raise ValueError(f"Unsupported Keenetic package name: {package.name}")
        variant, entware_version, architecture = match.groups()
        config = f"{architecture}-{entware_version}"
        if config not in ARCHITECTURES:
            raise ValueError(f"Unexpected architecture: {config}")
        dest = output / "keenetic" / config
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package, dest / package.name)
        variants.setdefault(config, set()).add(variant)

    if set(variants) != ARCHITECTURES:
        raise ValueError(f"Missing architectures: {sorted(ARCHITECTURES - set(variants))}")
    for config, present in variants.items():
        if present != {"keen-pbr", "keen-pbr-headless"}:
            raise ValueError(f"Missing full/headless package in {config}: {present}")
        dest = output / "keenetic" / config
        module.generate(dest)
        (dest / "Packages.manifest").unlink(missing_ok=True)
        print(f"Published unsigned feed: {config}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: prepare-pages-feed.py <release-assets> <site>")
    build(Path(sys.argv[1]), Path(sys.argv[2]))
