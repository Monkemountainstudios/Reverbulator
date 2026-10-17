#include "DigitalSpectralLayer.h"

namespace
{
float smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}

constexpr std::array<float, 5> leftRootSequence {
    1.0f, 1.25f, 1.50f, 2.0f, 1.333333f
};
constexpr std::array<float, 7> rightRootSequence {
    1.50f, 1.125f, 1.75f, 1.333333f, 2.0f, 1.25f, 1.0f
};
}

void DigitalSpectralLayer::StereoAllpass::prepare(int delaySamples)
{
    memory.setSize(2, juce::jmax(2, delaySamples), false, true, false);
    reset();
}

void DigitalSpectralLayer::StereoAllpass::reset()
{
    memory.clear();
    position = 0;
}

void DigitalSpectralLayer::StereoAllpass::process(float& left, float& right,
                                                   float coefficient) noexcept
{
    const std::array<float, 2> input { left, right };
    std::array<float, 2> output {};
    for (int channel = 0; channel < 2; ++channel)
    {
        const float delayed = memory.getSample(channel, position);
        output[static_cast<size_t>(channel)] = delayed
                                             - coefficient * input[static_cast<size_t>(channel)];
        memory.setSample(channel, position,
            input[static_cast<size_t>(channel)]
            + coefficient * output[static_cast<size_t>(channel)]);
    }
    left = output[0];
    right = output[1];
    position = (position + 1) % memory.getNumSamples();
}

int DigitalSpectralLayer::nextPrime(int value) noexcept
{
    auto isPrime = [] (int candidate)
    {
        if (candidate < 2)
            return false;
        for (int divisor = 2; divisor * divisor <= candidate; ++divisor)
            if (candidate % divisor == 0)
                return false;
        return true;
    };

    while (! isPrime(value))
        ++value;
    return value;
}

float DigitalSpectralLayer::activationForSpace(float space) noexcept
{
    return smoothStep((space - 0.30f) / 0.40f);
}

float DigitalSpectralLayer::flutterAmountForChaos(float chaos) noexcept
{
    return smoothStep((chaos - 0.75f) / 0.25f);
}

float DigitalSpectralLayer::flutterRateHz(float chaos) noexcept
{
    return 0.80f + 2.20f * flutterAmountForChaos(chaos);
}

float DigitalSpectralLayer::bandRatio(int band, float chaos) noexcept
{
    constexpr std::array<float, 4> octave { 1.0f, 2.0f, 4.0f, 8.0f };
    constexpr std::array<float, 4> harmonic { 1.0f, 2.0f, 3.0f, 4.0f };
    constexpr std::array<float, 4> inharmonic { 1.0f, 1.618034f, 2.414214f, 3.732051f };
    const auto index = static_cast<size_t>(juce::jlimit(0, 3, band));
    chaos = juce::jlimit(0.0f, 1.0f, chaos);
    if (chaos < 0.45f)
    {
        const float morph = smoothStep(chaos / 0.45f);
        return octave[index] + morph * (harmonic[index] - octave[index]);
    }
    const float morph = smoothStep((chaos - 0.45f) / 0.55f);
    return harmonic[index] + morph * (inharmonic[index] - harmonic[index]);
}

float DigitalSpectralLayer::bandPredelayMs(int channel, int band,
                                            float chaos) noexcept
{
    constexpr std::array<float, 4> leftBase { 3.1f, 8.7f, 17.9f, 31.3f };
    constexpr std::array<float, 4> rightBase { 5.3f, 12.1f, 23.7f, 37.9f };
    constexpr std::array<float, 4> chaosStretch { 1.5f, 4.0f, 9.0f, 18.0f };
    const auto index = static_cast<size_t>(juce::jlimit(0, 3, band));
    const auto& base = channel == 0 ? leftBase : rightBase;
    return base[index] + chaosStretch[index]
                       * juce::jlimit(0.0f, 1.0f, chaos);
}

float DigitalSpectralLayer::targetDecaySeconds(float space) noexcept
{
    space = juce::jlimit(0.0f, 1.0f, space);
    if (space <= 0.90f)
        return 1.8f + 10.2f * smoothStep((space - 0.30f) / 0.60f);
    return 12.0f + 78.0f * smoothStep((space - 0.90f) / 0.10f);
}

