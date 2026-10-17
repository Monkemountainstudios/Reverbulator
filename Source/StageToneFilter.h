#pragma once

#include <JuceHeader.h>

// Shared tone stage used at every reverb output. BODY gently removes top end;
// HALO uses a forgiving low cut plus a restrained presence resonance. Both
// paths run continuously and are smoothly faded to avoid clicks.
class StageToneFilter final
{
public:
    void prepare(double sampleRate)
    {
        const float rate = static_cast<float>(juce::jmax(1.0, sampleRate));
        lowPassCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * 9000.0f / rate);

        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * 440.0f);
        highPassCoefficient = rc / (rc + 1.0f / rate);

        // RBJ peaking EQ: +1.6 dB at 4.2 kHz, broad enough to feel like lift
        // rather than another exposed resonant mode.
        constexpr float centre = 4200.0f;
        constexpr float q = 0.90f;
        constexpr float gainDb = 1.60f;
        const float amplitude = std::pow(10.0f, gainDb / 40.0f);
        const float omega = juce::MathConstants<float>::twoPi * centre / rate;
        const float alpha = std::sin(omega) / (2.0f * q);
        const float a0 = 1.0f + alpha / amplitude;
        peakB0 = (1.0f + alpha * amplitude) / a0;
        peakB1 = (-2.0f * std::cos(omega)) / a0;
        peakB2 = (1.0f - alpha * amplitude) / a0;
        peakA1 = (-2.0f * std::cos(omega)) / a0;
        peakA2 = (1.0f - alpha / amplitude) / a0;

        bodyAmount.reset(sampleRate, 0.030);
        haloAmount.reset(sampleRate, 0.030);
        reset();
    }

    void reset()
    {
        lowPassState.fill(0.0f);
        highPassInput.fill(0.0f);
        highPassOutput.fill(0.0f);
        peakInput1.fill(0.0f);
        peakInput2.fill(0.0f);
        peakOutput1.fill(0.0f);
        peakOutput2.fill(0.0f);
        bodyAmount.setCurrentAndTargetValue(0.0f);
        haloAmount.setCurrentAndTargetValue(0.0f);
    }

    void setModes(bool bodyEnabled, bool haloEnabled)
    {
        bodyAmount.setTargetValue(bodyEnabled ? 1.0f : 0.0f);
        haloAmount.setTargetValue(haloEnabled ? 1.0f : 0.0f);
    }

    void processStereoSample(float& left, float& right) noexcept
    {
        const float body = bodyAmount.getNextValue();
        const float halo = haloAmount.getNextValue();
        left = processChannel(0, left, body, halo);
        right = processChannel(1, right, body, halo);
    }

private:
    float processChannel(int channel, float input, float body, float halo) noexcept
    {
        const auto index = static_cast<size_t>(channel);
        lowPassState[index] += lowPassCoefficient
                            * (input - lowPassState[index]);
        const float bodyOutput = input + body * (lowPassState[index] - input);

        const float highPassed = highPassCoefficient
            * (highPassOutput[index] + bodyOutput - highPassInput[index]);
        highPassInput[index] = bodyOutput;
        highPassOutput[index] = highPassed;
        const float lifted = peakB0 * highPassed
                           + peakB1 * peakInput1[index]
                           + peakB2 * peakInput2[index]
                           - peakA1 * peakOutput1[index]
                           - peakA2 * peakOutput2[index];
        peakInput2[index] = peakInput1[index];
        peakInput1[index] = highPassed;
        peakOutput2[index] = peakOutput1[index];
        peakOutput1[index] = lifted;
        return bodyOutput + halo * (lifted - bodyOutput);
    }

    float lowPassCoefficient = 1.0f;
    float highPassCoefficient = 0.0f;
    float peakB0 = 1.0f;
    float peakB1 = 0.0f;
    float peakB2 = 0.0f;
    float peakA1 = 0.0f;
    float peakA2 = 0.0f;
    std::array<float, 2> lowPassState { 0.0f, 0.0f };
    std::array<float, 2> highPassInput { 0.0f, 0.0f };
    std::array<float, 2> highPassOutput { 0.0f, 0.0f };
    std::array<float, 2> peakInput1 { 0.0f, 0.0f };
    std::array<float, 2> peakInput2 { 0.0f, 0.0f };
    std::array<float, 2> peakOutput1 { 0.0f, 0.0f };
    std::array<float, 2> peakOutput2 { 0.0f, 0.0f };
    juce::SmoothedValue<float> bodyAmount;
    juce::SmoothedValue<float> haloAmount;
};
