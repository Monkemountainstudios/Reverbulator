#pragma once

#include <JuceHeader.h>
#include "ChorusedSpaceLayer.h"
#include "DigitalSpectralLayer.h"
#include "GravityBloomLayer.h"
#include "OutputSafetyLimiter.h"
#include "SerialRouting.h"
#include "StageToneFilter.h"
#include "TotalSpectralReturn.h"
#include "WarmRoomLayer.h"

class ReverbulatorAudioProcessor final
    : public juce::AudioProcessor,
      private juce::AudioProcessorValueTreeState::Listener
{
public:
    ReverbulatorAudioProcessor();
    ~ReverbulatorAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 95.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    float getMassLevel() const noexcept { return massLevel.load(std::memory_order_relaxed); }
    bool isCurrentProgramModified() const noexcept
    {
        return programModified.load(std::memory_order_relaxed);
    }
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String&, float) override;

    std::atomic<float> massLevel { 0.0f };
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> programModified { false };
    std::atomic<bool> applyingProgram { false };
    float massEnvelope = 0.0f;
    double currentSampleRate = 44100.0;
    WarmRoomLayer warmRoomLayer;
    ChorusedSpaceLayer chorusedSpaceLayer;
    DigitalSpectralLayer digitalSpectralLayer;
    ChorusedSpaceLayer vectorChorusedSpaceLayer;
    DigitalSpectralLayer vectorDigitalSpectralLayer;
    GravityBloomLayer gravityBloomLayer;
    TotalSpectralReturn totalSpectralReturn;
    StageToneFilter finalHaloFilter;
    OutputSafetyLimiter outputSafetyLimiter;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> layer1Input;
    juce::AudioBuffer<float> laterFeedbackBus;
    juce::AudioBuffer<float> nextFeedbackBus;
    juce::AudioBuffer<float> vectorNextFeedbackBus;
    juce::AudioBuffer<float> blendedNextFeedbackBus;
    juce::AudioBuffer<float> totalFeedbackBus;
    juce::AudioBuffer<float> nextTotalFeedbackBus;
    juce::AudioBuffer<float> combinedFeedbackBus;
    juce::AudioBuffer<float> layer1Output;
    juce::AudioBuffer<float> layer1ToLayer3;
    juce::AudioBuffer<float> layer1ToLayer4;
    juce::AudioBuffer<float> layer2Output;
    juce::AudioBuffer<float> layer2Input;
    juce::AudioBuffer<float> layer3Output;
    juce::AudioBuffer<float> vectorLayer2Input;
    juce::AudioBuffer<float> vectorLayer2Output;
    juce::AudioBuffer<float> vectorLayer3Output;
    juce::AudioBuffer<float> vectorBypassBuffer;
    juce::AudioBuffer<float> layer4MainInput;
    juce::AudioBuffer<float> layer4Layer2Tap;
    juce::AudioBuffer<float> layer4Output;
    juce::SmoothedValue<float> mixAmount;
    juce::SmoothedValue<float> orderAmount;
    juce::SmoothedValue<float> networkActivationAmount;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbulatorAudioProcessor)
};

