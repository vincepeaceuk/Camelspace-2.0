#include "SkinPanel.h"
#include "CamelSpaceBrandData.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <vector>

namespace cs
{
namespace
{
const juce::StringArray filterNames {
    "LP2 BQ", "LP2 Fat", "BP2 BQ", "BP2 Fat", "HP2 BQ", "HP2 Fat", "Formant",
    "Peaking", "Notch", "CombP", "CombM", "RingMod", "Tube", "Mech", "Bit Red",
    "LP2 SVF HQ", "LP2 K20 HQ", "LP2 XP HQ", "LP2 MG HQ", "LP4 XP HQ", "LP4 MG HQ",
    "BP2 SVF HQ", "BP2 K20 HQ", "BP2 XP HQ", "BP2 MG HQ", "BP4 XP HQ", "BP4 MG HQ",
    "HP2 SVF HQ", "HP2 K20 HQ", "HP2 XP HQ", "HP2 MG HQ", "HP4 XP HQ", "HP4 MG HQ"
};
const juce::StringArray shapeNames {
    "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Random Square",
    "Random Triangle", "Inverted Sine", "Inverted Triangle", "Inverted Square"
};
// Original exported ModulationTarget uint16 table, verified from the supplied
// x86_64 binary. Morph selectors store the table index divided by 100.
constexpr std::array<int, 23> morphTargets {
    8, 10, 11, 15, 16, 19, 20, 22, 24, 26, 28, 29, 33, 35, 38, 39,
    42, 43, 44, 45, 46, 4, 5
};
const juce::StringArray morphLabels {
    "MmFilterMix", "MmFilterCutoff", "MmFilterRes", "FiltModDepth", "FiltModRate", "EnhXcita",
    "EnhSoftSat", "FlangerMix", "FlangerRate", "FlangerFbck", "DelayMix", "DelayFbck",
    "DelayCutoff", "ReverbMix", "MasterMix", "MasterVolume", "SeqAttack", "SeqSustain",
    "SeqDecay", "SeqShuffle", "SeqLength", "PanDepth", "PanRate"
};
}

struct SkinPanel::Impl
{
    struct Font
    {
        juce::Image image;
        juce::String characters;
        std::vector<int> widths, offsets;
    };
    struct Control
    {
        juce::String name, type, image;
        juce::Rectangle<int> bounds;
        int parameter = -1;
    };

    SkinPanel& owner;
    Callbacks cb;
    juce::File dataDirectory, skinDirectory;
    std::map<juce::String, juce::Image> images;
    std::map<juce::String, Font> fonts;
    std::vector<Control> controls;
    std::unique_ptr<juce::FileChooser> chooser;
    int pattern = 0, activeControl = -1, draggingParameter = -1, lastStep = -1;
    float dragStart = 0, binaryPaintValue = 0;
    juce::Point<float> downPosition, xyPosition { 0.5f, 0.5f };
    bool circular = false, metersEnabled = false, copied = false;
    std::array<float, 32> patternClipboard {};
    juce::String valueReadout;
    double readoutUntil = 0;
    juce::Image darkBackground,brand;

    Impl(SkinPanel& panel, juce::File directory, Callbacks callbacks)
        : owner(panel), cb(std::move(callbacks)), dataDirectory(std::move(directory))
    {
        darkBackground=juce::ImageFileFormat::loadFrom(CamelSpaceBrandData::paneldark_png,CamelSpaceBrandData::paneldark_pngSize);
        brand=juce::ImageFileFormat::loadFrom(CamelSpaceBrandData::atjazzrecordcompany_png,CamelSpaceBrandData::atjazzrecordcompany_pngSize);
        skinDirectory = dataDirectory.getChildFile("Skins/default");
        if (!skinDirectory.isDirectory()) skinDirectory = dataDirectory;
        for (auto file : skinDirectory.findChildFiles(juce::File::findFiles, false, "*.png"))
            images[file.getFileNameWithoutExtension()] = juce::ImageFileFormat::loadFrom(file);
        // Recolour only the red lettering in the original Randomize sprite.
        // Its frame, pressed-state geometry and original glyph shapes remain.
        if(auto it=images.find("ButtonRandom");it!=images.end())
        {
            auto whiteText=it->second.createCopy();
            juce::Image::BitmapData pixels(whiteText,juce::Image::BitmapData::readWrite);
            for(int y=0;y<pixels.height;++y)for(int x=0;x<pixels.width;++x)
            {
                const auto c=pixels.getPixelColour(x,y);
                const int red=c.getRed(),green=c.getGreen(),blue=c.getBlue();
                if(red>green+12 && red>blue+12)
                {
                    const float coverage=juce::jlimit(0.0f,1.0f,(red-juce::jmax(green,blue))/45.0f);
                    const auto shade=juce::uint8(juce::roundToInt(255*coverage+(green+blue)*.5f*(1-coverage)));
                    pixels.setPixelColour(x,y,juce::Colour(shade,shade,shade,c.getAlpha()));
                }
            }
            it->second=whiteText;
        }
        loadLayout();
    }

