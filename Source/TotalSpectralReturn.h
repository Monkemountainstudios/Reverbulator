#pragma once

#include <JuceHeader.h>

// A deliberately tiny, guarded return from the complete four-layer output to
// Layer 1. Only three high-frequency bands survive, and the path includes a
// modulated delay, energy governor, soft saturation, and absolute ceiling.
class TotalSpectralReturn final
{
public:
    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    void process(const juce::AudioBuffer<float>& input,
                 juce::AudioBuffer<float>& output,
                 float space, float motion, float chaos);

    static float activationForSpace(float space) noexcept;
    static float maximumGainForSpace(float space) noexcept;
    static float bandFrequencyHz(int band) noexcept;

private:
    float readDelay(int channel, float delaySamples) const noexcept;

    double currentSampleRate = 44100.0;
    int writePosition = 0;
    double driftPhase = 0.0;
    float returnEnvelope = 0.0f;
    juce::AudioBuffer<float> delayMemory;
    std::array<std::array<juce::dsp::StateVariableTPTFilter<float>, 3>, 2> bands;
    juce::SmoothedValue<float> gainAmount;
};
