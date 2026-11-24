#include "SkinPanel.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

// This exercises the actual native skin controls with metadata observed from
// the original engine. It makes no claim that the old editor can render today.
int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "Usage: SkinPanelTests CamelSpaceData parameter-metadata.txt output-directory\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const auto resolve = [](const char* argument)
    { return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argument)); };
    const auto dataDirectory = resolve(argv[1]);
    const auto metadataFile = resolve(argv[2]);
    const auto outputDirectory = resolve(argv[3]);
    if (!dataDirectory.isDirectory() || !metadataFile.existsAsFile()
        || !outputDirectory.createDirectory())
    {
        std::cerr << "Required input files or writable output directory unavailable\n";
        return 2;
    }

    cs::SkinPanel::Callbacks callbacks;
    std::vector<float> values(344);
    juce::StringArray names;
    for (int i = 0; i < 344; ++i) names.add(juce::String{});
    for (auto line : juce::StringArray::fromLines(metadataFile.loadFileAsString()))
    {
        std::istringstream input(line.toStdString());
        std::string kind, name;
        int index = -1;
        float value = 0;
        if (!(input >> kind >> index >> name >> value) || kind != "PARAM") continue;
        if (index < 0 || index >= 344 || !std::isfinite(value))
        { std::cerr << "Invalid original-engine metadata record\n"; return 2; }
        names.set(index, juce::String(name));
        values[static_cast<size_t>(index)] = value;
    }
    for (const auto& name : names)
        if (name.isEmpty()) { std::cerr << "Metadata must contain all344 parameter names\n"; return 2; }

    int failures = 0, edits = 0, beginCount = 0, endCount = 0, persistedPattern = -1;
    const auto check = [&](bool ok, const char* message)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
        if (!ok) ++failures;
    };
    callbacks.parameterNames = names;
    callbacks.getValue = [&](int i) { return values.at(static_cast<size_t>(i)); };
    callbacks.setValue = [&](int i, float value) { values.at(static_cast<size_t>(i)) = value; ++edits; };
    callbacks.getText = [&](int i) { return juce::String(values.at(static_cast<size_t>(i)) * 100, 1); };
    callbacks.getDefaultValue = [](int i) { return i == 39 ? 0.77f : 0.5f; };
    callbacks.beginGesture = [&](int) { ++beginCount; };
    callbacks.endGesture = [&](int) { ++endCount; };
    callbacks.presetNames.add("Rhythmic1 / 4Barfilter");
    callbacks.getPreset = [] { return 0; };
    callbacks.patternChanged = [&](int pattern) { persistedPattern = pattern; };
    juce::String status;
    callbacks.getStatus = [&] { return status; };
    const auto setFixture = [&](const char* name, float value)
    {
        const auto i = names.indexOf(name);
        if (i < 0) { std::cerr << "Missing expected original parameter " << name << '\n'; return false; }
        values.at(static_cast<size_t>(i)) = value;
        return true;
    };
    // Isolate gesture assertions from whichever original program supplied the
    // metadata. Names/indices still come from the original engine dump.
    if (!setFixture("PanOn", 0) || !setFixture("PanDepth", 0.5f)
        || !setFixture("PanShape", 0) || !setFixture("SeqVolMode", 0)
        || !setFixture("Seq2Step1Vol", 1) || !setFixture("Seq2Tie1Vol", 0)
        || !setFixture("SeqPat2FiltOn", 0) || !setFixture("MorphXParam", 0.14f)
        || !setFixture("MorphYParam", 0.02f)) return 2;
    cs::SkinPanel panel(dataDirectory, std::move(callbacks));
    const auto event = [&](float x, float y)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, y },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            &panel, &panel, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, false);
    };
    const auto click = [&](float x, float y)
    { const auto e = event(x, y); panel.mouseDown(e); panel.mouseUp(e); };
    const auto get = [&](const char* name) { return values.at(static_cast<size_t>(names.indexOf(name))); };
    check(names.size() == 344, "all original parameter names loaded");
    check(panel.hasValidSkin(), "original570x500 skin fonts and layout load");
    click(36, 94);
    check(get("PanOn") == 1, "AutoPan original on button changes engine parameter");
    { const auto down = event(80, 157); panel.mouseDown(down);
      const auto drag = event(80, 127); panel.mouseDrag(drag); panel.mouseUp(drag); }
    check(get("PanDepth") > 0.6f, "original depth knob drag updates parameter");
    click(36, 162);
    check(std::abs(get("PanSource") - 0.01f) < 1.0e-6f, "SEQ source uses original index/100 normalization");
    click(161, 116);
    check(std::abs(get("PanShape") - 0.01f) < 1.0e-6f, "shape arrow advances original enum");
    click(205, 331);
    check(panel.getCurrentPattern() == 1 && persistedPattern == 1,
          "pattern2 button selects pattern2 editor and notifies persistent state");
    click(184, 440);
    check(get("Seq2Step1Vol") == 0, "pattern2 step edits corresponding original parameter");
    click(184, 481);
    check(get("Seq2Tie1Vol") == 1, "pattern2 tie edits corresponding original parameter");
    click(252, 144);
    check(std::abs(get("MasterMix") - 0.5f) < 0.02f && std::abs(get("MmFilterRes") - 0.5f) < 0.02f,
          "XY targets original ModulationTarget master mix and resonance");
    click(251, 355);
    check(get("SeqPat2FiltOn") == 1 && std::abs(get("CutoffModSource") - 0.01f) < 1.0e-6f,
          "pattern filter route enables original destination and source");
    { const auto e = event(528, 406); panel.mouseDoubleClick(e); }
    check(std::abs(get("MasterVolume") - 0.77f) < 1.0e-6f,
          "doubleclick uses original default callback rather than midpoint");
    check(beginCount == endCount && edits > 0, "host gestures remain paired");
    panel.setCurrentPattern(99);
    check(panel.getCurrentPattern() == 7 && persistedPattern == 7,
          "pattern range clamps and external selection notifies persistent state");
    panel.setCurrentPattern(0);

    int patternCount = 0;
    for (auto file : dataDirectory.getChildFile("Presets/StepSequencerPresets")
                        .findChildFiles(juce::File::findFiles, false, "*.csp"))
        if (panel.loadSequencerPreset(file)) ++patternCount;
    check(patternCount == 25, "all25 original CSP patterns load into native editor parameters");
    const auto saved = outputDirectory.getChildFile("sequence-roundtrip.csp");
    check(panel.saveSequencerPreset(saved), "sequence preset saves");
    auto before = values;
    click(184, 440);
    check(panel.loadSequencerPreset(saved), "sequence preset reloads");
    bool exact = true;
    for (int i = 52; i < 84; ++i)
        if (values[static_cast<size_t>(i)] != before[static_cast<size_t>(i)]) exact = false;
    check(exact, "sequence32parameter values survive save/reload exactly");
    const auto malformed = outputDirectory.getChildFile("invalid.csp");
    malformed.replaceWithText("bad");
    before = values;
    check(!panel.loadSequencerPreset(malformed) && before == values,
          "malformed sequencer preset leaves every parameter unchanged");

    setFixture("PanDepth",.5f);
    const auto picture = panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f);
    const auto originalKnob=juce::ImageFileFormat::loadFrom(dataDirectory.getChildFile("Skins/default/Knob.png"));
    int knobPixels=0,knobMatches=0;
    const int frameHeight=originalKnob.getWidth(),frame=27;
    for(int y=8;y<frameHeight-8;++y)for(int x=8;x<frameHeight-8;++x){
        const auto pixel=originalKnob.getPixelAt(x,frame*frameHeight+y);
        if(pixel.getAlpha()==255){++knobPixels;knobMatches+=picture.getPixelAt(80-frameHeight/2+x,157-frameHeight/2+y)==pixel;}}
    check(knobPixels>100 && knobMatches==knobPixels,"original knob artwork is preserved pixel for pixel");
    const auto originalButton=juce::ImageFileFormat::loadFrom(dataDirectory.getChildFile("Skins/default/OnButton.png"));
    const int bw=originalButton.getWidth(),bh=originalButton.getHeight()/2;
    const int buttonFrame=get("PanOn")>=.5f?1:0;
    int buttonPixels=0,buttonMatches=0;
    for(int y=3;y<bh-3;++y)for(int x=3;x<bw-3;++x){const auto pixel=originalButton.getPixelAt(x,buttonFrame*bh+y);
        if(pixel.getAlpha()==255){++buttonPixels;buttonMatches+=picture.getPixelAt(36-bw/2+x,94-bh/2+y)==pixel;}}
    check(buttonPixels>30 && buttonMatches==buttonPixels,"original button artwork is preserved pixel for pixel");
    const auto randomize=juce::ImageFileFormat::loadFrom(dataDirectory.getChildFile("Skins/default/ButtonRandom.png"));
    int randomizeInk=0,whiteInk=0;
    const int rw=randomize.getWidth(),rh=randomize.getHeight()/2;
    for(int y=0;y<rh;++y)for(int x=0;x<rw;++x){const auto c=randomize.getPixelAt(x,y);
        if(c.getAlpha()>=225 && c.getRed()>juce::jmax(c.getGreen(),c.getBlue())+50){++randomizeInk;
            const auto shown=picture.getPixelAt(285-rw/2+x,34-rh/2+y);
            whiteInk+=shown.getRed()>225 && shown.getGreen()>225 && shown.getBlue()>225;}}
    std::cout<<"Randomize text pixels "<<whiteInk<<"/"<<randomizeInk<<" sprite "<<rw<<"x"<<rh<<"\n";
    check(randomizeInk>30 && whiteInk==randomizeInk,"Randomize lettering is white in its original glyph positions");
    check(picture.getWidth() == 570 && picture.getHeight() == 500, "render retains original570x500 dimensions");
    juce::Image expected(juce::Image::ARGB, 570, 500, true);
    { juce::Graphics graphics(expected); graphics.fillAll(juce::Colours::black);
      graphics.drawImageAt(juce::ImageFileFormat::loadFrom(dataDirectory.getChildFile("Skins/default/Background.png")), 0, 0); }
    bool darkPanel = true;
    // Sample panel fields, keeping the original bright chrome controls and
    // beveled frame out of the dark-surface assertion.
    for(auto point:{juce::Point<int>{106,130},{58,335},{155,440},{548,207}})
        darkPanel &= picture.getPixelAt(point.x,point.y).getPerceivedBrightness() < 0.3f;
    check(darkPanel, "classic panel surface is dark while chrome controls remain original");
    int oldBrandPixels = 0;
    for (int y = 9; y < 49; ++y)
        for (int x = 418; x < 532; ++x)
            oldBrandPixels += expected.getPixelAt(x,y) == picture.getPixelAt(x,y);
    check(oldBrandPixels < 200, "old Camel Audio logo and white backing are replaced");
    int logoPixels=0;
    for(int y=11;y<54;++y)for(int x=403;x<556;++x)
        logoPixels+=picture.getPixelAt(x,y).getPerceivedBrightness()>.6f;
    check(logoPixels>180,"transparent Atjazz symbol and wordmark render visibly on the dark header");
    status = "MIDI learn waiting";
    const auto waiting = panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f);
    int changedInside = 0, changedOutside = 0;
    const juce::Rectangle<int> readout(201, 70, 158, 16);
    for (int y = 0; y < 500; ++y)
        for (int x = 0; x < 570; ++x)
            if (waiting.getPixelAt(x, y) != picture.getPixelAt(x, y))
            { if (readout.contains(x, y)) ++changedInside; else ++changedOutside; }
    check(changedInside > 0 && changedOutside == 0,
          "helper or MIDIlearn status uses existing readout only");
    status.clear();
    const auto screenshot = outputDirectory.getChildFile("native-skin.png");
    auto stream = screenshot.createOutputStream();
    if(stream){stream->setPosition(0);stream->truncate();}
    const bool wrote = stream != nullptr && juce::PNGImageFormat().writeImageToStream(picture, *stream);
    stream.reset();
    check(wrote && screenshot.getSize() > 10000, "native bitmap editor screenshot written");
    std::cout << "RESULT " << failures << " failures, " << edits << " parameter edits\n";
    return failures == 0 ? 0 : 1;
}