    int index(const juce::String& name) const { return cb.parameterNames.indexOf(name); }
    float value(int id) const
    {
        if (id < 0 || id >= cb.parameterNames.size() || !cb.getValue) return 0;
        const auto result = cb.getValue(id);
        return std::isfinite(result) ? juce::jlimit(0.0f, 1.0f, result) : 0.0f;
    }
    float value(const juce::String& name) const { return value(index(name)); }
    int discrete(int id, int count) const
    {
        return juce::jlimit(0, count - 1, juce::roundToInt(value(id) * 100.0f));
    }
    juce::Image image(const juce::String& name) const
    {
        auto it = images.find(name);
        return it == images.end() ? juce::Image{} : it->second;
    }
    void loadLayout()
    {
        const auto lines = juce::StringArray::fromLines(
            skinDirectory.getChildFile("SkinParameters.txt").loadFileAsString());
        bool inControls = false;
        for (int i = 0; i < lines.size(); ++i)
        {
            const auto trimmed = lines[i].trim();
            if (trimmed == "CONTROLS") { inControls = true; continue; }
            if (i + 3 < lines.size() && trimmed.startsWith("Font")
                && lines[i + 1].trim().containsOnly("0123456789")
                && lines[i + 1].getIntValue() > 0)
            {
                Font f;
                f.image = image(trimmed);
                f.characters = lines[i + 2];
                auto widths = juce::StringArray::fromTokens(lines[i + 3], false);
                int offset = 0;
                for (auto width : widths)
                {
                    f.offsets.push_back(offset);
                    f.widths.push_back(width.getIntValue());
                    offset += width.getIntValue();
                }
                if (f.characters.length() == static_cast<int>(f.widths.size()))
                    fonts[trimmed] = std::move(f);
            }
            if (!inControls || trimmed.isEmpty() || trimmed.startsWith("---")) continue;
            auto tokens = juce::StringArray::fromTokens(trimmed, false);
            tokens.removeEmptyStrings();
            if (tokens.size() < 6) continue;
            Control c { tokens[0], tokens[1], tokens[2], {}, index(tokens[0]) };
            const int x = tokens[4].getIntValue(), y = tokens[5].getIntValue();
            if (tokens.size() >= 8)
                c.bounds = { x, y, tokens[6].getIntValue() - x, tokens[7].getIntValue() - y };
            else
            {
                auto bitmap = image(c.image);
                int w = bitmap.getWidth(), h = bitmap.getHeight();
                if (c.type == "Knob") h = w;
                else if (c.type == "OnButton" || c.type == "KickButton") h /= 2;
                else if (c.type == "Selector" || c.type == "UpDownPreset") h /= 3;
                c.bounds = { x - w / 2, y - h / 2, w, h };
            }
            controls.push_back(std::move(c));
        }
    }
    juce::Point<float> position(const juce::MouseEvent& e) const
    {
        return { e.position.x * nativeWidth / juce::jmax(1, owner.getWidth()),
                 e.position.y * nativeHeight / juce::jmax(1, owner.getHeight()) };
    }
    int hit(juce::Point<float> p) const
    {
        for (int i = static_cast<int>(controls.size()); --i >= 0;)
            if (controls[static_cast<size_t>(i)].bounds.toFloat().contains(p)) return i;
        return -1;
    }
    juce::String sequenceName(bool tie, int step) const
    {
        return "Seq" + juce::String(pattern + 1) + (tie ? "Tie" : "Step")
               + juce::String(step + 1) + "Vol";
    }
    juce::String routeName(const Control& c) const
    {
        if (c.name == "SeqFilterStartClk") return "SeqPat" + juce::String(pattern + 1) + "FiltOn";
        if (c.name == "SeqPanStartClk") return "SeqPat" + juce::String(pattern + 1) + "PanOn";
        if (c.name == "SeqTGateStartClk") return "SeqPat" + juce::String(pattern + 1) + "TGateOn";
        return c.name;
    }
    void readout(int id)
    {
        if (id < 0 || id >= cb.parameterNames.size()) return;
        valueReadout = cb.parameterNames[id] + ": "
            + (cb.getText ? cb.getText(id) : juce::String(value(id) * 100.0f, 1));
        readoutUntil = juce::Time::getMillisecondCounterHiRes() + 3000;
        owner.repaint();
    }
    void selectPattern(int next)
    {
        next = juce::jlimit(0, 7, next);
        if (pattern == next) return;
        pattern = next;
        if (cb.patternChanged) cb.patternChanged(pattern);
        owner.repaint();
    }
    void set(int id, float v, bool oneGesture = true)
    {
        if (id < 0 || id >= cb.parameterNames.size() || !cb.setValue) return;
        if (oneGesture && cb.beginGesture) cb.beginGesture(id);
        cb.setValue(id, juce::jlimit(0.0f, 1.0f, v));
        if (oneGesture && cb.endGesture) cb.endGesture(id);
        readout(id);
    }
    void set(const juce::String& name, float v) { set(index(name), v); }
    const juce::StringArray& choices(const Control& c) const
    {
        if (c.name == "MmFilterType") return filterNames;
        if (c.name == "MorphXParam" || c.name == "MorphYParam") return morphLabels;
        return shapeNames;
    }
    void sprite(juce::Graphics& g, const juce::Image& bitmap,
                juce::Rectangle<int> r, int frame, int frameHeight) const
    {
        if (bitmap.isNull() || frameHeight <= 0) return;
        frame = juce::jlimit(0, bitmap.getHeight() / frameHeight - 1, frame);
        g.drawImage(bitmap, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                    0, frame * frameHeight, bitmap.getWidth(), frameHeight);
    }
    void text(juce::Graphics& g, const juce::String& message, juce::Rectangle<int> r,
              const juce::String& font = "FontSelector", bool centred = false) const
    {
        auto it = fonts.find(font);
        if (it == fonts.end() || it->second.image.isNull()) return;
        const auto& f = it->second;
        const bool version = font == "FontVersion";
        if (version) g.setColour(juce::Colour(0xffdce1e4));
        int total = 0;
        for (auto c : message)
        {
            const auto n = f.characters.indexOfChar(c);
            if (n >= 0) total += f.widths[static_cast<size_t>(n)];
        }
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(r);
        int x = r.getX() + (centred ? juce::jmax(0, (r.getWidth() - total) / 2) : 0);
        for (auto c : message)
        {
            int n = f.characters.indexOfChar(c);
            if (n < 0) n = f.characters.indexOfChar('?');
            if (n < 0) continue;
            const auto k = static_cast<size_t>(n);
            g.drawImage(f.image, x, r.getY(), f.widths[k], f.image.getHeight(),
                        f.offsets[k], 0, f.widths[k], f.image.getHeight(), version);
            x += f.widths[k];
            if (x >= r.getRight()) break;
        }
    }
    void drawControl(juce::Graphics& g, const Control& c, int controlIndex)
    {
        auto bitmap = image(c.image);
        const auto r = c.bounds;
        if (c.type == "Knob")
            sprite(g, bitmap, r, juce::roundToInt(value(c.parameter) * 54), bitmap.getWidth());
        else if (c.type == "OnButton")
            sprite(g, bitmap, r, value(routeName(c)) >= 0.5f ? 1 : 0, bitmap.getHeight() / 2);
        else if (c.type == "KickButton")
            sprite(g, bitmap, r, activeControl == controlIndex ? 1 : 0, bitmap.getHeight() / 2);
        else if (c.type == "RadioButSrc")
        {
            const int selected = discrete(c.parameter, 2);
            for (int i = 0; i < 2; ++i)
            {
                auto button = image(i == 0 ? "ButtonSrcLfo" : "ButtonSrcSeq");
                sprite(g, button, { r.getX() - 1, r.getY() + 22 * i, 38, 20 },
                       selected == i ? 1 : 0, 20);
            }
        }
        else if (c.type == "RadioButPattern")
        {
            for (int i = 0; i < 8; ++i)
                sprite(g, image("PatternBut" + juce::String(i + 1)),
                       { r.getX() + 19 * i, r.getY(), 19, 23 }, i == pattern ? 1 : 0, 23);
        }
        else if (c.type == "Selector")
        {
            sprite(g, bitmap, r, 0, bitmap.getHeight() / 3);
            const bool small = c.name.startsWith("Morph");
            auto field = r.reduced(small ? 16 : 25, 0).withY(r.getY() + 4).withHeight(12);
            const auto& labels = choices(c);
            text(g, labels[discrete(c.parameter, labels.size())], field, "FontSelector", true);
        }
        else if (c.type == "UpDownPreset") sprite(g, bitmap, r, 0, bitmap.getHeight() / 3);
        else if (c.type == "MorphSquare")
        {
            g.drawImageAt(bitmap, r.getX(), r.getY());
            auto cursor = image("XYCursor");
            g.drawImageAt(cursor, r.getX() + juce::roundToInt(xyPosition.x * (r.getWidth() - 9)),
                          r.getY() + juce::roundToInt((1 - xyPosition.y) * (r.getHeight() - 9)));
        }
        else if (c.type == "StepSeqVol")
        {
            for (int i = 0; i < 16; ++i)
            {
                // Original StepSeq::draw crops y0..50 of the 56px strip;
                // it does not resize it. Every fourth step uses ActiveB.
                // The passive grid is already present in Background.png.
                const int top = int((1.0f - value(sequenceName(false, i))) * r.getHeight());
                const int h = r.getHeight() - top;
                if (h > 0)
                {
                    const auto bitmap = image(i % 4 == 0 ? "StepSeqMeterActiveB" : "StepSeqMeterActive");
                    // The original constructor fixes the inter-step gap at3px.
                    g.drawImage(bitmap, r.getX() + 16 * i, r.getY() + top, 13, h,
                                0, top, 13, h);
                }
            }
        }
        else if (c.type == "StepSeqTie")
        {
            for (int i = 0; i < 16; ++i)
                if (value(sequenceName(true, i)) >= 0.5f)
                    g.drawImageAt(image("StepSeqTieOnOff"), r.getX() + 16 * i + 1, r.getY());
        }
        else if (c.type == "VuMeter")
        {
            g.drawImageAt(image("LevelMeterOff"), r.getX(), r.getY());
            if (metersEnabled && cb.getOutputLevel)
            {
                const int channel = c.name.endsWith("L") ? 0 : 1;
                const auto amp = juce::jmax(1.0e-6f, cb.getOutputLevel(channel));
                const auto proportion = juce::jlimit(0.0f, 1.0f, (20 * std::log10(amp) + 60) / 60);
                const auto h = juce::roundToInt(proportion * r.getHeight());
                juce::Graphics::ScopedSaveState state(g);
                g.reduceClipRegion(r.withTop(r.getBottom() - h));
                g.drawImageAt(image("LevelMeterOn"), r.getX(), r.getY());
            }
        }
        else if (c.type == "VersionDisplay") text(g, cb.versionText, r, "FontVersion");
    }
    void paint(juce::Graphics& g)
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(darkBackground,0,0,nativeWidth,nativeHeight,0,0,darkBackground.getWidth(),darkBackground.getHeight());
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        // Preserve the original black displays and grids exactly. Only the
        // light panel plate is recoloured; all control sprites remain original.
        for(auto r:{juce::Rectangle<int>{193,55,185,190},{132,404,313,91}})
            g.drawImage(image("Background"),r.getX(),r.getY(),r.getWidth(),r.getHeight(),r.getX(),r.getY(),r.getWidth(),r.getHeight());
        {
            juce::Graphics::ScopedSaveState state(g);
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.setColour(juce::Colour(0xffe6ebed));
            g.drawImage(brand,403,12,40,40,0,16,594,594,true);
            g.drawImage(brand,449,18,103,31,0,650,595,176,true);
        }
        for (size_t i = 0; i < controls.size(); ++i) drawControl(g, controls[i], static_cast<int>(i));
        juce::String display;
        if (juce::Time::getMillisecondCounterHiRes() < readoutUntil) display = valueReadout;
        else if (cb.getPresetName) display = cb.getPresetName();
        else if (cb.getPreset && juce::isPositiveAndBelow(cb.getPreset(), cb.presetNames.size()))
            display = cb.presetNames[cb.getPreset()].fromLastOccurrenceOf(" / ", false, false);
        if (cb.getStatus)
        {
            const auto status = cb.getStatus();
            if (status.isNotEmpty()) display = status;
        }
        text(g, display, { 201, 70, 158, 16 });
    }
    void selectPreset(int delta)
    {
        if (cb.presetNames.isEmpty() || !cb.setPreset) return;
        const int old = cb.getPreset ? cb.getPreset() : 0;
        cb.setPreset((old + delta + cb.presetNames.size()) % cb.presetNames.size());
        readoutUntil = 0;
        owner.repaint();
    }
    void fileAction(bool save, bool bank)
    {
        const auto callback = save ? (bank ? cb.saveBankFile : cb.savePresetFile)
                                   : (bank ? cb.loadBankFile : cb.loadPresetFile);
        if (!callback) return;
        chooser = std::make_unique<juce::FileChooser>(save ? "Save CamelSpace preset" : "Load CamelSpace preset",
            juce::File{}, bank ? "*.fxb" : "*.fxp");
        const int flags = save ? juce::FileBrowserComponent::saveMode
                                   | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode;
        chooser->launchAsync(flags | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), callback](const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File{}) return;
                callback(fc.getResult());
            });
    }
    void presetMenu()
    {
        juce::PopupMenu menu;
        const int current = cb.getPreset ? cb.getPreset() : -1;
        std::map<juce::String, juce::PopupMenu> banks;
        for (int i = 0; i < cb.presetNames.size(); ++i)
        {
            const auto& full = cb.presetNames[i];
            const auto bank = full.contains(" / ") ? full.upToFirstOccurrenceOf(" / ", false, false) : "Presets";
            const auto name = full.fromLastOccurrenceOf(" / ", false, false);
            banks[bank].addItem(i + 1, name.isEmpty() ? "(blank)" : name, true, i == current);
        }
        for (const auto& bank : banks) menu.addSubMenu(bank.first, bank.second);
        const int base = cb.presetNames.size() + 100;
        const int bankBase = base + 100;
        if (cb.getBankProgramNames && cb.setBankProgram)
        {
            juce::PopupMenu current;
            const auto names = cb.getBankProgramNames();
            for (int i=0;i<names.size();++i)
                current.addItem(bankBase+i,names[i].isEmpty()?"(blank)":names[i],true,cb.getBankProgram && cb.getBankProgram()==i);
            menu.addSubMenu("Current Bank",current);
        }
        menu.addSeparator();
        menu.addItem(base, "Load Preset...", bool(cb.loadPresetFile));
        menu.addItem(base + 1, "Save Preset...", bool(cb.savePresetFile));
        menu.addItem(base + 2, "Load Bank...", bool(cb.loadBankFile));
        menu.addItem(base + 3, "Save Bank...", bool(cb.saveBankFile));
        menu.addItem(base + 4, "Clear Preset", bool(cb.clearPreset));
        menu.addSeparator();
        menu.addItem(base + 5, "Manual", bool(cb.openManual));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner),
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), base, bankBase](int result)
            {
                if (safe == nullptr || result == 0) return;
                auto& p = *safe->impl;
                if (result >= bankBase && p.cb.setBankProgram) { p.cb.setBankProgram(result-bankBase);p.readoutUntil=0; }
                else if (result < base && p.cb.setPreset) { p.cb.setPreset(result - 1); p.readoutUntil = 0; }
                else if (result < base + 4) p.fileAction((result - base) % 2 != 0, result >= base + 2);
                else if (result == base + 4 && p.cb.clearPreset) p.cb.clearPreset();
                else if (result == base + 5 && p.cb.openManual) p.cb.openManual();
                safe->repaint();
            });
    }
    void selectorMenu(const Control& c)
    {
        juce::PopupMenu menu;
        const auto labels = choices(c);
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem(i + 1, labels[i], true, i == discrete(c.parameter, labels.size()));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner),
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), id = c.parameter](int result)
            { if (safe != nullptr && result > 0) safe->impl->set(id, (result - 1) / 100.0f); });
    }
    void knobMenu(int id)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Display Value");
        menu.addItem(2, "MIDI Learn", bool(cb.learnMidi));
        menu.addSeparator();
        menu.addItem(3, "Linear", true, !circular);
        menu.addItem(4, "Circular", true, circular);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner),
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), id](int result)
            {
                if (safe == nullptr) return;
                auto& p = *safe->impl;
                if (result == 1) p.readout(id);
                if (result == 2 && p.cb.learnMidi) p.cb.learnMidi(id);
                if (result == 3 || result == 4) p.circular = result == 4;
            });
    }
    void copyPattern()
    {
        for (int i = 0; i < 16; ++i)
        {
            patternClipboard[static_cast<size_t>(i)] = value(sequenceName(false, i));
            patternClipboard[static_cast<size_t>(i + 16)] = value(sequenceName(true, i));
        }
        copied = true;
    }
    void pastePattern()
    {
        if (!copied) return;
        for (int i = 0; i < 16; ++i)
        {
            set(sequenceName(false, i), patternClipboard[static_cast<size_t>(i)]);
            set(sequenceName(true, i), patternClipboard[static_cast<size_t>(i + 16)]);
        }
    }
    void sequenceMenu()
    {
        juce::PopupMenu menu;
        std::vector<juce::File> files;
        for (auto file : dataDirectory.getChildFile("Presets/StepSequencerPresets")
                            .findChildFiles(juce::File::findFiles, false, "*.csp"))
            files.push_back(file);
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b)
                  { return a.getFileName().compareNatural(b.getFileName()) < 0; });
        juce::PopupMenu presets;
        for (size_t i = 0; i < files.size(); ++i)
            presets.addItem(100 + static_cast<int>(i), files[i].getFileNameWithoutExtension());
        menu.addSubMenu("Load Preset", presets, !files.empty());
        menu.addItem(20, "Load Preset File...");
        menu.addItem(21, "Save Preset...");
        menu.addSeparator();
        const juce::StringArray commands { "Clear", "Vol All", "Vol Every 2", "Vol Every 4",
            "Tie All", "Move Left", "Move Right", "MIDI Trigger On", "MIDI Trigger Off" };
        for (int i = 0; i < commands.size(); ++i) menu.addItem(i + 1, commands[i]);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner),
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), files = std::move(files)](int result)
            {
                if (safe == nullptr || result == 0) return;
                auto& p = *safe->impl;
                if (result >= 100)
                {
                    const auto i = static_cast<size_t>(result - 100);
                    if (i < files.size()) p.loadPattern(files[i]);
                    return;
                }
                if (result == 20 || result == 21) { p.patternFileAction(result == 21); return; }
                if (result >= 8) { p.set("SeqMidiNoteTrigger", result == 8 ? 1.0f : 0.0f); return; }
                if (result == 6 || result == 7)
                {
                    std::array<float, 16> volumes {}, ties {};
                    for (int i = 0; i < 16; ++i)
                    { volumes[static_cast<size_t>(i)] = p.value(p.sequenceName(false, i));
                      ties[static_cast<size_t>(i)] = p.value(p.sequenceName(true, i)); }
                    for (int i = 0; i < 16; ++i)
                    { const auto source = static_cast<size_t>((i + (result == 6 ? 1 : 15)) % 16);
                      p.set(p.sequenceName(false, i), volumes[source]);
                      p.set(p.sequenceName(true, i), ties[source]); }
                    return;
                }
                for (int i = 0; i < 16; ++i)
                {
                    if (result == 5) p.set(p.sequenceName(true, i), 1);
                    else p.set(p.sequenceName(false, i), result == 1 ? 0.0f
                        : result == 2 || i % (result == 3 ? 2 : 4) == 0 ? 1.0f : 0.0f);
                }
            });
    }
    bool loadPattern(const juce::File& file)
    {
        juce::MemoryBlock bytes;
        // The supplied 25 CSP files all have this exact observed structure:
        // LE uint32 version1, LE uint32 count16, float[16] vol, float[16] tie.
        if (file.getSize() != 136 || !file.loadFileAsData(bytes) || bytes.getSize() != 136) return false;
        const auto* data = static_cast<const uint8_t*>(bytes.getData());
        if (juce::ByteOrder::littleEndianInt(data) != 1 || juce::ByteOrder::littleEndianInt(data + 4) != 16) return false;
        std::array<float, 32> values {};
        for (size_t i = 0; i < values.size(); ++i)
        {
            values[i] = std::bit_cast<float>(juce::ByteOrder::littleEndianInt(data + 8 + i * 4));
            if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > 1) return false;
        }
        for (int i = 0; i < 16; ++i)
        {
            set(sequenceName(false, i), values[static_cast<size_t>(i)]);
            set(sequenceName(true, i), values[static_cast<size_t>(i + 16)]);
        }
        return true;
    }
    bool savePattern(const juce::File& file) const
    {
        juce::MemoryOutputStream bytes;
        bytes.writeInt(1);
        bytes.writeInt(16);
        for (int tie = 0; tie < 2; ++tie)
            for (int step = 0; step < 16; ++step)
                bytes.writeFloat(value(sequenceName(tie != 0, step)));
        return file.replaceWithData(bytes.getData(), bytes.getDataSize());
    }
    void patternFileAction(bool save)
    {
        auto directory = save ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                   .getChildFile("Camel Space 2/Step Sequencer Presets")
                              : dataDirectory.getChildFile("Presets/StepSequencerPresets");
        if (save) directory.createDirectory();
        chooser = std::make_unique<juce::FileChooser>(save ? "Save Step Sequencer Preset" : "Load Step Sequencer Preset",
            directory, "*.csp");
        const int flags = save ? juce::FileBrowserComponent::saveMode
                                   | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode;
        chooser->launchAsync(flags | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<SkinPanel>(&owner), save](const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File{}) return;
                auto& p = *safe->impl;
                if (!save) { p.loadPattern(fc.getResult()); return; }
                p.savePattern(fc.getResult());
            });
    }
    void route(const Control& c)
    {
        const auto name = routeName(c);
        const bool on = value(name) < 0.5f;
        set(name, on ? 1.0f : 0.0f);
        if (!on) return;
        if (c.name == "SeqFilterStartClk")
        { set("MmFilterOn", 1); set("CutoffModOn", 1); set("CutoffModSource", 0.01f); }
        if (c.name == "SeqPanStartClk") { set("PanOn", 1); set("PanSource", 0.01f); }
        if (c.name == "SeqTGateStartClk") set("TranceGateOn", 1);
    }
    void editStep(const Control& c, juce::Point<float> p, bool first, bool right)
    {
        const int step = juce::jlimit(0, 15, int((p.x - c.bounds.getX()) / 16));
        const bool tie = c.type == "StepSeqTie";
        const auto name = sequenceName(tie, step);
        if (tie || value("SeqVolMode") < 0.5f)
        {
            if (first) binaryPaintValue = right ? 0 : value(name) < 0.5f ? 1.0f : 0.0f;
            if (first || step != lastStep) set(name, binaryPaintValue);
        }
        else set(name, right ? 0.0f : (c.bounds.getBottom() - p.y) / c.bounds.getHeight());
        lastStep = step;
    }
    void xy(const Control& c, juce::Point<float> p)
    {
        xyPosition = { juce::jlimit(0.0f, 1.0f, (p.x - c.bounds.getX()) / c.bounds.getWidth()),
                       juce::jlimit(0.0f, 1.0f, (c.bounds.getBottom() - p.y) / c.bounds.getHeight()) };
        const int x = discrete(index("MorphXParam"), static_cast<int>(morphTargets.size()));
        const int y = discrete(index("MorphYParam"), static_cast<int>(morphTargets.size()));
        set(morphTargets[static_cast<size_t>(x)], xyPosition.x);
        set(morphTargets[static_cast<size_t>(y)], xyPosition.y);
    }
    void down(const juce::MouseEvent& e)
    {
        const auto p = position(e);
        activeControl = hit(p);
        if (activeControl < 0) return;
        const auto& c = controls[static_cast<size_t>(activeControl)];
        const bool right = e.mods.isPopupMenu();
        downPosition = p;
        if (c.type == "Knob")
        {
            if (right) { knobMenu(c.parameter); activeControl = -1; return; }
            draggingParameter = c.parameter; dragStart = value(c.parameter);
            if (cb.beginGesture && draggingParameter >= 0) cb.beginGesture(draggingParameter);
            readout(c.parameter);
        }
        else if (c.type == "OnButton")
        {
            if (c.name.endsWith("StartClk")) route(c);
            else set(c.parameter, value(c.parameter) < 0.5f ? 1.0f : 0.0f);
        }
        else if (c.type == "RadioButSrc")
            set(c.parameter, p.y >= c.bounds.getY() + 22 ? 0.01f : 0.0f);
        else if (c.type == "RadioButPattern") selectPattern(int((p.x - c.bounds.getX()) / 19));
        else if (c.type == "Selector")
        {
            const int arrow = c.name.startsWith("Morph") ? 15 : 24;
            const auto& labels = choices(c);
            if (p.x < c.bounds.getX() + arrow || p.x >= c.bounds.getRight() - arrow)
            {
                const int delta = p.x < c.bounds.getCentreX() ? -1 : 1;
                set(c.parameter, ((discrete(c.parameter, labels.size()) + delta + labels.size())
                                  % labels.size()) / 100.0f);
            }
            else selectorMenu(c);
        }
        else if (c.type == "MorphSquare") xy(c, p);
        else if (c.type == "UpDownPreset") selectPreset(p.y < c.bounds.getCentreY() ? -1 : 1);
        else if (c.type == "PresetMenu" || c.type == "TextDisplay") presetMenu();
        else if (c.type == "StepSeqVol" || c.type == "StepSeqTie") editStep(c, p, true, right);
        else if (c.type == "StepSeqMenu") sequenceMenu();
        else if (c.type == "VuMeter") metersEnabled = !metersEnabled;
        else if (c.type == "WebLink" && cb.openManual) cb.openManual();
        else if (c.name == "Randomize" && cb.randomize) cb.randomize();
        else if (c.name == "SeqPatternCopy") copyPattern();
        else if (c.name == "SeqPatternPaste") pastePattern();
        owner.repaint();
    }
    void drag(const juce::MouseEvent& e)
    {
        if (!juce::isPositiveAndBelow(activeControl, static_cast<int>(controls.size()))) return;
        const auto& c = controls[static_cast<size_t>(activeControl)];
        const auto p = position(e);
        if (c.type == "Knob" && draggingParameter >= 0)
        {
            float next = dragStart + (downPosition.y - p.y) / (e.mods.isShiftDown() ? 1500.0f : 150.0f);
            if (circular)
            {
                const auto delta = p - c.bounds.getCentre().toFloat();
                const float angle = std::atan2(delta.x, -delta.y);
                next = (angle + juce::MathConstants<float>::pi * 0.75f)
                       / (juce::MathConstants<float>::pi * 1.5f);
            }
            set(draggingParameter, next, false);
        }
        else if (c.type == "MorphSquare") xy(c, p);
        else if (c.type == "StepSeqVol" || c.type == "StepSeqTie") editStep(c, p, false, e.mods.isPopupMenu());
        owner.repaint();
    }
    void up()
    {
        if (draggingParameter >= 0 && cb.endGesture) cb.endGesture(draggingParameter);
        activeControl = draggingParameter = lastStep = -1;
        owner.repaint();
    }
};

