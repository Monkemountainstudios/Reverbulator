#pragma once

#include <JuceHeader.h>
#include "StageToneFilter.h"

// Layer 4: two asymmetric, envelope-ducked blooms feeding complementary dark
// and bright tanks, with a small high-SPACE preservation path from Layer 2.
class GravityBloomLayer final
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
    void process(const juce::AudioBuffer<float>& layer3Input,
                 const juce::AudioBuffer<float>& dryInput,
                 const juce::AudioBuffer<float>& layer1Tap,
                 const juce::AudioBuffer<float>& layer2Tap,
                 juce::AudioBuffer<float>& output,
                 MacroState macros);

    static float activationForSpace(float space) noexcept;
    static float bloomDelayMs(int bloom, float space) noexcept;
    static float dryInjectionGain(float space) noexcept;
    static float layer3PreservationGain(float space) noexcept;
    static float chorusPreservationGain(float space) noexcept;
    static float chorusSidePreservationGain(float space) noexcept;
    static float shimmerAmountForSpace(float space, bool haloEnabled) noexcept;

private:
    float readDelay(int channel, float delaySamples) const noexcept;
    float readShimmerDelay(int channel, float delaySamples) const noexcept;

    double currentSampleRate = 44100.0;
    int writePosition = 0;
    double bloomPhaseA = 0.0;
    double bloomPhaseB = 2.1;
    double spatialPhase = 0.0;
    double spatialDriftPhase = 0.0;
    float dryEnvelope = 0.0f;
    int shimmerWritePosition = 0;
    int shimmerWindowSamples = 1;
    std::array<double, 2> shimmerPhase { 0.0, 0.37 };
    std::array<float, 2> bodyLowPass { 0.0f, 0.0f };
    std::array<float, 2> haloHighPassInput { 0.0f, 0.0f };
    std::array<float, 2> haloHighPassOutput { 0.0f, 0.0f };
    juce::AudioBuffer<float> delayMemory;
    juce::AudioBuffer<float> bodyBuffer;
    juce::AudioBuffer<float> haloBuffer;
    juce::AudioBuffer<float> shimmerMemory;
    juce::Reverb bodyTank;
    juce::Reverb haloTank;
    StageToneFilter toneFilter;
    juce::SmoothedValue<float> spaceAmount;
    juce::SmoothedValue<float> motionAmount;
    juce::SmoothedValue<float> chaosAmount;
    juce::SmoothedValue<float> shimmerAmount;
};
