#!/usr/bin/env python3
"""Prepare local runtime assets; never run the legacy installer."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from recover_support import recover


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    recovered = root / "local/recovered"
    if not recovered.exists():
        recover(args.package / "Contents/Archive.pax", recovered)
    report = json.loads((recovered / "recovery-inventory.json").read_text())
    runtime = root / "local/runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    original = runtime / "Original/CamelSpace.vst"
    if original.exists():
        shutil.rmtree(original)
    original.parent.mkdir(exist_ok=True)
    shutil.copytree(args.plugin, original)
    # Sign only the development copy; downloaded original remains untouched.
    subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-",
                    "--timestamp=none", str(original)], check=True)
    data = recovered / "Library/Application Support/Camel Audio/CamelSpaceData"
    shutil.copytree(data, runtime / "CamelSpaceData", dirs_exist_ok=True)
    shutil.copy2(recovered / "Applications/Camel Audio/CamelSpaceManual.pdf", runtime)
    shutil.copy2(recovered / "recovery-inventory.json", runtime)
    programs = []
    # The default bank is a byte-identical Rhythmic1 copy; retain the file while
    # avoiding an extra duplicate category in the host's factory program list.
    for bank in report["banks"]:
        name = Path(bank["path"]).name
        if name == "-Default.fxb":
            continue
        for slot in bank["programs"]:
            if slot["name"]:
                programs.append({"bank": name, "slot": slot["index"], "name": slot["name"]})
    (runtime / "programs.json").write_text(json.dumps(programs, indent=2) + "\n")
    print(f"Prepared {len(programs)} named factory programs in {runtime}")


if __name__ == "__main__":
    main()