float DigitalSpectralLayer::tankFeedbackForSpace(float space) noexcept
{
    // Mean FDN delay is about 70.5 ms. This is the loop gain required for the
    // requested RT60 before frequency-dependent damping is applied.
    return std::pow(0.001f, 0.0705f / targetDecaySeconds(space));
}

float DigitalSpectralLayer::tankModulationDepthSamples(float motion,
                                                        float chaos) noexcept
{
    // Four samples is still only 0.083 ms at 48 kHz, but it is enough to stop
    // the extreme RT60 from resolving into stationary delay-line pitches.
    // Neither user macro is allowed to remove this stability floor.
    const float motionDrift = smoothStep(
        juce::jlimit(0.0f, 1.0f, motion / 0.25f));
    return 4.0f + 1.5f * motionDrift
                + 2.5f * juce::jlimit(0.0f, 1.0f, chaos);
}

float DigitalSpectralLayer::serialDryGainForSpace(float space) noexcept
{
    // Preserve a little more of Layer 2's moving stereo image as the long
    // spectral tank takes over. The lift is deliberately limited to 4%.
    return 0.18f + 0.04f * smoothStep((space - 0.55f) / 0.45f);
}

float DigitalSpectralLayer::stereoPreservationGainForSpace(float space) noexcept
{
    // Preserve only Layer 2's side information as the spectral tank takes
    // over. Mono input is unchanged and the centre does not become louder.
    return 0.160f * smoothStep((space - 0.45f) / 0.55f);
}

void DigitalSpectralLayer::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    constexpr std::array<double, 4> diffuserMs { 2.9, 4.3, 6.7, 9.1 };
    for (size_t stage = 0; stage < diffusers.size(); ++stage)
        diffusers[stage].prepare(nextPrime(juce::roundToInt(
            currentSampleRate * diffuserMs[stage] / 1000.0)));

    constexpr std::array<double, 8> tankMs {
        37.1, 43.7, 53.3, 61.7, 71.9, 83.1, 97.3, 113.9
    };
    for (size_t line = 0; line < tankMemory.size(); ++line)
    {
        tankBaseDelaySamples[line] = nextPrime(juce::roundToInt(
            currentSampleRate * tankMs[line] / 1000.0));
        tankMemory[line].resize(static_cast<size_t>(
            tankBaseDelaySamples[line] + 64), 0.0f);
        tankModulationPhase[line] = static_cast<double>(line) * 0.73;
    }

    juce::dsp::ProcessSpec monoSpec;
    monoSpec.sampleRate = currentSampleRate;
    monoSpec.maximumBlockSize = static_cast<juce::uint32>(maximumBlockSize);
    monoSpec.numChannels = 1;
    for (auto& channel : bandFilters)
        for (auto& filter : channel)
        {
            filter.prepare(monoSpec);
            filter.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        }

    for (auto& channel : bandFrequencies)
        for (size_t band = 0; band < channel.size(); ++band)
        {
            channel[band].reset(currentSampleRate, 0.090);
            channel[band].setCurrentAndTargetValue(165.0f * bandRatio(
                static_cast<int>(band), 0.0f));
        }

    bandDelayMemory.setSize(8,
        juce::roundToInt(currentSampleRate * 0.085) + 8,
        false, true, false);

    limitedInput.setSize(2, maximumBlockSize, false, true, false);
    juce::dsp::ProcessSpec stereoSpec { currentSampleRate,
        static_cast<juce::uint32>(maximumBlockSize), 2 };
    inputLimiter.prepare(stereoSpec);
    inputLimiter.setThreshold(-1.5f);
    inputLimiter.setRelease(70.0f);
    toneFilter.prepare(currentSampleRate);
    spaceAmount.reset(currentSampleRate, 0.120);
    motionAmount.reset(currentSampleRate, 0.100);
    chaosAmount.reset(currentSampleRate, 0.100);
    reset();
}

