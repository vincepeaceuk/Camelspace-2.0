#include <JuceHeader.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>
static int failures=0;
static void check(bool pass,const juce::String& label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;failures+=!pass;}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if(argc!=3)return 2;
    juce::File output(argv[2]);output.createDirectory();
    juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,juce::File(argv[1]).getFullPathName());
    check(types.size()==1,"native ARM VST3 scans");if(types.isEmpty())return 1;
    auto create=[&]{juce::String error;auto p=format.createInstanceFromDescription(*types[0],48000,256,error);if(!p)std::cerr<<error<<std::endl;return p;};
    auto plugin=create();check(plugin!=nullptr,"shipping bundle instantiates");if(!plugin)return 1;
    check(types[0]->version=="2.0.0","host reports release version 2.0.0");
    check(types[0]->manufacturerName=="Atjazz Record Company","host reports Atjazz Record Company branding");
    check(plugin->acceptsMidi(),"MIDI reaches sequencer trigger");
    int originalParameters=0;
    std::ifstream metadata(output.getChildFile("parameter-metadata.txt").getFullPathName().toStdString());
    std::string line;
    while(std::getline(metadata,line)) {
        std::istringstream row(line);std::string tag,name;int index;
        if(row>>tag>>index>>name && tag=="PARAM" && index>=0 && index<plugin->getParameters().size()
            && plugin->getParameters()[index]->getName(128)==juce::String(name))++originalParameters;
    }
    std::cout<<"Hosted parameter count="<<plugin->getParameters().size()<<"\n";
    check(originalParameters==344,"all 344 original automation names and indices retained");
    check(plugin->getNumPrograms()==476,"all 476 named factory entries outside duplicate default bank");
    plugin->setCurrentProgram(2);plugin->getParameters()[10]->setValueNotifyingHost(0.38f);
    juce::MemoryBlock saved;plugin->getStateInformation(saved);
    check(saved.getSize()>80000,"project state includes original bank data");
    plugin->setCurrentProgram(10);plugin->setStateInformation(saved.getData(),int(saved.getSize()));
    check(plugin->getCurrentProgram()==2,"project recall restores selected factory entry");
    check(std::abs(plugin->getParameters()[10]->getValue()-0.38f)<1e-6f,"project recall retains edited parameter");
    {auto other=create();other->setStateInformation(saved.getData(),int(saved.getSize()));check(std::abs(other->getParameters()[10]->getValue()-0.38f)<1e-6f,"state reopens in independent instance");}
    bool finite=true;double energy=0;float peak=0;
    plugin->prepareToPlay(48000,256);
    juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
    for(int program=0;program<plugin->getNumPrograms();++program) {
        plugin->setCurrentProgram(program);
        for(int block=0;block<8;++block) {
            for(int i=0;i<256;++i)audio.setSample(0,i,0.1f*std::sin(6.283185307179586*220*(block*256+i)/48000));
            audio.copyFrom(1,0,audio,0,0,256);plugin->processBlock(audio,midi);
            for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i){float f=audio.getSample(ch,i);finite&=std::isfinite(f);energy+=double(f)*f;peak=std::max(peak,std::abs(f));}
        }
    }
    check(finite && peak<100 && energy>1e-5,"every factory entry processes finite audio through VST3");
    plugin->setCurrentProgram(0);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorAndMakeActive());
        check(editor && editor->getWidth()==570 && editor->getHeight()==500,"original 570x500 panel dimensions");
        if(editor) {
            editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
            editor->setVisible(true);
            check(editor->getPeer()!=nullptr,"hosted native editor attaches to a macOS window");
            editor->removeFromDesktop();
        }
    }
    juce::MemoryBlock before;plugin->getStateInformation(before);
    const uint8_t bad[]={1,2,3,4,5};plugin->setStateInformation(bad,sizeof(bad));
    juce::MemoryBlock after;plugin->getStateInformation(after);check(before==after,"malformed project state ignored");
    plugin->releaseResources();
    std::cout<<"Audio energy="<<energy<<" peak="<<peak<<"\n";
    return failures?1:0;
}
