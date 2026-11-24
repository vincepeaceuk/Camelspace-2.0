#!/usr/bin/env python3
"""Assemble the private local build and complete corresponding JUCE source."""
from pathlib import Path
import hashlib
import json
import plistlib
import shutil
import subprocess


def main():
    project = Path(__file__).resolve().parents[1]
    repository = project
    dist = project / "dist"
    bundle = project / "build/CamelSpace2_artefacts/Release/VST3/Camel Space 2.vst3"
    version=plistlib.loads((bundle / "Contents/Info.plist").read_bytes())["CFBundleShortVersionString"]
    release = dist / f"Camel Space 2 macOS {version}"
    if release.exists():
        shutil.rmtree(release)
    release.mkdir(parents=True)
    subprocess.run(["/usr/bin/codesign", "--verify", "--strict", "--deep", str(bundle)], check=True)
    shutil.copytree(bundle, release / "VST3" / bundle.name)
    for source, name in [(project / "README.md", "README.md"),
                         (project / "SUPPORT.md", "SUPPORT.md"),
                         (project / "LICENSE", "LICENSE"),
                         (project / "CHANGELOG.md", "CHANGELOG.md"),
                         (project / "tools/Install.command", "Install.command"),
                         (project / "tools/install.py", "install.py"),
                         (project / "local/runtime/CamelSpaceManual.pdf", "CamelSpace Manual.pdf")]:
        shutil.copy2(source, release / name)
    shutil.copytree(project / "docs", release / "Verification")
    for name in ["README.md", "SUPPORT.md"]:
        document = release / name
        document.write_text(document.read_text().replace("(docs/", "(Verification/"))
    shutil.copy2(repository / "LICENSE.md", release)
    (release / "licenses").mkdir()
    for name in ["AGPL-3.0.txt", "JUCE-LICENSE.md", "JUCE.spdx.json", "VST3-SDK-LICENSE.txt"]:
        shutil.copy2(repository / "licenses" / name, release / "licenses" / name)
    shutil.copy2(repository / "licenses/INITIAL-REPOSITORY-MIT.txt", release / "licenses")
    shutil.copytree(project / "assets", release / "assets")
    evidence = release / "Verification/evidence"
    evidence.mkdir(exist_ok=True)
    for source in [project / "build/Testing/Temporary/LastTest.log",
                   project / "build/evidence/audio-equivalence.csv",
                   project / "build/evidence/Camel Space 2.png",
                   project / "local/analysis/pluginval-final-console.log",
                   project / "local/analysis/ctest.log",
                   project / "local/analysis/ctest-release-ui.log",
                   project / "local/analysis/reverse-import.log"]:
        if source.is_file():
            shutil.copy2(source, evidence / source.name)
    for name in ["installed-host.log", "pluginval-installed.log", "ctest-preset-labels.log"]:
        source = project / "local/analysis" / name
        if source.is_file():
            shutil.copy2(source, evidence / name)
    for pattern in ["rebrand-*.log","correction-*.log","white-text-check.log"]:
        for source in (project / "local/analysis").glob(pattern):
            shutil.copy2(source,evidence / source.name)
    data = project / "local/runtime/CamelSpaceData"
    presets = release / "Original Presets"
    presets.mkdir()
    for source in data.glob("*.fxb"):
        shutil.copy2(source, presets / source.name)
    shutil.copytree(data / "Presets/StepSequencerPresets", presets / "Step Sequencer Presets")
    shutil.copy2(project / "local/runtime/recovery-inventory.json", presets)
    source_root = dist / f"Camel Space 2 Source {version}"
    if source_root.exists():
        shutil.rmtree(source_root)
    source_root.mkdir()
    shutil.copytree(project, source_root, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("local", "dist", "build", "build-*", "deps", ".git", "__pycache__", "*.pyc", ".DS_Store"))
    shutil.copytree(repository / "deps/JUCE", source_root / "deps/JUCE",
                    ignore=shutil.ignore_patterns(".git", ".DS_Store", "__pycache__", "*.pyc"))
    (source_root / "deps/JUCE/CAMELSPACE-PINNED-COMMIT.txt").write_text(
        "d60b7a78030661574a615b586d9325ba7a7fa7dd\n")
    (source_root / "SOURCE-REVISION.txt").write_text(
        "JUCE 9.0.5 d60b7a78030661574a615b586d9325ba7a7fa7dd\n"
        "Build instructions: README.md\n"
        "Original CamelSpace runtime files are supplied separately in the local binary package.\n")
    artifacts = []
    for folder in [release, source_root]:
        archive = dist / (folder.name + ".zip")
        if archive.exists():
            archive.unlink()
        subprocess.run(["/usr/bin/ditto", "-c", "-k", "--keepParent", str(folder), str(archive)], check=True)
        artifacts.append({"file": archive.name, "bytes": archive.stat().st_size,
                          "sha256": hashlib.file_digest(archive.open("rb"), "sha256").hexdigest()})
    (dist / "artifacts.json").write_text(json.dumps(artifacts, indent=2) + "\n")
    print(json.dumps(artifacts, indent=2))


if __name__ == "__main__":
    main()