void DigitalSpectralLayer::reset()
{
    for (auto& diffuser : diffusers)
        diffuser.reset();
    for (auto& line : tankMemory)
        std::fill(line.begin(), line.end(), 0.0f);
    tankPosition.fill(0);
    for (size_t line = 0; line < tankModulationPhase.size(); ++line)
        tankModulationPhase[line] = static_cast<double>(line) * 0.73;
    dampingState.fill(0.0f);
    flutterLowState.fill(0.0f);
    bandDelayMemory.clear();
    bandDelayWritePosition = 0;
    for (auto& channel : bandFilters)
        for (auto& filter : channel)
            filter.reset();
    limitedInput.clear();
    inputLimiter.reset();
    toneFilter.reset();
    spectralPhase = 0.0;
    flutterPhase = 0.0;
    spaceAmount.setCurrentAndTargetValue(0.0f);
    motionAmount.setCurrentAndTargetValue(0.0f);
    chaosAmount.setCurrentAndTargetValue(0.0f);
}

void DigitalSpectralLayer::updateBandTargets(float motion, float chaos)
{
    const float cycle = static_cast<float>(spectralPhase
        / juce::MathConstants<double>::twoPi);
    const int leftStep = static_cast<int>(cycle * leftRootSequence.size())
                       % static_cast<int>(leftRootSequence.size());
    const float rightCycle = std::fmod(cycle + 0.37f, 1.0f);
    const int rightStep = static_cast<int>(rightCycle * rightRootSequence.size())
                        % static_cast<int>(rightRootSequence.size());
    const float movement = smoothStep((motion - 0.25f) / 0.75f);
    const std::array<float, 2> sequenceRoot {
        leftRootSequence[static_cast<size_t>(leftStep)],
        rightRootSequence[static_cast<size_t>(rightStep)]
    };

    for (int channel = 0; channel < 2; ++channel)
    {
        const float microDrift = 1.0f + (1.0f - movement) * 0.012f
            * static_cast<float>(std::sin(spectralPhase
                + (channel == 0 ? 0.0 : juce::MathConstants<double>::pi)));
        const float rootMultiplier = microDrift * (1.0f
            + movement * (sequenceRoot[static_cast<size_t>(channel)] - 1.0f));
        for (int band = 0; band < 4; ++band)
        {
            const float target = juce::jlimit(80.0f, 8500.0f,
                165.0f * rootMultiplier * bandRatio(band, chaos));
            bandFrequencies[static_cast<size_t>(channel)][static_cast<size_t>(band)]
                .setTargetValue(target);
        }
    }
}

