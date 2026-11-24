# Licensing and notices

The Camel Space 2 restoration's integration source, native editor implementation,
helper/client code, tests and build tools are licensed under the GNU Affero
General Public License, version 3 or (at your option) any later version
(**AGPL-3.0-or-later**). See [LICENSE](LICENSE) for the full text.

The combined JUCE-based plug-in is built using JUCE's AGPL licensing option.
JUCE 9.0.5 is fetched separately by `tools/bootstrap.py`; its notices and SPDX
inventory are preserved in `licenses/JUCE-LICENSE.md` and `licenses/JUCE.spdx.json`.
The VST3 SDK licence notice is in `licenses/VST3-SDK-LICENSE.txt`. The integration
does not require the original VST2 SDK.

The repository's initial MIT notice is preserved verbatim at
`licenses/INITIAL-REPOSITORY-MIT.txt` for the initial repository material. It does
not relicense the subsequently imported AGPL integration or JUCE.

## Excluded from the AGPL grant

The original Camel Audio plug-in, factory banks, sequencer presets, control
artwork, manual and other recovered support files remain subject to their
original rights. They are not included in this public repository. Build and
recovery tools take the user's own compatible files as local inputs. Local
binary packages produced by those tools contain original materials; no right
to redistribute them is asserted here.

The Atjazz Record Company logo, modified classic panel artwork, CamelSpace name
and any third-party marks retain their respective owners' rights. Their presence
as theme assets or in the documentation screenshot does not place them under
AGPL or grant rights to use the marks. See `assets/README.md` and
`assets/PANEL-DARK-EDIT.md` for artwork provenance.

This project is an independent restoration, not an official Camel Audio,
Apple, JUCE or Steinberg release. The applicable licences provide the software
without warranty.
