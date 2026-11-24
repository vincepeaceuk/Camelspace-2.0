# CamelSpace support-file recovery

Verified locally on 19 November 2026. This report concerns static recovery of the
user-supplied **CS Support Files.pkg** payload. It does not establish that the
plug-in's sound, interface, presets or host state operate correctly at runtime.

The old package is a bundle containing `Contents/Archive.pax`. Despite its name,
the payload is an **ASCII odc cpio** archive (`070707` magic), not a tar archive
and not a modern flat installer. The recovery tool reads those bytes directly.
It never invokes Installer, executes package scripts, or installs support files
in a system directory. Documents and skin settings inside the package are data.

The original archive is 2,089,472 bytes and has SHA-256
`ed88c7bc4c31ddd22c7b7d5a856d670cad006183c31761d16280e2b864493b7a`.
The generated `recovery-inventory.json` records the actual source path, archive
digest, recovered-file digests and complete static program inventory. Recheck
that digest when processing a different supplied package.

## Recovered material

The archive contains 88 entries: 11 directories and **77 regular files**. Every
recovered payload file was checked against its source byte length and SHA-256.

| Material | Count |
| --- | ---: |
| Original FXB bank files | 10 |
| Sequencer CSP presets | 25 |
| Original PNG skin images | 39 |
| SkinParameters.txt | 1 |
| CamelSpaceManual.pdf | 1 |
| Original Thumbs.db | 1 |

All source bytes are preserved, including blank preset slots and duplicate
banks. Source executable permission bits are not reproduced on recovered data.
The original background image is preserved at 570 × 500 pixels.

| Bank | Slots | Named slots | Blank names |
| --- | ---: | ---: | ---: |
| -Default.fxb | 64 | 64 | 0 |
| Basic FX.fxb | 64 | 48 | 16 |
| Drums.fxb | 64 | 64 | 0 |
| Guitar.fxb | 64 | 25 | 39 |
| Pads.fxb | 64 | 64 | 0 |
| Rhythmic1.fxb | 64 | 64 | 0 |
| Rhythmic2.fxb | 64 | 64 | 0 |
| Sound Effects.fxb | 64 | 64 | 0 |
| Synth.fxb | 64 | 61 | 3 |
| Vocals.fxb | 64 | 22 | 42 |

There are **640 stored program slots**, of which **540 have names** and 100 have
blank names. `-Default.fxb` and `Rhythmic1.fxb` are byte-identical (SHA-256
`079ef3ca4ed1e28d1fe92e89864b276acca3ddedd94fe01bbbd0b1d21f2512d8`).
Excluding that duplicate bank leaves 576 slots with 476 named records. Those
records contain 475 distinct name strings: two different records are named
`Wobble`. These are structural counts; they do not certify audible uniqueness
or that every blank slot is musically empty.

## Bank format observed in this package

All ten files have big-endian VST chunk-bank headers (`CcnK`, `FBCh`), format
version 1, plug-in ID `CaS1`, 64 declared programs and a chunk starting at byte
160. The original `byteSize` field counts the entire file, whereas the common
VST file convention excludes the first eight bytes. The parser accepts either
documented convention and still requires exact agreement with the chunk length.

The body was decoded as 64 consecutive records, each containing a little-endian
32-bit float `1.0`, a NUL-terminated UTF-8 name, a little-endian integer `344`,
and 344 little-endian 32-bit values. Parsing all declared records consumes every
body byte exactly in all ten banks. This record layout is inferred from the
supplied bytes, not recovered original source documentation. No conversion of
the values is performed. The inventory includes each record's file offset,
length, name, slot index and digest so a host adapter can load the source bank
or identify an individual stored record without guessing offsets.

## Reproduce

From this repository root, run:

```sh
CAMELSPACE_SUPPORT_ARCHIVE='/path/to/CS Support Files.pkg/Contents/Archive.pax' \
  python3 -m unittest discover -s tests -p 'test_recover_support.py' -v
python3 tools/recover_support.py '/path/to/CS Support Files.pkg/Contents/Archive.pax' '/new/output/directory'
```

The real recovery for this task is in `local/recovered/`, excluded
from Git. Its support-data directory is
`Library/Application Support/Camel Audio/CamelSpaceData/`; the manual is
`Applications/Camel Audio/CamelSpaceManual.pdf` within the recovery root.

The **9 tests pass**. They cover normal unaligned odc records, traversal and
absolute-path rejection, symlink/device/hardlink handling, duplicate paths,
truncation, missing trailers and size limits, refusal to overwrite existing
output, unchanged input bytes, chunk-bank decoding, malformed bank lengths,
the legacy size-field convention and the supplied real payload counts.
The tool validates the complete archive and bank structures before creating
output, writes only to a new destination, and uses directory-relative file
operations with `O_NOFOLLOW` so archive paths cannot traverse symlinks.

The initial RED run failed because `recover_support.py` did not exist; after
implementation, the fixture checks exposed the real banks' legacy `byteSize`
convention. That quirk has its own regression test. No audio, GUI or VST3 claim
is based on these recovery tests.
