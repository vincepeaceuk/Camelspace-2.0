# Support

Camel Space 2 is an independent experimental restoration. Support is provided
through [GitHub Issues](https://github.com/vincepeaceuk/Camelspace-2.0/issues) on a
best-effort basis; it is not supported by Camel Audio, Apple, JUCE or Steinberg.

## Plug-in is missing from the DAW

1. Confirm the bundle is at `~/Library/Audio/Plug-Ins/VST3/Camel Space 2.vst3`.
   In Finder, **Go → Go to Folder…** opens this otherwise hidden Library path.
2. Confirm the host supports macOS VST3 and that you are looking under audio
   effects, with vendor **Atjazz Record Company**.
3. Close and reopen the DAW, then use its plug-in rescan/re-enable procedure if
   it has cached a failed scan. Avoid keeping two copies with the same identity
   in different scanned VST3 folders.
4. Confirm Rosetta 2 is installed. The front end is native ARM64, but the engine
   and helper are Intel executables.

AU-only hosts cannot load the current build. There is no AU `.component` yet.

## Source setup or build fails

- Run `python3 tools/bootstrap.py` from the checkout. If an existing JUCE checkout
  has the wrong revision, move it aside yourself and rerun bootstrap; the tool
  refuses to overwrite it.
- Supply the matching original VST2 bundle and directory-style support package.
  This repository does not contain the original runtime or a substitute engine.
- A missing `local/runtime/programs.json` means resource preparation has not
  completed. Rerun the preparation command with your actual file paths.
- `local/recovered/` is reused once created. To prepare a different source
  package, move your previous `local/` directory aside first. Other binary
  versions require ABI validation and are not currently supported.
- Verify Apple developer tools, CMake, Ninja, Python 3.11+ and Rosetta are
  available. Keep the configure/build error and the first failing test result.

## The effect is dry, presets are missing, or the helper fails

Check that the **whole** VST3 bundle was installed. It must contain its helper,
original engine and `CamelSpaceData` under `Contents/Resources/`. Do not copy only
the executable or edit resources inside a signed installed bundle. Rebuild and
reinstall a complete bundle if anything is missing.

Dry audio can be the bridge's fallback for a failed, stalled or busy helper.
Report whether this happens on initial load, on preset changes or during
playback. For timing/random-LFO comparisons include transport state, tempo,
rate and buffer size; two original instances need not produce identical random
sequences.

## macOS blocks a package

The current local build is ad hoc signed and is not Developer ID notarised.
A downloaded locally prepared package may therefore be blocked by Gatekeeper.
Prefer building from the reviewed source on your Mac. Do not disable macOS
security globally. If a bundle fails signature verification, rebuild/reinstall
rather than bypassing the failed check. Include the exact macOS message in an
issue without sharing personal file paths.

## Old appearance or version is still visible

Quit the DAW completely and reopen it so it reloads the bundle. Rescan if the
vendor or version remains cached. Version 2.0.0 retains the original silver
knobs, blue/grey buttons and bitmap labels; the panel is dark, the company mark
is Atjazz, and **2.0.0** and **Randomize** are white.

## Roll back or uninstall

Close the DAW first. The installer preserves previous builds under
`~/Library/Audio/Plug-Ins/Camel Space 2 Backups/`, outside the scanned VST3 tree.
Replace the installed `Camel Space 2.vst3` with the selected backup, or remove
that installed bundle to uninstall. Keep backups outside scanned plug-in folders.
User-created CSP files remain under
`~/Library/Application Support/Camel Space 2/`.

## Report an issue

Include:

- macOS version and Apple chip model;
- DAW name/version and whether it runs natively or through Rosetta;
- Camel Space 2 version, source commit if built yourself, and original engine
  version (no binary attachment);
- sample rate, buffer size, tempo and transport state;
- preset/bank name, actions taken, expected result and actual result;
- a small relevant error excerpt or screenshot, with personal details removed.

For test failures, give the failing target name and a short excerpt from
`build/Testing/Temporary/LastTest.log`. Check [the verification record](docs/VERIFICATION.md)
for known limits, especially exact-audio differences and untested host workflows.
Do not upload credentials, private session files, the original engine, manual
or factory data. New hosts/OS versions are unverified until actually tested.
