#include "GravityBloomLayer.h"

namespace
{
float smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}
}

float GravityBloomLayer::activationForSpace(float space) noexcept
{
    return smoothStep((space - 0.55f) / 0.30f);
}

float GravityBloomLayer::bloomDelayMs(int bloom, float space) noexcept
{
    space = juce::jlimit(0.0f, 1.0f, space);
    const bool second = bloom != 0;
    if (space <= 0.90f)
    {
        const float position = smoothStep((space - 0.55f) / 0.35f);
        return second ? 95.0f + 195.0f * position
                      : 55.0f + 125.0f * position;
    }

    const float eventHorizon = smoothStep((space - 0.90f) / 0.10f);
    return second ? 290.0f + 1510.0f * eventHorizon
                  : 180.0f + 1020.0f * eventHorizon;
}

float GravityBloomLayer::dryInjectionGain(float space) noexcept
{
    return 0.06f + 0.06f * smoothStep((space - 0.75f) / 0.25f);
}

float GravityBloomLayer::layer3PreservationGain(float space) noexcept
{
    const float normal = 0.28f - 0.03f
        * smoothStep((space - 0.85f) / 0.15f);
    const float eventHorizon = 0.13f
        * smoothStep((space - 0.90f) / 0.10f);
    return normal + eventHorizon;
}

float GravityBloomLayer::chorusPreservationGain(float space) noexcept
{
    return 0.065f * smoothStep((space - 0.72f) / 0.28f);
}

float GravityBloomLayer::chorusSidePreservationGain(float space) noexcept
{
    return 0.045f * smoothStep((space - 0.72f) / 0.28f);
}

float GravityBloomLayer::shimmerAmountForSpace(float space,
                                                bool haloEnabled) noexcept
{
    return haloEnabled ? 0.18f * smoothStep((space - 0.75f) / 0.25f) : 0.0f;
}

void GravityBloomLayer::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    delayMemory.setSize(2, juce::roundToInt(currentSampleRate * 2.35) + 8,
                        false, true, false);
    bodyBuffer.setSize(2, maximumBlockSize, false, true, false);
    haloBuffer.setSize(2, maximumBlockSize, false, true, false);
    shimmerWindowSamples = juce::jmax(32,
        juce::roundToInt(currentSampleRate * 0.065));
    shimmerMemory.setSize(2, shimmerWindowSamples
        + juce::roundToInt(currentSampleRate * 0.010) + 16,
        false, true, false);

    bodyTank.setSampleRate(currentSampleRate);
    haloTank.setSampleRate(currentSampleRate);
    juce::Reverb::Parameters body;
    body.roomSize = 0.89f;
    body.damping = 0.67f;
    body.wetLevel = 0.90f;
    body.dryLevel = 0.0f;
    body.width = 0.96f;
    bodyTank.setParameters(body);
    juce::Reverb::Parameters halo;
    halo.roomSize = 0.96f;
    halo.damping = 0.23f;
    halo.wetLevel = 0.86f;
    halo.dryLevel = 0.0f;
    halo.width = 1.0f;
    haloTank.setParameters(halo);

    toneFilter.prepare(currentSampleRate);
    spaceAmount.reset(currentSampleRate, 0.180);
    motionAmount.reset(currentSampleRate, 0.120);
    chaosAmount.reset(currentSampleRate, 0.120);
    shimmerAmount.reset(currentSampleRate, 0.100);
    reset();
}

void GravityBloomLayer::reset()
{
    delayMemory.clear();
    bodyBuffer.clear();
    haloBuffer.clear();
    shimmerMemory.clear();
    bodyTank.reset();
    haloTank.reset();
    toneFilter.reset();
    writePosition = 0;
    bloomPhaseA = 0.0;
    bloomPhaseB = 2.1;
    spatialPhase = 0.0;
    spatialDriftPhase = 0.0;
    dryEnvelope = 0.0f;
    shimmerWritePosition = 0;
    shimmerPhase = { 0.0, 0.37 };
    bodyLowPass.fill(0.0f);
    haloHighPassInput.fill(0.0f);
    haloHighPassOutput.fill(0.0f);
    spaceAmount.setCurrentAndTargetValue(0.0f);
    motionAmount.setCurrentAndTargetValue(0.0f);
    chaosAmount.setCurrentAndTargetValue(0.0f);
    shimmerAmount.setCurrentAndTargetValue(0.0f);
}

