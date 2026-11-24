#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace cs
{
/** Paints original controls over the darkened classic panel with Atjazz branding.
    It is independent of the bridge/processor and preserves the input assets.
*/
class SkinPanel final : public juce::Component, private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<float(int)> getValue;
        std::function<void(int, float)> setValue;
        std::function<juce::String(int)> getText;
        std::function<float(int)> getDefaultValue;
        juce::StringArray parameterNames;
        juce::StringArray presetNames;
        std::function<int()> getPreset;
        std::function<void(int)> setPreset;
        std::function<juce::String()> getPresetName;
        std::function<juce::StringArray()> getBankProgramNames;
        std::function<void(int)> setBankProgram;
        std::function<int()> getBankProgram;
        std::function<void(int)> patternChanged;
        std::function<void()> openManual;
        std::function<juce::String()> getStatus;

        // Optional host features. Unsupported menu actions are disabled.
        std::function<void(int)> beginGesture, endGesture, learnMidi;
        std::function<void()> randomize, clearPreset;
        std::function<float(int)> getOutputLevel;
        std::function<void(const juce::File&)> loadPresetFile, savePresetFile;
        std::function<void(const juce::File&)> loadBankFile, saveBankFile;
        juce::String versionText = "v2.0.0";
    };

    explicit SkinPanel(juce::File dataDirectory, Callbacks callbacks);
    ~SkinPanel() override;
    static constexpr int nativeWidth = 570, nativeHeight = 500;

    int getCurrentPattern() const noexcept;
    void setCurrentPattern(int zeroBasedPattern);
    bool hasValidSkin() const noexcept;
    bool loadSequencerPreset(const juce::File&);
    bool saveSequencerPreset(const juce::File&) const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkinPanel)
};

using SkinPanelCallbacks = SkinPanel::Callbacks;
}