float DigitalSpectralLayer::readBandDelay(int channel, int band,
                                           float delaySamples) const noexcept
{
    const int size = bandDelayMemory.getNumSamples();
    float position = static_cast<float>(bandDelayWritePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    while (position >= static_cast<float>(size))
        position -= static_cast<float>(size);
    const int a = static_cast<int>(position);
    const int b = (a + 1) % size;
    const float fraction = position - static_cast<float>(a);
    const int memoryChannel = channel * 4 + band;
    const auto* memory = bandDelayMemory.getReadPointer(memoryChannel);
    return memory[a] + fraction * (memory[b] - memory[a]);
}

std::array<float, 2> DigitalSpectralLayer::processTank(float left, float right,
                                                        float space, float motion,
                                                        float chaos) noexcept
{
    const float modulationDepth = tankModulationDepthSamples(motion, chaos);
    std::array<float, 8> delayed {};
    for (size_t line = 0; line < tankMemory.size(); ++line)
    {
        const auto& memory = tankMemory[line];
        const float modulation = modulationDepth
            * static_cast<float>(std::sin(tankModulationPhase[line]));
        float readPosition = static_cast<float>(tankPosition[line]
            - tankBaseDelaySamples[line]) - modulation;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(memory.size());
        while (readPosition >= static_cast<float>(memory.size()))
            readPosition -= static_cast<float>(memory.size());
        const int a = static_cast<int>(readPosition);
        const int b = (a + 1) % static_cast<int>(memory.size());
        const float fraction = readPosition - static_cast<float>(a);
        delayed[line] = memory[static_cast<size_t>(a)]
                      + fraction * (memory[static_cast<size_t>(b)]
                                  - memory[static_cast<size_t>(a)]);

        // Incommensurate rates keep the Householder loop from periodically
        // returning to one coherent set of delay lengths.
        const double rate = 0.071 + static_cast<double>(line) * 0.0137;
        tankModulationPhase[line] += juce::MathConstants<double>::twoPi
                                   * rate / currentSampleRate;
        if (tankModulationPhase[line] >= juce::MathConstants<double>::twoPi)
            tankModulationPhase[line] -= juce::MathConstants<double>::twoPi;
    }

    float delayedSum = 0.0f;
    for (const float value : delayed)
        delayedSum += value;
    std::array<float, 8> mixed {};
    for (size_t line = 0; line < mixed.size(); ++line)
        mixed[line] = delayed[line] - delayedSum * 0.25f;

    // Calm CHAOS settings are deliberately more damped. Higher CHAOS opens
    // the digital top end while the diffusion flutter keeps it animated.
    const float dampingCutoff = 5800.0f + chaos * 3000.0f;
    const float dampingCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * dampingCutoff
        / static_cast<float>(currentSampleRate));
    const float feedback = tankFeedbackForSpace(space);
    const std::array<float, 8> injection {
        (left + right) * 0.22f,
        (left - right) * 0.22f,
        left * 0.24f,
        right * 0.24f,
        -left * 0.20f,
        -right * 0.20f,
        (left + right * 0.5f) * 0.18f,
        (right - left * 0.5f) * 0.18f
    };

    for (size_t line = 0; line < tankMemory.size(); ++line)
    {
        dampingState[line] += dampingCoefficient
                            * (mixed[line] - dampingState[line]);
        tankMemory[line][static_cast<size_t>(tankPosition[line])] = std::tanh(
            injection[line] + dampingState[line] * feedback);
        tankPosition[line] = (tankPosition[line] + 1)
                           % static_cast<int>(tankMemory[line].size());
    }

    return {
        (delayed[0] + delayed[2] - delayed[4] + delayed[6]
            + (delayed[1] - delayed[5]) * 0.5f) * 0.18f,
        (delayed[1] + delayed[3] - delayed[5] + delayed[7]
            + (delayed[2] - delayed[6]) * 0.5f) * 0.18f
    };
}

