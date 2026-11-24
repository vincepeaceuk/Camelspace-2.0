#!/usr/bin/env python3
"""Install the local bundle, preserving any earlier installed build."""
import argparse
from datetime import datetime
from pathlib import Path
import os
import shutil
import subprocess


def install(bundle, destination):
    bundle = Path(bundle).resolve(strict=True)
    destination = Path(destination).expanduser().resolve()
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / "Camel Space 2.vst3"
    if target == bundle:
        raise ValueError("Source bundle is already the installation target")
    staging = destination / f".CamelSpace2-install-{os.getpid()}"
    if staging.exists():
        raise FileExistsError(staging)
    backup = None
    try:
        shutil.copytree(bundle, staging, symlinks=True)
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(staging)], check=True)
        if target.exists():
            # Keep old VST3 bundles outside the scanned VST3 tree, so hosts
            # cannot discover two builds with the same plug-in identity.
            folder = destination.parent / "Camel Space 2 Backups" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
            folder.mkdir(parents=True)
            backup = folder / target.name
            target.rename(backup)
        try:
            staging.rename(target)
        except BaseException:
            if backup is not None:
                backup.rename(target)
            raise
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return target, backup


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, default=Path(__file__).resolve().parent / "VST3/Camel Space 2.vst3")
    parser.add_argument("--destination", type=Path, default=Path.home() / "Library/Audio/Plug-Ins/VST3")
    args = parser.parse_args()
    target, backup = install(args.bundle, args.destination)
    print(f"Installed: {target}")
    if backup:
        print(f"Previous build preserved: {backup}")
    print("Restart or rescan your DAW, then select Camel Space 2 from Atjazz Record Company.")


if __name__ == "__main__":
    main()
