# Original CamelSpace editor investigation

Executed on 19 November 2026, macOS 26.6.2, Apple Silicon with the supplied
CamelSpace 1.50.1 x86_64 engine running through Rosetta. This is a bounded
feasibility investigation, not a claim about every historical host or macOS.

## Result

The original editor opens and accepts mouse input after its skin resources are
resolved. Its drawing does not appear in either tested bitmap-capture path.
Forwarding those captures would produce a blank panel, so the VST3 editor needs
to draw the recovered original artwork using a current graphics implementation.

| Check | Executed result |
| --- | --- |
| Original bank import | PASS: `effSetChunk` accepts the original FXB body beginning at byte 160; `effSetProgram(0)` supplies the original values |
| Parameter inventory | PASS: all 344 names, normalized values and original formatted displays read from the engine |
| Resource path | Original runtime fields resolve `/Library/Application Support/Camel Audio/CamelSpaceData` |
| User support directory / current working directory / host directory callback | Did not change that hardcoded resource lookup |
| Skin loading | PASS with a resource-only probe shim: original editor rectangle is 570 × 500 and `effEditOpen` returns 1 |
| Original Cocoa view | PASS: a `VSTGUI_NSView` child is created at 570 × 500 |
| Original input | PASS: synthetic left-down/up at the TranceGate switch invokes `audioMasterAutomate(0, 0)` and changes original parameter 0 from 1 to 0 |
| `NSBitmapImageRep` cache capture | FAIL: saved 1140 × 1000 Retina bitmap has no pixel variation, before or after the click |
| Direct `drawRect:` into bitmap context | FAIL: a red sentinel background remains unchanged |

The tested window and original child view had `wantsLayer = NO`, and automatic
layer backing was disabled through process user defaults. These settings did
not restore the original drawing. No Screen Recording or Accessibility
permission was needed for these in-process bitmap and event probes.

## Isolation and evidence

The input bundle in Downloads was not edited. The test used the project's
separate working copy. No system support installation, security change or
authentication change was made. An absent user support symlink was tried and
then removed because it had no effect. The original engine wrote a new
`MidiConfig.txt` during probe shutdown; that probe-generated file was removed
from the recovered data directory afterward.

`tests/EditorProbe.mm` contains the standalone test and an optional test-only
resource shim. The shim maps only the original CamelSpaceData path prefix to a
caller-selected recovered data directory for PNG and text lookup. It does not
change the plug-in binary or DSP. The shim was built as a separate x86_64 dylib
and injected into the probe process, rather than the DAW.

Local evidence, excluded from source control:

- `local/analysis/parameter-metadata.txt`: all 344 original parameter records.
- `local/analysis/editor-probe-shim-evidence.log`: original view creation,
  resource lookups, mouse callback and uniform-bitmap acceptance failure.
- `local/analysis/editor-probe-shim-evidence/original-editor.png` and
  `original-editor-after-click.png`: uniform captured bitmaps.
- `local/analysis/editor-probe-shim-evidence/original-editor-direct-draw.png`:
  unchanged red sentinel in the direct drawing test.

The final probe returns status **5** when the capture is uniform. That failure
is retained as negative evidence for the original-editor-forwarding approach;
successful mouse dispatch is not treated as successful visual rendering.

## Exact morph destination mapping

The engine exposes an exported `ModulationTarget` data symbol. For the supplied
x86_64 slice, it contains the following 23 little-endian 16-bit parameter IDs:

```text
8, 10, 11, 15, 16, 19, 20, 22, 24, 26, 28, 29,
33, 35, 38, 39, 42, 43, 44, 45, 46, 4, 5
```

Static inspection of `CamelSpaceEditor::valueChangedCustom` confirms that the
original editor uses this table to convert the destination selector index into
the edited parameter ID. It does not use the full parameter list directly.
For example, default-bank `MorphXParam = 0.14` selects destination index 14,
which maps to `MasterMix` (parameter 38); `MorphYParam = 0.02` selects index 2,
which maps to `MmFilterRes` (parameter 11).

The selector labels were also read through the original
`CAPlugin::getParamShortName` method, which the original editor calls while
building both menus. The exact names are retained in
`local/analysis/morph-short-names.txt`.

## Sequencer drawing details

Bounded inspection of the original `StepSeq` constructor, its `draw` function
and the calls in `CamelSpaceEditor::createCustomControl` established:

- Each of the 16 steps uses integer `controlWidth / 16` width, with a **3-pixel
  gap** subtracted from the right edge of its drawn bar.
- For the 50-pixel-high volume control, the active bitmap is cropped to source
  rows 0–49. The 56-pixel strip is not scaled to 50 pixels. A value `v` draws
  from source y = `int((1 - v) * 50)` down to control y = 50.
- `StepSeqMeterActiveB` is used for indices 0, 4, 8 and 12. The alternate color
  marks groups of four steps; it is not a playback position.
- The background is the recovered original `Background` bitmap, cropped from
  the control's position in that full image. The separate passive-meter image
  is not supplied to these control constructors.
- Both volume and tie control constructors receive a null position-pointer
  argument. The original `StepSeq::draw` does not read a playback-position
  field. Adding a moving playhead would therefore add behavior that is absent
  from this supplied editor.

The original inner engine declares 366 parameters, including internal GUI
controls. Internal parameter 353, `CurrentPattern`, is read by the original
editor to choose which stored pattern's 16 values the controls display. It has
not been established as a playback-chain position. Internal names are recorded
in `local/analysis/internal-parameters.txt`.

## Original preset operations

Direct calls to the original exported `CAPluginVST::savePreset(char*)` and
`loadPreset(char*)`, using the verified VST wrapper pointer, returned success
and round-tripped all 344 parameter float bit patterns. `clearPreset()` changed
119 of those values in the tested edited default program and named it `blank`.

The saved `original-saved-preset.fxp` has `CcnK` / `FPCh` / `CaS1` identifiers
and a one-program payload beginning at byte 60. This original writer produced
a legacy `byteSize` header value of 152 and a zero `chunkSize`, despite the
actual file being 1,466 bytes. A compatibility importer must validate the
actual program body rather than assume this writer fills standard length
fields correctly. The body starts with little-endian float version 1.0,
a NUL-terminated program name, little-endian integer 344 and 344 floats.
