#include "PluginProcessor.h"
#include <iostream>
#include <cmath>
static int failures=0;
static void check(bool pass,const char* message){std::cout<<(pass?"PASS ":"FAIL ")<<message<<std::endl;failures+=!pass;}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3)return 2;
    cs::Processor p{juce::File(argv[1])};
    juce::File out(argv[2]);out.createDirectory();
    check(p.bridge().ready(),"processor uses bundled original helper");
    p.prepareToPlay(48000,256);
    p.getParameters()[10]->setValueNotifyingHost(0.371f);p.bridge().sync();
    p.savePresetFile(out.getChildFile("edited.fxp"));
    p.getParameters()[10]->setValueNotifyingHost(0.8f);p.bridge().sync();
    p.loadPresetFile(out.getChildFile("edited.fxp"));
    check(std::abs(p.bridge().value(10)-0.371f)<1e-6f,"FXP save/load restores edited original value");
    p.saveBankFile(out.getChildFile("edited.fxb"));p.clearPreset();
    check(p.currentPresetName()=="blank","Clear Preset displays original blank program name");
    p.loadBankFile(out.getChildFile("edited.fxb"));
    check(std::abs(p.bridge().value(10)-0.371f)<1e-6f,"FXB save/load restores edited original value");
    check(p.currentBankPrograms().size()==64,"all imported bank slots are browsable");
    p.selectBankProgram(2);check(p.currentBankProgram()==2 && p.currentPresetName()==p.currentBankPrograms()[2],"bank selection displays actual selected record");
    p.selectBankProgram(0);
    const auto originalFXP=juce::File::getCurrentWorkingDirectory().getChildFile("../local/analysis/original-saved-preset.fxp");
    if(originalFXP.existsAsFile()) {p.loadPresetFile(originalFXP);check(p.bridge().ready(),"legacy nonstandard original FXP imports");}
    p.midiLearning.store(10);juce::AudioBuffer<float> audio(2,256);audio.clear();juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(3,74,88),0);p.processBlock(audio,midi);
    check(p.midiMappings[10].load()==2*128+74,"MIDI learn retains channel and controller");
    check(std::abs(p.bridge().value(10)-88.0f/127)<1e-6f,"learned controller reaches original cutoff");
    p.editingPattern.store(5);juce::MemoryBlock state;p.getStateInformation(state);
    cs::Processor reopened{juce::File(argv[1])};reopened.setStateInformation(state.getData(),int(state.getSize()));
    check(reopened.midiMappings[10].load()==2*128+74 && reopened.editingPattern.load()==5,"MIDI learn and selected pattern survive project recall");
    check(std::abs(reopened.bridge().value(10)-88.0f/127)<1e-6f,"original bank survives extended project state");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto* panel=editor->getChildComponent(0);
        check(panel!=nullptr,"shipping processor mounts original-skin editor");
        if(panel) {
            const auto old=p.bridge().value(1);
            const auto event=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{36,94},
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,panel,panel,
                juce::Time::getCurrentTime(),{36,94},juce::Time::getCurrentTime(),1,false);
            panel->mouseDown(event);panel->mouseUp(event);p.bridge().sync();
            check(p.bridge().value(1)!=old,"actual original-artwork switch controls original engine");
            p.setCurrentProgram(0);
            auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());
            auto stream=out.getChildFile("Camel Space 2.png").createOutputStream();
            stream->setPosition(0);stream->truncate();
            juce::PNGImageFormat png;png.writeImageToStream(snapshot,*stream);stream.reset();
            check(snapshot.getPixelAt(10,10)!=juce::Colours::black,"shipping native panel draws original artwork");
        }
    }
    // Save a clean first-open view after the gesture checks, so the release
    // preview shows the actual factory preset rather than a transient readout.
    for(int i=0;i<p.getNumPrograms();++i)if(p.getProgramName(i)=="Rhythmic1 / 4Barfilter"){p.setCurrentProgram(i);break;}
    p.editingPattern.store(0);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());
        auto stream=out.getChildFile("Camel Space 2.png").createOutputStream();
        stream->setPosition(0);stream->truncate();
        juce::PNGImageFormat png;png.writeImageToStream(snapshot,*stream);stream.reset();
    }
    return failures?1:0;
}
