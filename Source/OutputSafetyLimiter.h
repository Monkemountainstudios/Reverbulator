#pragma once

#include <JuceHeader.h>
#include <cmath>

class OutputSafetyLimiter
{
public:
    static constexpr float ceilingDecibels = -0.3f;

    void prepare(double sampleRate) noexcept
    {
        const auto safeRate = juce::jmax(1.0, sampleRate);
        releaseCoefficient = std::exp(-1.0f
            / static_cast<float>(safeRate * releaseSeconds));
        reset();
    }

    void reset() noexcept
    {
        gain = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        jassert(buffer.getNumChannels() >= 2);
        const float ceiling = juce::Decibels::decibelsToGain(ceilingDecibels);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float left = buffer.getSample(0, sample);
            const float right = buffer.getSample(1, sample);
            if (! std::isfinite(left) || ! std::isfinite(right))
            {
                buffer.setSample(0, sample, 0.0f);
                buffer.setSample(1, sample, 0.0f);
                gain = 0.0f;
                continue;
            }

            const float peak = juce::jmax(std::abs(left), std::abs(right));
            const float requiredGain = peak > ceiling ? ceiling / peak : 1.0f;

            // Attenuate immediately to guarantee the ceiling, then recover
            // slowly enough to avoid stereo image shifts or obvious pumping.
            if (requiredGain < gain)
                gain = requiredGain;
            else
                gain = requiredGain
                    + releaseCoefficient * (gain - requiredGain);

            buffer.setSample(0, sample, left * gain);
            buffer.setSample(1, sample, right * gain);
        }
    }

private:
    static constexpr double releaseSeconds = 0.120;
    float releaseCoefficient = 0.0f;
    float gain = 1.0f;
};