void DigitalSpectralLayer::process(const juce::AudioBuffer<float>& layer2Input,
                                    const juce::AudioBuffer<float>& layer1Bypass,
                                    juce::AudioBuffer<float>& layer4Feed,
                                    MacroState macros)
{
    const int samples = layer2Input.getNumSamples();
    jassert(layer2Input.getNumChannels() >= 2);
    jassert(layer1Bypass.getNumChannels() >= 2);
    jassert(layer1Bypass.getNumSamples() >= samples);

    if (limitedInput.getNumSamples() < samples)
        limitedInput.setSize(2, samples, false, false, true);
    layer4Feed.setSize(2, samples, false, false, true);
    for (int channel = 0; channel < 2; ++channel)
        limitedInput.copyFrom(channel, 0, layer2Input, channel, 0, samples);
    juce::dsp::AudioBlock<float> limiterBlock(limitedInput);
    juce::dsp::ProcessContextReplacing<float> limiterContext(limiterBlock);
    inputLimiter.process(limiterContext);

    spaceAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.space));
    motionAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.motion));
    chaosAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.chaos));
    toneFilter.setModes(macros.body, macros.halo);
    updateBandTargets(macros.motion, macros.chaos);

    const float stationaryQTrim = 0.72f + 0.28f
        * smoothStep(juce::jlimit(0.0f, 1.0f, macros.motion / 0.30f));
    const float q = 0.82f + 3.35f
        * smoothStep((macros.chaos - 0.20f) / 0.80f) * stationaryQTrim;
    for (auto& channel : bandFilters)
        for (auto& filter : channel)
            filter.setResonance(q);

    for (int sample = 0; sample < samples; ++sample)
    {
        const float space = spaceAmount.getNextValue();
        const float motion = motionAmount.getNextValue();
        const float chaos = chaosAmount.getNextValue();
        const float activation = activationForSpace(space);
        float left = (limitedInput.getSample(0, sample)
                    + layer1Bypass.getSample(0, sample) * 0.80f) * activation;
        float right = (limitedInput.getSample(1, sample)
                     + layer1Bypass.getSample(1, sample) * 0.80f) * activation;

        const float diffusionCoefficient = 0.54f + 0.16f * space;
        for (auto& diffuser : diffusers)
            diffuser.process(left, right, diffusionCoefficient);

        const float flutterAmount = flutterAmountForChaos(chaos);
        const float flutterDepthDb = 2.35f * flutterAmount;
        const float splitCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * 5000.0f
            / static_cast<float>(currentSampleRate));
        const std::array<float, 2> flutterLfo {
            static_cast<float>(std::sin(flutterPhase)),
            static_cast<float>(std::sin(flutterPhase + juce::degreesToRadians(143.0)))
        };
        std::array<float, 2> diffused { left, right };
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            flutterLowState[index] += splitCoefficient
                                    * (diffused[index] - flutterLowState[index]);
            const float high = diffused[index] - flutterLowState[index];
            const float attenuationDb = -flutterDepthDb
                * (0.5f + 0.5f * flutterLfo[index]);
            diffused[index] = flutterLowState[index]
                            + high * juce::Decibels::decibelsToGain(attenuationDb);
        }
        flutterPhase += juce::MathConstants<double>::twoPi
                      * flutterRateHz(chaos) / currentSampleRate;
        if (flutterPhase >= juce::MathConstants<double>::twoPi)
            flutterPhase -= juce::MathConstants<double>::twoPi;

        const float bandMix = 0.015f + 0.19f * smoothStep(chaos);
        std::array<float, 2> tankInput { diffused[0], diffused[1] };
        for (int channel = 0; channel < 2; ++channel)
        {
            float filteredSum = 0.0f;
            for (int band = 0; band < 4; ++band)
            {
                auto& frequency = bandFrequencies[static_cast<size_t>(channel)]
                                                  [static_cast<size_t>(band)];
                auto& filter = bandFilters[static_cast<size_t>(channel)]
                                          [static_cast<size_t>(band)];
                filter.setCutoffFrequency(frequency.getNextValue());
                const float filtered = filter.processSample(
                    0, diffused[static_cast<size_t>(channel)]);
                const int memoryChannel = channel * 4 + band;
                bandDelayMemory.setSample(memoryChannel, bandDelayWritePosition, filtered);
                const float delaySamples = bandPredelayMs(channel, band, chaos)
                                         * static_cast<float>(currentSampleRate / 1000.0);
                filteredSum += readBandDelay(channel, band, delaySamples);
            }
            tankInput[static_cast<size_t>(channel)] += filteredSum * bandMix * 0.25f;
        }
        bandDelayWritePosition = (bandDelayWritePosition + 1)
                               % bandDelayMemory.getNumSamples();

        const auto tank = processTank(tankInput[0], tankInput[1],
                                      space, motion, chaos);
        const float serialDryGain = serialDryGainForSpace(space);
        const float tankGain = 1.0f - serialDryGain;
        const float inputSide = 0.5f
            * (limitedInput.getSample(0, sample)
             - limitedInput.getSample(1, sample));
        const float sidePreservation = stereoPreservationGainForSpace(space);
        float serialLeft = limitedInput.getSample(0, sample) * serialDryGain
                         + tank[0] * tankGain
                         + inputSide * sidePreservation;
        float serialRight = limitedInput.getSample(1, sample) * serialDryGain
                          + tank[1] * tankGain
                          - inputSide * sidePreservation;
        toneFilter.processStereoSample(serialLeft, serialRight);
        layer4Feed.setSample(0, sample,
            layer2Input.getSample(0, sample) * (1.0f - activation)
            + serialLeft * activation);
        layer4Feed.setSample(1, sample,
            layer2Input.getSample(1, sample) * (1.0f - activation)
            + serialRight * activation);

        const float chaosSpeed = 1.0f + 3.0f * smoothStep((chaos - 0.90f) / 0.10f);
        const float spectralRate = (0.025f + 0.18f * motion) * chaosSpeed;
        spectralPhase += juce::MathConstants<double>::twoPi
                       * spectralRate / currentSampleRate;
        if (spectralPhase >= juce::MathConstants<double>::twoPi)
            spectralPhase -= juce::MathConstants<double>::twoPi;
    }
}
