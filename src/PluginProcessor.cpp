#include "PluginProcessor.h"
#include "SkinPanel.h"
#include <dlfcn.h>

namespace cs {
namespace {
std::pair<size_t,size_t> recordRange(const std::vector<uint8_t>&,int);
void resourceAnchor() {}
juce::File findResources() {
    Dl_info info{};
    if(dladdr(reinterpret_cast<void*>(&resourceAnchor),&info) && info.dli_fname)
        return juce::File(info.dli_fname).getParentDirectory().getParentDirectory().getChildFile("Resources");
    return {};
}
class Parameter final : public juce::AudioProcessorParameterWithID {
public:
    Parameter(BridgeClient& engine,int index,float initialValue)
      : AudioProcessorParameterWithID(juce::ParameterID("camelspace_"+juce::String(index),1),
            engine.parameterName(index).empty()?"Parameter "+juce::String(index+1):juce::String(engine.parameterName(index))),engine(engine),index(index),initial(initialValue) {}
    float getValue() const override {return engine.value(index);}
    void setValue(float v) override {engine.setValue(index,v);}
    float getDefaultValue() const override {return initial;}
    juce::String getLabel() const override {return engine.parameterLabel(index);}
    juce::String getText(float value,int length) const override {
        return juce::String(value,4).substring(0,length);
    }
    float getValueForText(const juce::String& text) const override {return juce::jlimit(0.0f,1.0f,text.getFloatValue());}
private:BridgeClient& engine;int index;float initial;
};
class Editor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    Editor(Processor& processor) : AudioProcessorEditor(processor),processor(processor) {
        SkinPanel::Callbacks cb;
        cb.versionText="v"+juce::String(JucePlugin_VersionString);
        cb.parameterNames=processor.names;
        for(const auto& p:processor.programs)cb.presetNames.add(p.bank.getFileNameWithoutExtension()+" / "+p.name);
        cb.getValue=[&](int i){return processor.bridge().value(i);};
        cb.getDefaultValue=[&](int i){return processor.defaults[size_t(i)];};
        cb.setValue=[&](int i,float v){processor.edit(i,v);};
        cb.getText=[&](int i){return juce::String(processor.bridge().display(i));};
        cb.getPreset=[&]{return processor.getCurrentProgram();};
        cb.getPresetName=[&]{return processor.currentPresetName();};
        cb.getBankProgramNames=[&]{return processor.currentBankPrograms();};
        cb.getBankProgram=[&]{return processor.currentBankProgram();};
        cb.setBankProgram=[&](int i){processor.selectBankProgram(i);};
        cb.setPreset=[&](int i){processor.setCurrentProgram(i);};
        cb.patternChanged=[&](int i){processor.editingPattern.store(i);};
        cb.beginGesture=[&](int i){if(juce::isPositiveAndBelow(i,processor.getParameters().size()))processor.getParameters()[i]->beginChangeGesture();};
        cb.endGesture=[&](int i){if(juce::isPositiveAndBelow(i,processor.getParameters().size()))processor.getParameters()[i]->endChangeGesture();};
        cb.getStatus=[&]{return !processor.bridge().ready()?juce::String(processor.bridge().error()):processor.midiLearning.load()>=0?"MIDI Learn: move a controller":juce::String();};
        cb.learnMidi=[&](int i){processor.midiLearning.store(i);};
        cb.getOutputLevel=[&](int ch){return processor.levels[juce::jlimit(0,1,ch)].load();};
        cb.randomize=[&]{processor.randomize();};
        cb.clearPreset=[&]{processor.clearPreset();};
        cb.openManual=[&]{processor.resources.getChildFile("CamelSpaceManual.pdf").startAsProcess();};
        cb.loadBankFile=[&](const juce::File& f){processor.loadBankFile(f);};
        cb.saveBankFile=[&](const juce::File& f){processor.saveBankFile(f);};
        cb.loadPresetFile=[&](const juce::File& f){processor.loadPresetFile(f);};
        cb.savePresetFile=[&](const juce::File& f){processor.savePresetFile(f);};
        panel=std::make_unique<SkinPanel>(processor.dataDirectory,std::move(cb));
        panel->setCurrentPattern(processor.editingPattern.load());
        addAndMakeVisible(*panel);
        setSize(570,500);setResizable(false,false);startTimerHz(10);
    }
    ~Editor() override {processor.editingPattern.store(panel->getCurrentPattern());}
    void resized() override {if(panel)panel->setBounds(getLocalBounds());}
private:
    void timerCallback() override {processor.bridge().updateDisplays();if(panel->getCurrentPattern()!=processor.editingPattern.load())panel->setCurrentPattern(processor.editingPattern.load());panel->repaint();}
    Processor& processor;std::unique_ptr<SkinPanel> panel;
};
}
Processor::Processor(juce::File resourceDirectory) : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
    resources=resourceDirectory.isDirectory()?resourceDirectory:findResources();dataDirectory=resources.getChildFile("CamelSpaceData");
    client=std::make_unique<BridgeClient>(resources.getChildFile("camelspace-helper").getFullPathName().toStdString(),
       resources.getChildFile("Original/CamelSpace.vst").getFullPathName().toStdString(),dataDirectory.getFullPathName().toStdString());
    auto catalog=juce::JSON::parse(resources.getChildFile("programs.json"));
    if(auto* entries=catalog.getArray())for(const auto& item:*entries) {
        const auto bank=item["bank"].toString();
        if(bank==juce::File::createLegalFileName(bank) && item["slot"].isInt())
            programs.push_back({item["name"].toString(),dataDirectory.getChildFile(bank),int(item["slot"])});
    }
    if(programs.empty())programs.push_back({"Default",dataDirectory.getChildFile("-Default.fxb"),0});
    client->loadBank(dataDirectory.getChildFile("-Default.fxb").getFullPathName().toStdString(),0);
    auto initial=client->state();
    client->clearPreset();
    for(int i=0;i<kParameters;++i)defaults[size_t(i)]=client->value(i);
    client->restore(initial);
    for(int i=0;i<kParameters;++i) {midiMappings[size_t(i)].store(-1);names.add(client->ready()?juce::String(client->parameterName(i)):"Parameter "+juce::String(i+1));addParameter(new Parameter(*client,i,defaults[size_t(i)]));}
    for(size_t i=0;i<programs.size();++i)if(programs[i].bank.getFileName()=="Rhythmic1.fxb" && programs[i].slot==0)selected=int(i);
    setLatencySamples(client->latency());client->updateDisplays();
    refreshPresetNames();
}
Processor::~Processor()=default;
bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
}
void Processor::prepareToPlay(double sampleRate,int block) {rate=sampleRate;client->prepare(rate,juce::jlimit(1,kMaxFrames,block));}
void Processor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    if(audio.getNumChannels()<2)return;
    for(const auto item:midi) {
        const auto message=item.getMessage();
        if(message.isController()) {
            const int cc=(message.getChannel()-1)*128+message.getControllerNumber();
            const int learning=midiLearning.exchange(-1);
            if(juce::isPositiveAndBelow(learning,kParameters))midiMappings[size_t(learning)].store(cc);
            for(int i=0;i<kParameters;++i)if(midiMappings[size_t(i)].load()==cc)edit(i,message.getControllerValue()/127.0f);
        }
    }
    Transport transport;
    if(auto* playhead=getPlayHead())if(auto p=playhead->getPosition()) {
        if(auto bpm=p->getBpm())transport.bpm=*bpm;
        if(auto ppq=p->getPpqPosition())transport.ppqPosition=*ppq;
        if(auto sample=p->getTimeInSamples())transport.samplePosition=double(*sample);
        transport.playing=p->getIsPlaying();transport.cycling=p->getIsLooping();
        if(auto loop=p->getLoopPoints()){transport.cycleStart=loop->ppqStart;transport.cycleEnd=loop->ppqEnd;}
        if(auto sig=p->getTimeSignature()){transport.numerator=sig->numerator;transport.denominator=sig->denominator;}
    }
    for(int start=0;start<audio.getNumSamples();start+=kMaxFrames) {
        int count=juce::jmin(kMaxFrames,audio.getNumSamples()-start);
        const float* input[]={audio.getReadPointer(0,start),audio.getReadPointer(1,start)};
        float* output[]={audio.getWritePointer(0,start),audio.getWritePointer(1,start)};
        Midi events[kMaxMidi];int n=0;
        for(const auto metadata:midi)if(metadata.samplePosition>=start && metadata.samplePosition<start+count && metadata.numBytes<=3 && n<kMaxMidi) {
            events[n].offset=metadata.samplePosition-start;events[n].size=uint8_t(metadata.numBytes);
            std::memcpy(events[n].bytes,metadata.data,metadata.numBytes);++n;
        }
        // If the helper exits, keep the dry input already present in the buffer.
        if(!client->process(input,output,count,transport,events,n))break;
        transport.samplePosition+=count;transport.ppqPosition+=double(count)/rate*transport.bpm/60.0;
    }
    for(int ch=0;ch<2;++ch)levels[ch].store(audio.getMagnitude(ch,0,audio.getNumSamples()));
    midi.clear();
}
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
void Processor::edit(int index,float value) {if(juce::isPositiveAndBelow(index,getParameters().size()))getParameters()[index]->setValueNotifyingHost(value);}
void Processor::setCurrentProgram(int index) {
    if(!juce::isPositiveAndBelow(index,int(programs.size())))return;
    auto& p=programs[size_t(index)];
    if(client->loadBank(p.bank.getFullPathName().toStdString(),p.slot)) {selected=index;originalSlot=p.slot;client->updateDisplays();refreshPresetNames();updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true).withParameterInfoChanged(true));}
}
const juce::String Processor::getProgramName(int i) {return juce::isPositiveAndBelow(i,int(programs.size()))?programs[size_t(i)].bank.getFileNameWithoutExtension()+" / "+programs[size_t(i)].name:juce::String();}
void Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto state=client->state();if(state.empty())return;
    juce::MemoryOutputStream out(dest,false);out.writeInt(int(0x43535302));out.writeInt(selected);out.writeInt(originalSlot);out.writeInt(int(state.size()));out.write(state.data(),state.size());
    out.writeInt(editingPattern.load());for(auto& cc:midiMappings)out.writeInt(cc.load());
}
void Processor::setStateInformation(const void* data,int size) {
    if(!data || size<16 || size>int(kMaxState+16+4+4*kParameters))return;
    juce::MemoryInputStream input(data,size,false);
    const auto version=uint32_t(input.readInt());
    if(version!=kProtocol && version!=0x43535302)return;
    int program=input.readInt(),slot=input.readInt(),count=input.readInt();
    const int extra=version==0x43535302 ? 4+4*kParameters : 0;
    if(count<1 || count!=size-16-extra || slot<0 || slot>=64 || !juce::isPositiveAndBelow(program,int(programs.size())))return;
    std::vector<uint8_t> chunk(size_t(count),uint8_t{});input.read(chunk.data(),count);
    int pattern=0;std::array<int,kParameters> mappings;mappings.fill(-1);
    if(extra){pattern=input.readInt();if(pattern<0||pattern>7)return;for(auto& cc:mappings){cc=input.readInt();if(cc<-1||cc>=2048)return;}}
    if(client->restore(chunk,slot)){selected=program;originalSlot=slot;editingPattern=pattern;for(int i=0;i<kParameters;++i)midiMappings[size_t(i)].store(mappings[size_t(i)]);client->updateDisplays();refreshPresetNames();updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));}
}
void Processor::loadBankFile(const juce::File& file) {if(client->loadBank(file.getFullPathName().toStdString(),0)){originalSlot=0;client->updateDisplays();refreshPresetNames();updateHostDisplay();}}
void Processor::saveBankFile(const juce::File& file) {
    auto state=client->state();if(state.empty())return;
    juce::MemoryOutputStream out;out.write("CcnK",4);out.writeIntBigEndian(int(160+state.size()));out.write("FBCh",4);out.writeIntBigEndian(1);out.write("CaS1",4);out.writeIntBigEndian(1501);out.writeIntBigEndian(64);
    const char reserved[128]{};out.write(reserved,128);out.writeIntBigEndian(int(state.size()));out.write(state.data(),state.size());
    auto target=file.hasFileExtension("fxb")?file:file.withFileExtension("fxb");target.replaceWithData(out.getData(),out.getDataSize());
}
void Processor::randomize() {
    if(client->randomize()){for(int i=0;i<kParameters;++i)getParameters()[i]->sendValueChangedMessageToListeners(client->value(i));client->updateDisplays();updateHostDisplay();}
}
void Processor::clearPreset(){if(client->clearPreset()){client->updateDisplays();refreshPresetNames();for(int i=0;i<kParameters;++i)getParameters()[i]->sendValueChangedMessageToListeners(client->value(i));updateHostDisplay();}}
namespace {
std::pair<size_t,size_t> recordRange(const std::vector<uint8_t>& chunk,int slot) {
    if(!validChunk(chunk) || slot<0 || slot>=64)return {};
    size_t p=0;
    for(int i=0;i<=slot;++i){const size_t start=p;p+=4;while(chunk[p])++p;p+=1+4+kParameters*4;if(i==slot)return {start,p};}
    return {};
}
}
void Processor::loadPresetFile(const juce::File& file) {
    if(file.getSize()<60 || file.getSize()>int64_t(kMaxState))return;
    juce::MemoryBlock fileBytes;if(!file.loadFileAsData(fileBytes))return;
    auto* bytes=static_cast<const uint8_t*>(fileBytes.getData());
    if(std::memcmp(bytes,"CcnK",4) || std::memcmp(bytes+8,"FPCh",4) || std::memcmp(bytes+16,"CaS1",4))return;
    std::vector<uint8_t> record(bytes+60,bytes+fileBytes.getSize());if(!validRecords(record,1))return;
    auto bank=client->state();auto [start,end]=recordRange(bank,originalSlot);if(end==0)return;
    bank.erase(bank.begin()+ptrdiff_t(start),bank.begin()+ptrdiff_t(end));bank.insert(bank.begin()+ptrdiff_t(start),record.begin(),record.end());
    if(client->restore(bank,originalSlot)){client->updateDisplays();refreshPresetNames();updateHostDisplay();}
}
void Processor::savePresetFile(const juce::File& file) {
    auto bank=client->state();auto [start,end]=recordRange(bank,originalSlot);if(end==0)return;
    juce::MemoryOutputStream out;out.write("CcnK",4);out.writeIntBigEndian(int(60+end-start-8));out.write("FPCh",4);out.writeIntBigEndian(1);out.write("CaS1",4);out.writeIntBigEndian(1501);out.writeIntBigEndian(kParameters);
    char name[28]{};std::strncpy(name,reinterpret_cast<const char*>(bank.data()+start+4),27);out.write(name,28);out.writeIntBigEndian(int(end-start));out.write(bank.data()+start,end-start);
    auto target=file.hasFileExtension("fxp")?file:file.withFileExtension("fxp");target.replaceWithData(out.getData(),out.getDataSize());
}
void Processor::refreshPresetNames() {
    auto chunk=client->state();if(!validChunk(chunk))return;
    juce::StringArray names;size_t p=0;
    for(int i=0;i<64;++i){p+=4;const size_t start=p;while(chunk[p])++p;names.add(juce::String::fromUTF8(reinterpret_cast<const char*>(chunk.data()+start),int(p-start)));p+=1+4+kParameters*4;}
    std::lock_guard lock(nameMutex);bankNames=std::move(names);currentName=bankNames[originalSlot.load()];
}
juce::String Processor::currentPresetName() const {std::lock_guard lock(nameMutex);return currentName;}
juce::StringArray Processor::currentBankPrograms() const {std::lock_guard lock(nameMutex);return bankNames;}
void Processor::selectBankProgram(int slot) {
    if(slot<0||slot>=64)return;auto chunk=client->state();
    if(client->restore(chunk,slot)){originalSlot=slot;client->updateDisplays();refreshPresetNames();updateHostDisplay();}
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new cs::Processor();}
