#pragma once

#include <JuceHeader.h>
#include "StageToneFilter.h"

// Layer 2: a wide, chorused space with two deliberately non-identical stereo
// trajectories. It owns its small, delayed self-return and exposes a separate
// low-cut return for Layer 1, so neither feedback path can become instantaneous.
class ChorusedSpaceLayer final
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

    void process(const juce::AudioBuffer<float>& input,
                 juce::AudioBuffer<float>& stage3Feed,
                 juce::AudioBuffer<float>& feedbackToLayer1,
                 MacroState macros);

    static float movementRateHz(float chaos) noexcept;
    static float chorusAmountForChaos(float chaos) noexcept;
    static float layer1FeedbackGain(float space) noexcept;
    static float selfFeedbackGain(float space) noexcept;
    static float feedbackHighPassCutoffHz(float space) noexcept;
    static float feedbackLowPassCutoffHz(float space) noexcept;
    static float spatialChopperAmount(float motion) noexcept;
    static float spatialChopperRateHz(float motion) noexcept;
    static float motionInputTrim(float motion) noexcept;
    static float chorusMotionAmount(float motion) noexcept;
    static float internalDecorrelationDepthMs(float motion, float chaos) noexcept;

private:
    float readChorusDelay(int channel, float delayInSamples) const noexcept;
    float readSelfDelay(int channel) const noexcept;

    double currentSampleRate = 44100.0;
    int chorusWritePosition = 0;
    int selfWritePosition = 0;
    int selfDelaySamples = 1;
    double movementPhase = 0.0;
    double driftPhase = 0.0;
    double chopperPhase = 0.0;
    std::array<float, 2> highPassInput { 0.0f, 0.0f };
    std::array<float, 2> highPassOutput { 0.0f, 0.0f };
    std::array<float, 2> highPassInput2 { 0.0f, 0.0f };
    std::array<float, 2> highPassOutput2 { 0.0f, 0.0f };
    std::array<float, 2> lowPassState { 0.0f, 0.0f };
    std::array<float, 2> lowPassState2 { 0.0f, 0.0f };

    juce::AudioBuffer<float> chorusDelay;
    juce::AudioBuffer<float> selfDelay;
    juce::AudioBuffer<float> tankInput;
    juce::AudioBuffer<float> limitedInput;
    juce::AudioBuffer<float> closeTankBuffer;
    juce::AudioBuffer<float> largeTankBuffer;
    juce::AudioBuffer<float> directBuffer;
    juce::Reverb closeTank;
    juce::Reverb largeTank;
    juce::dsp::Limiter<float> inputLimiter;
    StageToneFilter toneFilter;
    juce::SmoothedValue<float> spaceAmount;
    juce::SmoothedValue<float> motionAmount;
    juce::SmoothedValue<float> chaosAmount;
};
