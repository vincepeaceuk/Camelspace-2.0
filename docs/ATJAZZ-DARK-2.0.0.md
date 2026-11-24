# Atjazz classic dark edition 2.0.0

The requested changes are limited to the pale panel material, company mark,
and version text. Richard explicitly requires the original buttons, knobs,
control styling, bitmap fonts and layout. Both the 2.0.0 version and Randomize
lettering are white.

The original control sprite rendering has been restored. The original silver
knobs, blue/grey on/off buttons, selector arrows, pattern buttons, copy/paste
buttons, XY display, meters and sequencer graphics are retained. The dark
background is an imagegen recolour of the original empty panel plate. The
original black displays are then composited back at their exact coordinates,
and every original control sprite is drawn in its original position. The
Randomize sprite is tinted only at its red lettering; its original alpha,
glyph positions and button frame are retained. Version glyphs use the original
bitmap font with a white tint. Original support files remain unchanged.

The authentic existing transparent Atjazz PNG is embedded unchanged and drawn
on the upper-right nameplate with a light tint. Source/background edit details
and the exact prompt are in `assets/PANEL-DARK-EDIT.md`.

The displayed, bundle and VST3 version is 2.0.0; vendor is Atjazz Record Company.
Existing bundle identifier, manufacturer code, plug-in code and both VST3 class
IDs remain unchanged:

- `ABCDEF019182FAEB5270527643615332`
- `ABCDEF011234ABCD5270527643615332`

The UI regression checks compare original opaque knob and on/off-button pixels
exactly, check white Randomize ink at its original glyph positions, and exercise
all existing gestures and all 25 sequencer presets. The original Intel engine,
Rosetta helper, factory banks and state formats are retained. Final automated
results are in `Verification/evidence` in the release package.

The installer preserves earlier builds outside the scanned VST3 directory.
Restart the DAW to load the corrected classic skin; rescan if the vendor/version
metadata remains cached.

Validation scope: UI/host/state/recovery/parser checks and pluginval strictness 5
pass. Exact audio matching is intermittent despite unchanged engine/helper
bytes. One aligned-seed matrix had 494 non-exact cases (max difference
0.0000025183); a subsequent isolated-input matrix passed all 6,480 cases exactly.
No input mutation was observed, and the tiny-difference cause remains unknown.
The input-copy test hardening must not be described as a proven DSP fix.

A no-bridge probe with two direct original instances also reproduced tiny
cross-instance differences in 54/64 pairs (max 0.000000719), with isolated inputs
and matched libc seeds. The observed global RNG and floating-point mode were
unchanged; the precise hidden state is unidentified. This is recorded as a
limit of deterministic original-instance comparisons, not as a skin regression.