float GravityBloomLayer::readDelay(int channel, float delaySamples) const noexcept
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

float GravityBloomLayer::readShimmerDelay(int channel,
                                           float delaySamples) const noexcept
{
    const int size = shimmerMemory.getNumSamples();
    float position = static_cast<float>(shimmerWritePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    while (position >= static_cast<float>(size))
        position -= static_cast<float>(size);
    const int a = static_cast<int>(position);
    const int b = (a + 1) % size;
    const float fraction = position - static_cast<float>(a);
    const auto* memory = shimmerMemory.getReadPointer(channel);
    return memory[a] + fraction * (memory[b] - memory[a]);
}

void GravityBloomLayer::process(const juce::AudioBuffer<float>& layer3Input,
                                const juce::AudioBuffer<float>& dryInput,
                                const juce::AudioBuffer<float>& layer1Tap,
                                const juce::AudioBuffer<float>& layer2Tap,
                                juce::AudioBuffer<float>& output,
                                MacroState macros)
{
    const int samples = layer3Input.getNumSamples();
    jassert(layer3Input.getNumChannels() >= 2);
    jassert(dryInput.getNumChannels() >= 2);
    jassert(layer1Tap.getNumChannels() >= 2);
    jassert(layer2Tap.getNumChannels() >= 2);
    jassert(dryInput.getNumSamples() >= samples);
    jassert(layer1Tap.getNumSamples() >= samples);
    jassert(layer2Tap.getNumSamples() >= samples);

    if (bodyBuffer.getNumSamples() < samples)
    {
        bodyBuffer.setSize(2, samples, false, false, true);
        haloBuffer.setSize(2, samples, false, false, true);
    }
    output.setSize(2, samples, false, false, true);
    spaceAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.space));
    motionAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.motion));
    chaosAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.chaos));
    shimmerAmount.setTargetValue(shimmerAmountForSpace(macros.space, macros.halo));
    // HALO is applied once after the complete four-layer network. The flag is
    // still used above to enable shimmer, while BODY remains local to Layer 4.
    toneFilter.setModes(macros.body, false);

    for (int sample = 0; sample < samples; ++sample)
    {
        const float space = spaceAmount.getNextValue();
        const float motion = motionAmount.getNextValue();
        const float chaos = chaosAmount.getNextValue();
        const float shimmerBlend = shimmerAmount.getNextValue();
        const float activation = activationForSpace(space);
        const float dryLevel = 0.5f * (std::abs(dryInput.getSample(0, sample))
                                    + std::abs(dryInput.getSample(1, sample)));
        const float envelopeSeconds = dryLevel > dryEnvelope ? 0.006f : 0.58f;
        const float envelopeCoefficient = std::exp(-1.0f
            / static_cast<float>(currentSampleRate * envelopeSeconds));
        dryEnvelope = dryLevel + envelopeCoefficient * (dryEnvelope - dryLevel);
        const float ducking = 1.0f - 0.62f
            * juce::jlimit(0.0f, 1.0f, dryEnvelope * 2.5f);

        for (int channel = 0; channel < 2; ++channel)
        {
            const float excitation = activation
                * (layer3Input.getSample(channel, sample) * 0.60f
                 + dryInput.getSample(channel, sample) * dryInjectionGain(space)
                 + layer1Tap.getSample(channel, sample) * 0.80f);
            delayMemory.setSample(channel, writePosition, excitation);
        }

        const float danger = smoothStep((chaos - 0.90f) / 0.10f);
        const float driftDepth = 0.003f + 0.020f * motion + 0.012f * chaos;
        const float rateMultiplier = 1.0f + 3.0f * danger;
        const float driftA = driftDepth * static_cast<float>(std::sin(bloomPhaseA));
        const float driftB = driftDepth * static_cast<float>(std::sin(bloomPhaseB));
        const float samplesPerMs = static_cast<float>(currentSampleRate / 1000.0);

        for (int channel = 0; channel < 2; ++channel)
        {
            const float stereoA = channel == 0 ? 0.94f : 1.06f;
            const float stereoB = channel == 0 ? 1.08f : 0.96f;
            const float delayA = bloomDelayMs(0, space) * stereoA
                               * (1.0f - 0.035f * chaos)
                               * (1.0f + driftA) * samplesPerMs;
            const float delayB = bloomDelayMs(1, space) * stereoB
                               * (1.0f + 0.060f * chaos)
                               * (1.0f + driftB) * samplesPerMs;
            const float bodyTap = readDelay(channel, delayA);
            const float haloTap = readDelay(channel, delayB);
            const auto index = static_cast<size_t>(channel);
            const float bodyCoefficient = 1.0f - std::exp(
                -juce::MathConstants<float>::twoPi * (1700.0f - 450.0f * space)
                / static_cast<float>(currentSampleRate));
            bodyLowPass[index] += bodyCoefficient
                                * (bodyTap - bodyLowPass[index]);
            const float haloCutoff = 1450.0f + 550.0f * chaos;
            const float rc = 1.0f / (juce::MathConstants<float>::twoPi * haloCutoff);
            const float hpCoefficient = rc
                / (rc + 1.0f / static_cast<float>(currentSampleRate));
            const float highPassed = hpCoefficient
                * (haloHighPassOutput[index] + haloTap - haloHighPassInput[index]);
            haloHighPassInput[index] = haloTap;
            haloHighPassOutput[index] = highPassed;
            shimmerMemory.setSample(channel, shimmerWritePosition, highPassed);
            const float detuneCents = 2.0f + 4.0f * motion;
            const float detuneSign = channel == 0 ? -1.0f : 1.0f;
            const double ratio = 2.0 * std::pow(2.0,
                static_cast<double>(detuneSign * detuneCents) / 1200.0);
            const double phaseA = shimmerPhase[index];
            const double phaseB = std::fmod(phaseA + 0.5, 1.0);
            const float windowA = std::pow(static_cast<float>(
                std::sin(juce::MathConstants<double>::pi * phaseA)), 2.0f);
            const float windowB = std::pow(static_cast<float>(
                std::sin(juce::MathConstants<double>::pi * phaseB)), 2.0f);
            constexpr float minimumDelay = 64.0f;
            const float shiftedA = readShimmerDelay(channel, minimumDelay
                + static_cast<float>((1.0 - phaseA) * shimmerWindowSamples));
            const float shiftedB = readShimmerDelay(channel, minimumDelay
                + static_cast<float>((1.0 - phaseB) * shimmerWindowSamples));
            const float shifted = (shiftedA * windowA + shiftedB * windowB)
                                / juce::jmax(0.001f, windowA + windowB);
            shimmerPhase[index] = std::fmod(phaseA
                + (ratio - 1.0) / static_cast<double>(shimmerWindowSamples), 1.0);
            bodyBuffer.setSample(channel, sample,
                bodyLowPass[index] * ducking * 0.78f);
            haloBuffer.setSample(channel, sample,
                (highPassed + shifted * shimmerBlend) * ducking * 0.72f);
        }

        writePosition = (writePosition + 1) % delayMemory.getNumSamples();
        shimmerWritePosition = (shimmerWritePosition + 1)
                             % shimmerMemory.getNumSamples();
        bloomPhaseA += juce::MathConstants<double>::twoPi
                     * (0.011 + 0.052 * motion) * rateMultiplier / currentSampleRate;
        bloomPhaseB += juce::MathConstants<double>::twoPi
                     * (0.017 + 0.071 * motion) * rateMultiplier / currentSampleRate;
        if (bloomPhaseA >= juce::MathConstants<double>::twoPi)
            bloomPhaseA -= juce::MathConstants<double>::twoPi;
        if (bloomPhaseB >= juce::MathConstants<double>::twoPi)
            bloomPhaseB -= juce::MathConstants<double>::twoPi;
    }

    bodyTank.processStereo(bodyBuffer.getWritePointer(0),
                           bodyBuffer.getWritePointer(1), samples);
    haloTank.processStereo(haloBuffer.getWritePointer(0),
                           haloBuffer.getWritePointer(1), samples);

    for (int sample = 0; sample < samples; ++sample)
    {
        const float space = spaceAmount.getCurrentValue();
        const float motion = motionAmount.getCurrentValue();
        const float chaos = chaosAmount.getCurrentValue();
        const float activation = activationForSpace(space);
        const float panMix = 0.38f * smoothStep(motion);
        const float bodyPan = 0.82f * motion * static_cast<float>(std::sin(spatialPhase));
        const float haloPan = 0.88f * motion * static_cast<float>(std::sin(
            spatialPhase + 2.73 + 0.31 * std::sin(spatialDriftPhase)));
        const auto panPair = [] (float mono, float pan)
        {
            const float angle = (pan + 1.0f)
                              * juce::MathConstants<float>::pi * 0.25f;
            return std::array<float, 2> {
                mono * std::cos(angle) * std::sqrt(2.0f),
                mono * std::sin(angle) * std::sqrt(2.0f)
            };
        };

        const float bodyLeft = bodyBuffer.getSample(0, sample);
        const float bodyRight = bodyBuffer.getSample(1, sample);
        const float haloLeft = haloBuffer.getSample(0, sample);
        const float haloRight = haloBuffer.getSample(1, sample);
        const auto pannedBody = panPair(0.5f * (bodyLeft + bodyRight), bodyPan);
        const auto pannedHalo = panPair(0.5f * (haloLeft + haloRight), haloPan);
        const float bodyWeight = 0.62f - 0.12f * chaos;
        const float haloWeight = 1.0f - bodyWeight;
        float wetLeft = bodyWeight * (bodyLeft + panMix * (pannedBody[0] - bodyLeft))
                      + haloWeight * (haloLeft + panMix * (pannedHalo[0] - haloLeft));
        float wetRight = bodyWeight * (bodyRight + panMix * (pannedBody[1] - bodyRight))
                       + haloWeight * (haloRight + panMix * (pannedHalo[1] - haloRight));

        const float preserved = layer3PreservationGain(space);
        const float chorusPreserved = chorusPreservationGain(space);
        const float chorusSide = 0.5f
            * (layer2Tap.getSample(0, sample)
             - layer2Tap.getSample(1, sample));
        const float chorusSidePreserved = chorusSidePreservationGain(space);
        float processedLeft = layer3Input.getSample(0, sample) * preserved
                            + wetLeft * (1.0f - preserved)
                            + layer2Tap.getSample(0, sample) * chorusPreserved
                            + chorusSide * chorusSidePreserved;
        float processedRight = layer3Input.getSample(1, sample) * preserved
                             + wetRight * (1.0f - preserved)
                             + layer2Tap.getSample(1, sample) * chorusPreserved
                             - chorusSide * chorusSidePreserved;
        toneFilter.processStereoSample(processedLeft, processedRight);
        output.setSample(0, sample, layer3Input.getSample(0, sample)
            * (1.0f - activation) + processedLeft * activation);
        output.setSample(1, sample, layer3Input.getSample(1, sample)
            * (1.0f - activation) + processedRight * activation);

        const float movementRate = (0.025f + 0.16f * motion)
            * (1.0f + 3.0f * smoothStep((chaos - 0.90f) / 0.10f));
        spatialPhase += juce::MathConstants<double>::twoPi
                      * movementRate / currentSampleRate;
        spatialDriftPhase += juce::MathConstants<double>::twoPi
                           * 0.013 / currentSampleRate;
        if (spatialPhase >= juce::MathConstants<double>::twoPi)
            spatialPhase -= juce::MathConstants<double>::twoPi;
        if (spatialDriftPhase >= juce::MathConstants<double>::twoPi)
            spatialDriftPhase -= juce::MathConstants<double>::twoPi;
    }
}
