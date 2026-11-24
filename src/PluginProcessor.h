#pragma once
#include <JuceHeader.h>
#include "BridgeClient.h"

namespace cs {
struct Program {juce::String name;juce::File bank;int slot=0;};
class Processor final : public juce::AudioProcessor {
public:
    explicit Processor(juce::File resourceDirectory={});
    ~Processor() override;
    const juce::String getName() const override {return "Camel Space 2";}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 30.0;}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override {return int(programs.size());}
    int getCurrentProgram() override {return selected;}
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    BridgeClient& bridge() {return *client;}
    juce::File resources,dataDirectory;
    std::vector<Program> programs;
    juce::StringArray names;
    std::atomic<float> levels[2]{{0},{0}};
    void edit(int,float);
    void loadBankFile(const juce::File&);
    void saveBankFile(const juce::File&);
    void loadPresetFile(const juce::File&);
    void savePresetFile(const juce::File&);
    void randomize();
    void clearPreset();
    juce::String currentPresetName() const;
    juce::StringArray currentBankPrograms() const;
    int currentBankProgram() const {return originalSlot.load();}
    void selectBankProgram(int);
    std::array<float,kParameters> defaults{};
    std::array<std::atomic<int>,kParameters> midiMappings;
    std::atomic<int> midiLearning{-1};
    std::atomic<int> editingPattern{0};
private:
    std::unique_ptr<BridgeClient> client;
    std::atomic<int> selected{0},originalSlot{0};
    void refreshPresetNames();
    mutable std::mutex nameMutex;
    juce::String currentName;
    juce::StringArray bankNames;
    double rate=44100;
};
}
