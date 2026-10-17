#include "WarmRoomLayer.h"

namespace
{
constexpr float layer3TapGain = 0.16f;
constexpr float layer4TapGain = 0.065f;
constexpr float feedbackInputCeiling = 0.72f;
}

void WarmRoomLayer::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    const int maximumDelaySamples = juce::roundToInt(currentSampleRate * 0.080) + 8;
    delayMemory.setSize(2, maximumDelaySamples, false, true, false);
    networkInput.setSize(2, maximumBlockSize, false, true, false);
    earlyBuffer.setSize(2, maximumBlockSize, false, true, false);
    plateBuffer.setSize(2, maximumBlockSize, false, true, false);
    roomBuffer.setSize(2, maximumBlockSize, false, true, false);

    plateReverb.setSampleRate(currentSampleRate);
    roomReverb.setSampleRate(currentSampleRate);

    juce::Reverb::Parameters plate;
    plate.roomSize = 0.43f;
    plate.damping = 0.29f;
    plate.wetLevel = 0.92f;
    plate.dryLevel = 0.0f;
    plate.width = 0.88f;
    plate.freezeMode = 0.0f;
    plateReverb.setParameters(plate);

    juce::Reverb::Parameters room;
    room.roomSize = 0.76f;
    room.damping = 0.47f;
    room.wetLevel = 0.92f;
    room.dryLevel = 0.0f;
    room.width = 0.82f;
    room.freezeMode = 0.0f;
    roomReverb.setParameters(room);
    toneFilter.prepare(currentSampleRate);

    spaceMorph.reset(currentSampleRate, 0.085);
    motionAmount.reset(currentSampleRate, 0.070);
    chaosAmount.reset(currentSampleRate, 0.060);
    reset();
}

void WarmRoomLayer::reset()
{
    delayMemory.clear();
    networkInput.clear();
    earlyBuffer.clear();
    plateBuffer.clear();
    roomBuffer.clear();
    plateReverb.reset();
    roomReverb.reset();
    toneFilter.reset();
    writePosition = 0;
    modulationPhase = 0.0;
    spaceMorph.setCurrentAndTargetValue(0.0f);
    motionAmount.setCurrentAndTargetValue(0.0f);
    chaosAmount.setCurrentAndTargetValue(0.0f);
}

float WarmRoomLayer::readDelay(int channel, float delayInSamples) const noexcept
{
    const int memorySize = delayMemory.getNumSamples();
    float readPosition = static_cast<float>(writePosition) - delayInSamples;
    while (readPosition < 0.0f)
        readPosition += static_cast<float>(memorySize);
    while (readPosition >= static_cast<float>(memorySize))
        readPosition -= static_cast<float>(memorySize);

    const int indexA = static_cast<int>(readPosition);
    const int indexB = (indexA + 1) % memorySize;
    const float fraction = readPosition - static_cast<float>(indexA);
    const auto* memory = delayMemory.getReadPointer(channel);
    return memory[indexA] + fraction * (memory[indexB] - memory[indexA]);
}

