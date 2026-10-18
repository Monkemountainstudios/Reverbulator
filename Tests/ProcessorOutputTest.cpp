#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr float inputCeiling = 0.89125094f; // -1 dBFS
}

int main()
{
    ReverbulatorAudioProcessor processor;
    processor.setCurrentProgram(8); // PURE HORROR
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random(0x53525334);
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    bool allFinite = true;
    constexpr int renderSamples = static_cast<int>(sampleRate * 12.0);

    for (int offset = 0; offset < renderSamples; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, renderSamples - offset);
        buffer.setSize(2, count, false, false, true);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absoluteSample = offset + sample;
            const float time = static_cast<float>(absoluteSample / sampleRate);
            const float transient = absoluteSample % 6000 < 36
                ? 0.72f * std::exp(-static_cast<float>(absoluteSample % 6000) / 9.0f)
                : 0.0f;
            for (int channel = 0; channel < 2; ++channel)
            {
                const float phaseOffset = channel == 0 ? 0.0f : 0.37f;
                const float tone = 0.42f * std::sin(
                    juce::MathConstants<float>::twoPi * 117.0f * time + phaseOffset)
                    + 0.18f * std::sin(
                        juce::MathConstants<float>::twoPi * 887.0f * time - phaseOffset);
                const float noise = 0.08f * (2.0f * random.nextFloat() - 1.0f);
                const float input = juce::jlimit(-inputCeiling, inputCeiling,
                                                  tone + transient + noise);
                buffer.setSample(channel, sample, input);
                inputPeak = juce::jmax(inputPeak, std::abs(input));
            }
        }

        processor.processBlock(buffer, midi);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const float value = buffer.getSample(channel, sample);
                allFinite = allFinite && std::isfinite(value);
                outputPeak = juce::jmax(outputPeak, std::abs(value));
            }
    }

    std::cout << "PURE HORROR insert test peaks: input=" << inputPeak
              << " output=" << outputPeak << '\n';

    if (! allFinite)
    {
        std::cerr << "PURE HORROR produced non-finite output\n";
        return 1;
    }
    if (outputPeak > 0.9670f) // -0.3 dBFS, with rounding tolerance
    {
        std::cerr << "Final output exceeded the safety ceiling\n";
        return 2;
    }

    ReverbulatorAudioProcessor dryProcessor;
    if (auto* mix = dryProcessor.parameters.getParameter("mix"))
        mix->setValueNotifyingHost(0.0f);
    dryProcessor.prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> dryBuffer(2, blockSize);
    juce::AudioBuffer<float> dryReference(2, blockSize);
    for (int sample = 0; sample < blockSize; ++sample)
        for (int channel = 0; channel < 2; ++channel)
        {
            const float value = 0.5f * std::sin(
                juce::MathConstants<float>::twoPi * 220.0f
                * static_cast<float>(sample) / static_cast<float>(sampleRate));
            dryBuffer.setSample(channel, sample, value);
            dryReference.setSample(channel, sample, value);
        }
    dryProcessor.processBlock(dryBuffer, midi);

    float dryDifference = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            dryDifference = juce::jmax(dryDifference, std::abs(
                dryBuffer.getSample(channel, sample)
                - dryReference.getSample(channel, sample)));
    std::cout << "Sub-ceiling dry-path maximum difference: "
              << dryDifference << '\n';
    if (dryDifference > 1.0e-7f)
    {
        std::cerr << "Safety limiter changed sub-ceiling dry audio\n";
        return 3;
    }

    dryBuffer.clear();
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            dryBuffer.setSample(channel, sample, channel == 0 ? 1.2f : -1.2f);
    dryProcessor.processBlock(dryBuffer, midi);
    const float manualPeak = juce::jmax(
        dryBuffer.getMagnitude(0, 0, blockSize),
        dryBuffer.getMagnitude(1, 0, blockSize));
    std::cout << "MANUAL over-ceiling output peak: " << manualPeak << '\n';
    if (manualPeak > 0.9670f)
    {
        std::cerr << "MANUAL output bypassed the universal safety limiter\n";
        return 4;
    }

    return 0;
}
