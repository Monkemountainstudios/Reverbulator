#pragma once

#include <JuceHeader.h>
#include "StageToneFilter.h"

// Layer 3: a dense, bright digital tank illuminated by a parallel bank of
// frequency-locked band-pass filters. It deliberately has no external return
// yet; the spectral character can be judged before adding another feedback arc.
class DigitalSpectralLayer final
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

    void process(const juce::AudioBuffer<float>& layer2Input,
                 const juce::AudioBuffer<float>& layer1Bypass,
                 juce::AudioBuffer<float>& layer4Feed,
                 MacroState macros);

    static float activationForSpace(float space) noexcept;
    static float flutterAmountForChaos(float chaos) noexcept;
    static float flutterRateHz(float chaos) noexcept;
    static float bandRatio(int band, float chaos) noexcept;
    static float bandPredelayMs(int channel, int band, float chaos) noexcept;
    static float targetDecaySeconds(float space) noexcept;
    static float tankFeedbackForSpace(float space) noexcept;
    static float tankModulationDepthSamples(float motion, float chaos) noexcept;
    static float serialDryGainForSpace(float space) noexcept;
    static float stereoPreservationGainForSpace(float space) noexcept;

private:
    struct StereoAllpass
    {
        void prepare(int delaySamples);
        void reset();
        void process(float& left, float& right, float coefficient) noexcept;

        juce::AudioBuffer<float> memory;
        int position = 0;
    };

    static int nextPrime(int value) noexcept;
    void updateBandTargets(float motion, float chaos);
    float readBandDelay(int channel, int band, float delaySamples) const noexcept;
    std::array<float, 2> processTank(float left, float right,
                                     float space, float motion,
                                     float chaos) noexcept;

    double currentSampleRate = 44100.0;
    double spectralPhase = 0.0;
    double flutterPhase = 0.0;
    std::array<StereoAllpass, 4> diffusers;
    std::array<std::vector<float>, 8> tankMemory;
    std::array<int, 8> tankPosition {};
    std::array<int, 8> tankBaseDelaySamples {};
    std::array<double, 8> tankModulationPhase {};
    std::array<float, 8> dampingState {};
    std::array<float, 2> flutterLowState { 0.0f, 0.0f };
    std::array<std::array<juce::dsp::StateVariableTPTFilter<float>, 4>, 2> bandFilters;
    std::array<std::array<juce::SmoothedValue<float>, 4>, 2> bandFrequencies;
    juce::AudioBuffer<float> bandDelayMemory;
    int bandDelayWritePosition = 0;
    juce::AudioBuffer<float> limitedInput;
    juce::dsp::Limiter<float> inputLimiter;
    StageToneFilter toneFilter;
    juce::SmoothedValue<float> spaceAmount;
    juce::SmoothedValue<float> motionAmount;
    juce::SmoothedValue<float> chaosAmount;
};