void WarmRoomLayer::process(const juce::AudioBuffer<float>& input,
                            const juce::AudioBuffer<float>& feedbackFromLater,
                            juce::AudioBuffer<float>& stage2Feed,
                            juce::AudioBuffer<float>& stage3Bypass,
                            juce::AudioBuffer<float>& stage4Bypass,
                            MacroState macros)
{
    const int samples = input.getNumSamples();
    jassert(input.getNumChannels() >= 2);
    jassert(feedbackFromLater.getNumChannels() >= 2);
    jassert(feedbackFromLater.getNumSamples() >= samples);

    if (networkInput.getNumSamples() < samples)
    {
        networkInput.setSize(2, samples, false, false, true);
        earlyBuffer.setSize(2, samples, false, false, true);
        plateBuffer.setSize(2, samples, false, false, true);
        roomBuffer.setSize(2, samples, false, false, true);
    }

    stage2Feed.setSize(2, samples, false, false, true);
    stage3Bypass.setSize(2, samples, false, false, true);
    stage4Bypass.setSize(2, samples, false, false, true);

    // SPACE completes this layer's entire plate-to-room movement at 40%.
    spaceMorph.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.space / 0.40f));
    motionAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.motion));
    chaosAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.chaos));
    toneFilter.setModes(macros.body, macros.halo);

    const float sampleRateScale = static_cast<float>(currentSampleRate / 1000.0);
    constexpr double lfoFrequency = 0.115;
    const double phaseIncrement = juce::MathConstants<double>::twoPi * lfoFrequency
                                / currentSampleRate;

    for (int sample = 0; sample < samples; ++sample)
    {
        const float motion = motionAmount.getNextValue();
        const float chaos = chaosAmount.getNextValue();
        const float lfo = static_cast<float>(std::sin(modulationPhase));
        modulationPhase += phaseIncrement;
        if (modulationPhase >= juce::MathConstants<double>::twoPi)
            modulationPhase -= juce::MathConstants<double>::twoPi;

        for (int channel = 0; channel < 2; ++channel)
        {
            // Later reverbs may feed this input, but the warm room never reads
            // its own output. Soft limiting makes the future matrix safe at the
            // boundary without changing normal-level material.
            const float source = input.getSample(channel, sample);
            const float laterFeedback = std::tanh(
                feedbackFromLater.getSample(channel, sample) * feedbackInputCeiling);
            const float intoRoom = source + laterFeedback;
            delayMemory.setSample(channel, writePosition, intoRoom);

            const float stereoPolarity = channel == 0 ? 1.0f : -1.0f;
            const float basePredelayMs = 4.6f + motion * 1.7f;
            const float modulationMs = motion * 0.72f * lfo * stereoPolarity;
            const float predelayed = readDelay(channel,
                juce::jmax(1.0f, (basePredelayMs + modulationMs) * sampleRateScale));

            const int opposite = 1 - channel;
            const float reflectionA = readDelay(channel, 8.1f * sampleRateScale);
            const float reflectionB = readDelay(opposite, 13.7f * sampleRateScale);
            const float reflectionC = readDelay(channel, 21.4f * sampleRateScale);
            const float early = chaos * (reflectionA * 0.095f
                                        - reflectionB * 0.070f
                                        + reflectionC * 0.052f);
            earlyBuffer.setSample(channel, sample, early);
            networkInput.setSample(channel, sample, predelayed + early);
        }

        writePosition = (writePosition + 1) % delayMemory.getNumSamples();
    }

    plateBuffer.copyFrom(0, 0, networkInput, 0, 0, samples);
    plateBuffer.copyFrom(1, 0, networkInput, 1, 0, samples);
    roomBuffer.copyFrom(0, 0, networkInput, 0, 0, samples);
    roomBuffer.copyFrom(1, 0, networkInput, 1, 0, samples);
    plateReverb.processStereo(plateBuffer.getWritePointer(0), plateBuffer.getWritePointer(1), samples);
    roomReverb.processStereo(roomBuffer.getWritePointer(0), roomBuffer.getWritePointer(1), samples);

    for (int sample = 0; sample < samples; ++sample)
    {
        const float roomAmount = spaceMorph.getNextValue();
        // Equal-power interpolation keeps the perceived mass stable through
        // the algorithm transition rather than dipping at the midpoint.
        const float plateGain = std::cos(roomAmount * juce::MathConstants<float>::halfPi);
        const float roomGain = std::sin(roomAmount * juce::MathConstants<float>::halfPi);
        float left = plateBuffer.getSample(0, sample) * plateGain
                   + roomBuffer.getSample(0, sample) * roomGain
                   + earlyBuffer.getSample(0, sample) * 0.62f;
        float right = plateBuffer.getSample(1, sample) * plateGain
                    + roomBuffer.getSample(1, sample) * roomGain
                    + earlyBuffer.getSample(1, sample) * 0.62f;
        toneFilter.processStereoSample(left, right);
        stage2Feed.setSample(0, sample, left);
        stage2Feed.setSample(1, sample, right);
        stage3Bypass.setSample(0, sample, left * layer3TapGain);
        stage3Bypass.setSample(1, sample, right * layer3TapGain);
        stage4Bypass.setSample(0, sample, left * layer4TapGain);
        stage4Bypass.setSample(1, sample, right * layer4TapGain);
    }
}

