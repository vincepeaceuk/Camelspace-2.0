# Camel Space 2 verification

Current release: **2.0.0 Atjazz classic dark edition**. The original knobs,
buttons, bitmap fonts and control layout are retained; only the panel material,
company mark, version and requested Randomize text colour change. Original
opaque knob/button sprite pixels are compared exactly, and white Randomize
lettering is checked at the original glyph positions. The native host, UI,
recovery, state/files and parser-bound tests pass. Both VST3 class IDs are stable.

The exact audio comparison is intermittent: an aligned-seed run had 494 of
6,480 cases differ by at most 0.0000025183 despite equal parameter/state values.
The final isolated-input diagnostic matched all 6,480 cases / 45,342,720 samples
bit for bit. It observed no input mutations, so separate input copies harden
the test but are not established as the cause/fix. Both results are retained.
A separate isolated-input probe reproduced differences in 54/64 pairs of two
direct original instances, with no bridge/helper, max 0.000000719 and matched
libc seeds. Observed global RNG and floating-point mode stayed fixed; precise
hidden state remains unidentified. This behavior can occur in the original.
The original engine and Rosetta helper remain byte-identical to the previously
installed build; no DSP change was made for this skin. pluginval strictness 5
and the installed native-host checks pass. See [ATJAZZ-DARK-2.0.0.md](ATJAZZ-DARK-2.0.0.md).

The original 0.1.0 revival baseline and its remaining acceptance limits follow.

Executed 19 November 2026 on arm64 macOS 26.6.2 with Rosetta available.

| Check | Result |
| --- | --- |
| Native release build | ARM64 VST3 front end, x86_64 helper, copied original VST2 engine |
| Recovery | 77 files preserved byte for byte; 10 FXB, 25 CSP, 39 PNG, skin settings, manual and Thumbs.db |
| Recovery tests | 9/9, including traversal/link/device/truncation handling and all original bank records |
| CTest | Seven targets: bridge, recovery, native VST3, processor files/recall, skin controls, audio equivalence, legacy parser bounds |
| Native VST3 host | Instantiates, all 344 original parameter names/indices retained, 476 factory entries process finite audio, native editor attaches at 570×500 |
| Extra host parameters | JUCE's VST3 host also exposes 2,080 MIDI-controller proxies plus program/bypass controls; these are not additional CamelSpace DSP parameters |
| Audio equivalence | 6,480/6,480 cases, 45,342,720 samples, zero differing float bits, all finite |
| Rates / blocks | 44,100, 48,000, 96,000, 192,000 Hz / 32, 256, 1024 frames |
| Tempo change | Additional 196,608 samples, 120→87 BPM, exact comparison |
| Original state | Full bank state equality, edited restore and all 344 parameter values checked |
| Host project state | Selected factory entry, edited original values, MIDI Learn channel/controller and selected editing pattern survive independent-instance recall |
| FXP / FXB | Native save/load passes; unmodified original engine reimports exported files and all 344 values match |
| Native skin | 23 focused gesture/render/CSP checks; all 25 CSPs load, saved floats round-trip, malformed CSP is ignored |
| Helper failure | Missing helper, process death, stopped helper during display polling and large state send are bounded; audio keeps dry input on unavailable/busy helper |
| Legacy input parser | Long names and out-of-range/NaN values rejected before the old parser; validates record count/lengths |
| pluginval 1.0.4 | Strictness 5 PASS, GUI checks included, same four rates and three block sizes |
| Signatures | Strict local bundle verification after resource copying/signing |
| Installed copy | Native ARM host and pluginval strictness 5 pass using the actual user VST3 folder |
| Installer/archive | Two installations preserve the earlier bundle; actual ZIP extraction and installation pass strict deep signature verification |

The exact audio test compares a direct x86_64 original instance to an original
instance reached through the bridge. The separate bridge and VST3 tests execute
the ARM64 client, covering the actual cross-architecture path. Original libc RNG
seeding uses `time(NULL)`; the exact comparison checks that both unchanged
constructors initialize in the same clock second. A retained earlier run with
unmatched seed seconds demonstrates that random LFO differences also occur
between original instances. No DSP or RNG instructions were patched.

The original GUI cannot provide a working pixel reference on this Mac. Its
artwork, skin coordinates, bitmap fonts, selector strings, morph destinations
and sequencer strip geometry were recovered from the original files/binary.
The native panel is visually inspected and exercised; this is not a certified
pixel-perfect comparison against a running historical editor. The original
sequencer control receives no playhead pointer and draws no moving playhead;
the restoration follows that behavior.

The first hosted test reproduced a JSON parsing crash caused by Objective-C++
JUCE symbols being externally visible when both the host and plug-in loaded
JUCE. Setting Objective-C++ visibility to hidden fixes that crash; native
host tests and pluginval now pass. An early host test also assumed all host
parameters were original DSP controls; it was corrected to assert all 344
original names/indices while allowing standard JUCE MIDI proxies.

Focused test-first evidence: bridge acceptance initially failed compilation
because BridgeClient was absent. Parser-boundary tests initially accepted a
64-character name and values outside [0,1]; all three now fail safely before
dispatch to the unchecked legacy parser. Recovery tests initially failed for
the missing recovery implementation, and now pass. No line-coverage percentage
is claimed.

Local evidence lives in `build/evidence/`, `build/Testing/Temporary/LastTest.log`,
and `local/analysis/`. The release package includes selected final logs and the
native panel image. `docs/EDITOR-INVESTIGATION.md` records the failed original
drawing probes separately from successful DSP/native UI evidence.

Not executed: a full Cubase music session, Cubase project save/reopen, an official
Steinberg SDK validator, other macOS releases/Intel Macs, long-duration physical
MIDI testing, Developer ID signing or notarisation. The new VST3 does not migrate
old VST2 project instances automatically. A busy, stalled or failed helper can
cause temporary dry fallback; this development bridge is not a hard real-time
guarantee. No system security settings were changed.
