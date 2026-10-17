#include "TotalSpectralReturn.h"

namespace
{
float smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}
}

float TotalSpectralReturn::activationForSpace(float space) noexcept
{
    return smoothStep((space - 0.90f) / 0.10f);
}

float TotalSpectralReturn::maximumGainForSpace(float space) noexcept
{
    return 0.0045f * activationForSpace(space);
}

float TotalSpectralReturn::bandFrequencyHz(int band) noexcept
{
    constexpr std::array<float, 3> frequencies { 4100.0f, 7700.0f, 13100.0f };
    return frequencies[static_cast<size_t>(juce::jlimit(0, 2, band))];
}

void TotalSpectralReturn::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    delayMemory.setSize(2,
        juce::roundToInt(currentSampleRate * 0.180) + maximumBlockSize + 8,
        false, true, false);
    juce::dsp::ProcessSpec monoSpec {
        currentSampleRate,
        static_cast<juce::uint32>(juce::jmax(1, maximumBlockSize)),
        1
    };
    constexpr std::array<float, 3> qValues { 2.2f, 2.7f, 1.8f };
    for (auto& channel : bands)
        for (size_t band = 0; band < channel.size(); ++band)
        {
            channel[band].prepare(monoSpec);
            channel[band].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
            channel[band].setResonance(qValues[band]);
        }
    gainAmount.reset(currentSampleRate, 0.180);
    reset();
}

void TotalSpectralReturn::reset()
{
    delayMemory.clear();
    for (auto& channel : bands)
        for (auto& filter : channel)
            filter.reset();
    writePosition = 0;
    driftPhase = 0.0;
    returnEnvelope = 0.0f;
    gainAmount.setCurrentAndTargetValue(0.0f);
}

float TotalSpectralReturn::readDelay(int channel,
                                     float delaySamples) const noexcept
{
    const int size = delayMemory.getNumSamples();
    float position = static_cast<float>(writePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    while (position >= static_cast<float>(size))
        position -= static_cast<float>(size);
    const int a = static_cast<int>(position);
    const int b = (a + 1) % size;
    const float fraction = position - static_cast<float>(a);
    const auto* memory = delayMemory.getReadPointer(channel);
    return memory[a] + fraction * (memory[b] - memory[a]);
}

void TotalSpectralReturn::process(const juce::AudioBuffer<float>& input,
                                  juce::AudioBuffer<float>& output,
                                  float space, float motion, float chaos)
{
    const int samples = input.getNumSamples();
    output.setSize(2, samples, false, false, true);
    output.clear();
    gainAmount.setTargetValue(maximumGainForSpace(space));

    const float safeNyquist = static_cast<float>(currentSampleRate * 0.44);
    const float driftDepth = 0.004f + 0.016f * juce::jlimit(0.0f, 1.0f, motion);
    const float drift = driftDepth * static_cast<float>(std::sin(driftPhase));
    const float oppositeDrift = driftDepth * static_cast<float>(
        std::sin(driftPhase + 2.17));
    const float qLift = 1.0f + 0.35f * smoothStep((chaos - 0.90f) / 0.10f);
    constexpr std::array<float, 3> qValues { 2.2f, 2.7f, 1.8f };
    for (int channel = 0; channel < 2; ++channel)
        for (int band = 0; band < 3; ++band)
        {
            const float channelDrift = channel == 0 ? drift : oppositeDrift;
            bands[static_cast<size_t>(channel)][static_cast<size_t>(band)]
                .setCutoffFrequency(juce::jmin(safeNyquist,
                    bandFrequencyHz(band) * (1.0f + channelDrift)));
            bands[static_cast<size_t>(channel)][static_cast<size_t>(band)]
                .setResonance(qValues[static_cast<size_t>(band)] * qLift);
        }

    const float samplesPerMs = static_cast<float>(currentSampleRate / 1000.0);
    for (int sample = 0; sample < samples; ++sample)
    {
        for (int channel = 0; channel < 2; ++channel)
            delayMemory.setSample(channel, writePosition,
                                  input.getSample(channel, sample));

        std::array<float, 2> filtered {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const float stereoDelay = channel == 0 ? 83.0f : 117.0f;
            const float movingDelay = stereoDelay
                * (1.0f + (channel == 0 ? drift : oppositeDrift));
            const float delayed = readDelay(channel, movingDelay * samplesPerMs);
            float sum = 0.0f;
            for (auto& filter : bands[static_cast<size_t>(channel)])
                sum += filter.processSample(0, delayed);
            filtered[static_cast<size_t>(channel)] = sum * (1.0f / 3.0f);
        }

        const float detector = 0.5f
            * (std::abs(filtered[0]) + std::abs(filtered[1]));
        const float seconds = detector > returnEnvelope ? 0.004f : 0.40f;
        const float envelopeCoefficient = std::exp(-1.0f
            / static_cast<float>(currentSampleRate * seconds));
        returnEnvelope = detector
            + envelopeCoefficient * (returnEnvelope - detector);
        const float governor = juce::jmin(1.0f,
            0.035f / juce::jmax(1.0e-6f, returnEnvelope));
        const float gain = gainAmount.getNextValue() * governor;
        for (int channel = 0; channel < 2; ++channel)
        {
            const float bounded = std::tanh(
                filtered[static_cast<size_t>(channel)] * gain);
            output.setSample(channel, sample,
                juce::jlimit(-0.012f, 0.012f, bounded));
        }

        writePosition = (writePosition + 1) % delayMemory.getNumSamples();
        driftPhase += juce::MathConstants<double>::twoPi
                    * (0.021 + 0.085 * motion)
                    / currentSampleRate;
        if (driftPhase >= juce::MathConstants<double>::twoPi)
            driftPhase -= juce::MathConstants<double>::twoPi;
    }
}
