# Standalone public source import

Verified locally on **24 November 2026**, on Apple Silicon macOS 26.6.2 with
Rosetta available.

Camel Space 2 has been separated from the local collection of other plug-ins
for this repository. Its integration source files match the existing 2.0.0
project byte for byte. Changes for the standalone layout are limited to the
JUCE path/bootstrap, package layout, an opt-in real recovery-test fixture path,
and public documentation/notices. No sound-engine, control or state-format
change was made for this import.

## Executed checks

| Check | Result |
| --- | --- |
| Dependency bootstrap | Fresh JUCE 9.0.5 checkout fetched and exact pinned revision verified |
| Resource preparation | Matching user-supplied bundle/package accepted; 476 factory browser entries generated |
| Python recovery tests | 9/9 pass with the original archive; without the archive, 8 pass and the real-payload test skips |
| Release configure/build | Pass from this standalone root using the README commands |
| CTest | 7/7 pass: OriginalEngineBridge, SupportRecovery, NativeVST3, ProcessorRecallAndFiles, SkinControls, OriginalAudioEquivalence, LegacyParserBounds |
| Exact audio, this run | 6,480 cases / 45,342,720 samples; zero differing samples, zero maximum difference, matching parameters and no observed input mutation |
| Tempo comparison, this run | 196,608 samples through 120 → 87 BPM; zero differences |
| Bundle | Version 2.0.0; ARM64 front end, x86_64 helper; strict deep signature verification passes |
| Installer | Two installations into a temporary test destination pass; previous build preserved outside its scanned VST3 directory |
| Local packaging | Binary and corresponding-source archives assembled; package links and archived dependency bootstrap checked |
| Publication review | Source/docs/theme only; no original engine, recovered factory files, manual, build output, private logs or personal source paths staged |

This fresh exact-audio pass does not resolve the intermittent tiny differences
recorded in [VERIFICATION.md](VERIFICATION.md). Those earlier results and the
comparison's strict assertions are retained. pluginval results in that record
are the earlier local results; pluginval was not rerun for this source-only
layout change. A new full DAW music session and Cubase project save/reopen were
not part of this publication check.

Local generated runtime, dependency checkout, build evidence and packages are
excluded from Git. Original downloads and the previous installed build were
not replaced by this import. The original repository's starter commit remains
in history, with its MIT notice preserved separately from the imported AGPL
integration.