SkinPanel::SkinPanel(juce::File directory, Callbacks callbacks)
    : impl(std::make_unique<Impl>(*this, std::move(directory), std::move(callbacks)))
{
    setName("CamelSpace");
    setOpaque(true);
    setSize(nativeWidth, nativeHeight);
    startTimerHz(24);
}
SkinPanel::~SkinPanel() = default;
int SkinPanel::getCurrentPattern() const noexcept { return impl->pattern; }
void SkinPanel::setCurrentPattern(int p) { impl->selectPattern(p); }
bool SkinPanel::hasValidSkin() const noexcept
{
    return impl->image("Background").isValid() && !impl->controls.empty()
           && impl->fonts.find("FontSelector") != impl->fonts.end();
}
bool SkinPanel::loadSequencerPreset(const juce::File& file) { return impl->loadPattern(file); }
bool SkinPanel::saveSequencerPreset(const juce::File& file) const { return impl->savePattern(file); }
void SkinPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.addTransform(juce::AffineTransform::scale(float(getWidth()) / nativeWidth, float(getHeight()) / nativeHeight));
    impl->paint(g);
    if (!hasValidSkin())
    {
        g.setColour(juce::Colours::white);
        g.drawText("CamelSpace skin files could not be loaded", juce::Rectangle<int>(20, 200, 530, 60), juce::Justification::centred);
    }
}
void SkinPanel::mouseDown(const juce::MouseEvent& e) { impl->down(e); }
void SkinPanel::mouseDrag(const juce::MouseEvent& e) { impl->drag(e); }
void SkinPanel::mouseUp(const juce::MouseEvent&) { impl->up(); }
void SkinPanel::mouseMove(const juce::MouseEvent&) {}
void SkinPanel::mouseExit(const juce::MouseEvent&) {}
void SkinPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto i = impl->hit(impl->position(e));
    if (i < 0) return;
    const auto& control = impl->controls[static_cast<size_t>(i)];
    if (control.type == "Knob" && impl->cb.getDefaultValue)
        impl->set(control.parameter, impl->cb.getDefaultValue(control.parameter));
    else impl->readout(control.parameter);
}
void SkinPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto i = impl->hit(impl->position(e));
    if (i < 0) return;
    const auto& c = impl->controls[static_cast<size_t>(i)];
    if (c.type == "Knob") impl->set(c.parameter, impl->value(c.parameter) + wheel.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.2f));
    if (c.type == "Selector")
    {
        const auto count = impl->choices(c).size();
        const int next = juce::jlimit(0, count - 1, impl->discrete(c.parameter, count) + (wheel.deltaY > 0 ? 1 : -1));
        impl->set(c.parameter, next / 100.0f);
    }
}
void SkinPanel::timerCallback() { repaint(); }
}
