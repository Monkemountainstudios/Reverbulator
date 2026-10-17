#pragma once

#include <JuceHeader.h>
#include "StageToneFilter.h"

// Layer 1 of the SRS-4 network. It deliberately has no self-feedback path.
// The only feedback it can receive is the explicit, externally-owned buffer
// supplied by a later layer. That makes the routing constraint structural
// rather than dependent on a macro value.
class WarmRoomLayer final
{
public:
    struct MacroState
    {
        float space = 0.0f;
        float motion = 0.0f;
        float chaos = 0.0f;
        bool body = false;
        bool halo = false;
    };

    void prepare(double sampleRate, int maximumBlockSize);
    void reset();

    // input and feedbackFromLater are not modified. The three outputs are:
    // - stage2Feed: the normal serial output
    // - stage3Bypass: a quieter tap which skips Layer 2
    // - stage4Bypass: a still quieter tap which skips Layers 2 and 3
    void process(const juce::AudioBuffer<float>& input,
                 const juce::AudioBuffer<float>& feedbackFromLater,
                 juce::AudioBuffer<float>& stage2Feed,
                 juce::AudioBuffer<float>& stage3Bypass,
                 juce::AudioBuffer<float>& stage4Bypass,
                 MacroState macros);

private:
    float readDelay(int channel, float delayInSamples) const noexcept;

    double currentSampleRate = 44100.0;
    int writePosition = 0;
    double modulationPhase = 0.0;
    juce::AudioBuffer<float> delayMemory;
    juce::AudioBuffer<float> networkInput;
    juce::AudioBuffer<float> earlyBuffer;
    juce::AudioBuffer<float> plateBuffer;
    juce::AudioBuffer<float> roomBuffer;
    juce::Reverb plateReverb;
    juce::Reverb roomReverb;
    StageToneFilter toneFilter;
    juce::SmoothedValue<float> spaceMorph;
    juce::SmoothedValue<float> motionAmount;
    juce::SmoothedValue<float> chaosAmount;
};

