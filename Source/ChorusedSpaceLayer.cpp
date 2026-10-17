#include "ChorusedSpaceLayer.h"

namespace
{
// Temporary A/B diagnostic: leave Layer 2's ordinary reverb tanks intact but
// remove both regenerative returns. This isolates Layer 3's internal FDN from
// the Layer 2 -> Layer 1 -> Layer 2 serial loop.
constexpr bool diagnosticBypassLayer2Feedback = false;

float smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}
}

float ChorusedSpaceLayer::movementRateHz(float chaos) noexcept
{
    chaos = juce::jlimit(0.0f, 1.0f, chaos);
    if (chaos <= 0.90f)
    {
        const float musical = chaos / 0.90f;
        return 0.055f + 0.345f * std::pow(musical, 1.35f);
    }

    // The final tenth is intentionally a different gear rather than a
    // continuation of the normal range.
    const float danger = (chaos - 0.90f) / 0.10f;
    return 0.40f + 8.60f * danger * danger * danger;
}

float ChorusedSpaceLayer::chorusAmountForChaos(float chaos) noexcept
{
    return smoothStep(juce::jlimit(0.0f, 1.0f, chaos / 0.50f));
}

float ChorusedSpaceLayer::layer1FeedbackGain(float space) noexcept
{
    return 0.010f * smoothStep((space - 0.60f) / 0.40f);
}

float ChorusedSpaceLayer::selfFeedbackGain(float space) noexcept
{
    return 0.016f * smoothStep((space - 0.90f) / 0.10f);
}

float ChorusedSpaceLayer::feedbackHighPassCutoffHz(float space) noexcept
{
    // Close enough to HALO to prevent low-frequency mass from circulating,
    // but still below its 650 Hz corner so HALO remains a distinct mode.
    return 430.0f + 120.0f * juce::jlimit(0.0f, 1.0f, space);
}

float ChorusedSpaceLayer::feedbackLowPassCutoffHz(float space) noexcept
{
    // The return gets darker as its decay grows, keeping upper comb modes
    // from turning the serial loop into a metallic oscillator.
    return 5200.0f - 1800.0f * juce::jlimit(0.0f, 1.0f, space);
}

float ChorusedSpaceLayer::spatialChopperAmount(float motion) noexcept
{
    return smoothStep((motion - 0.50f) / 0.50f);
}

float ChorusedSpaceLayer::spatialChopperRateHz(float motion) noexcept
{
    return 0.10f + 0.80f * spatialChopperAmount(motion);
}

float ChorusedSpaceLayer::motionInputTrim(float motion) noexcept
{
    // The unmodulated tanks are highly correlated and sound slightly louder.
    // Recover unity by the midpoint, before the chopper begins.
    return 0.89f + 0.11f * smoothStep(juce::jlimit(0.0f, 1.0f, motion / 0.50f));
}

float ChorusedSpaceLayer::chorusMotionAmount(float motion) noexcept
{
    // The chorus owns only the first half of MOTION. Above that point its
    // excursion is fixed and the independent spatial chopper takes over.
    return smoothStep(juce::jlimit(0.0f, 1.0f, motion / 0.50f));
}

float ChorusedSpaceLayer::internalDecorrelationDepthMs(float motion,
                                                        float chaos) noexcept
{
    // The 0.28 ms floor is maintenance modulation: enough to keep the serial
    // return from repeatedly exciting exactly the same modes, but too small
    // and slow to read as the user-facing chorus. MOTION and CHAOS add the
    // audible excursion independently above it.
    const float motionShape = chorusMotionAmount(motion);
    const float chaosShape = chorusAmountForChaos(chaos);
    return 0.28f + 0.42f * motionShape
                 + 2.50f * motionShape * chaosShape;
}

void ChorusedSpaceLayer::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    const int chorusSamples = juce::roundToInt(currentSampleRate * 0.065) + 8;
    chorusDelay.setSize(2, chorusSamples, false, true, false);

    // Keeping this delay longer than the largest process block guarantees
    // that the reverb output can be written after block processing without an
    // accidental zero-delay feedback path.
    selfDelaySamples = maximumBlockSize + juce::roundToInt(currentSampleRate * 0.031);
    selfDelay.setSize(2, selfDelaySamples + maximumBlockSize + 8, false, true, false);
    tankInput.setSize(2, maximumBlockSize, false, true, false);
    limitedInput.setSize(2, maximumBlockSize, false, true, false);
    closeTankBuffer.setSize(2, maximumBlockSize, false, true, false);
    largeTankBuffer.setSize(2, maximumBlockSize, false, true, false);
    directBuffer.setSize(2, maximumBlockSize, false, true, false);

    closeTank.setSampleRate(currentSampleRate);
    largeTank.setSampleRate(currentSampleRate);

    juce::Reverb::Parameters close;
    close.roomSize = 0.70f;
    close.damping = 0.49f;
    close.wetLevel = 0.88f;
    close.dryLevel = 0.0f;
    close.width = 0.94f;
    close.freezeMode = 0.0f;
    closeTank.setParameters(close);

    juce::Reverb::Parameters large;
    large.roomSize = 0.94f;
    large.damping = 0.31f;
    large.wetLevel = 0.86f;
    large.dryLevel = 0.0f;
    large.width = 1.0f;
    large.freezeMode = 0.0f;
    largeTank.setParameters(large);
    toneFilter.prepare(currentSampleRate);

    juce::dsp::ProcessSpec limiterSpec;
    limiterSpec.sampleRate = currentSampleRate;
    limiterSpec.maximumBlockSize = static_cast<juce::uint32>(maximumBlockSize);
    limiterSpec.numChannels = 2;
    inputLimiter.prepare(limiterSpec);
    inputLimiter.setThreshold(-1.0f);
    inputLimiter.setRelease(65.0f);

    spaceAmount.reset(currentSampleRate, 0.11);
    motionAmount.reset(currentSampleRate, 0.085);
    chaosAmount.reset(currentSampleRate, 0.085);
    reset();
}

void ChorusedSpaceLayer::reset()
{
    chorusDelay.clear();
    selfDelay.clear();
    tankInput.clear();
    limitedInput.clear();
    closeTankBuffer.clear();
    largeTankBuffer.clear();
    directBuffer.clear();
    closeTank.reset();
    largeTank.reset();
    inputLimiter.reset();
    toneFilter.reset();
    chorusWritePosition = 0;
    selfWritePosition = 0;
    movementPhase = 0.0;
    driftPhase = 0.0;
    chopperPhase = 0.0;
    highPassInput.fill(0.0f);
    highPassOutput.fill(0.0f);
    highPassInput2.fill(0.0f);
    highPassOutput2.fill(0.0f);
    lowPassState.fill(0.0f);
    lowPassState2.fill(0.0f);
    spaceAmount.setCurrentAndTargetValue(0.0f);
    motionAmount.setCurrentAndTargetValue(0.0f);
    chaosAmount.setCurrentAndTargetValue(0.0f);
}

float ChorusedSpaceLayer::readChorusDelay(int channel, float delayInSamples) const noexcept
{
    const int size = chorusDelay.getNumSamples();
    float position = static_cast<float>(chorusWritePosition) - delayInSamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    while (position >= static_cast<float>(size))
        position -= static_cast<float>(size);

    const int a = static_cast<int>(position);
    const int b = (a + 1) % size;
    const float fraction = position - static_cast<float>(a);
    const auto* data = chorusDelay.getReadPointer(channel);
    return data[a] + fraction * (data[b] - data[a]);
}

float ChorusedSpaceLayer::readSelfDelay(int channel) const noexcept
{
    const int size = selfDelay.getNumSamples();
    int position = selfWritePosition - selfDelaySamples;
    while (position < 0)
        position += size;
    return selfDelay.getSample(channel, position);
}

void ChorusedSpaceLayer::process(const juce::AudioBuffer<float>& input,
                                 juce::AudioBuffer<float>& stage3Feed,
                                 juce::AudioBuffer<float>& feedbackToLayer1,
                                 MacroState macros)
{
    const int samples = input.getNumSamples();
    jassert(input.getNumChannels() >= 2);

    if (tankInput.getNumSamples() < samples)
    {
        tankInput.setSize(2, samples, false, false, true);
        limitedInput.setSize(2, samples, false, false, true);
        closeTankBuffer.setSize(2, samples, false, false, true);
        largeTankBuffer.setSize(2, samples, false, false, true);
        directBuffer.setSize(2, samples, false, false, true);
    }

    stage3Feed.setSize(2, samples, false, false, true);
    feedbackToLayer1.setSize(2, samples, false, false, true);
    spaceAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.space));
    motionAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.motion));
    chaosAmount.setTargetValue(juce::jlimit(0.0f, 1.0f, macros.chaos));
    toneFilter.setModes(macros.body, macros.halo);

    for (int channel = 0; channel < 2; ++channel)
        limitedInput.copyFrom(channel, 0, input, channel, 0, samples);
    juce::dsp::AudioBlock<float> limiterBlock(limitedInput);
    juce::dsp::ProcessContextReplacing<float> limiterContext(limiterBlock);
    inputLimiter.process(limiterContext);

    const float samplesPerMillisecond = static_cast<float>(currentSampleRate / 1000.0);
    const int selfBlockStart = selfWritePosition;

    for (int sample = 0; sample < samples; ++sample)
    {
        const float space = spaceAmount.getNextValue();
        const float motion = motionAmount.getNextValue();
        const float chaos = chaosAmount.getNextValue();
        const float rate = movementRateHz(chaos);
        const float depthMs = internalDecorrelationDepthMs(motion, chaos);
        const float inputTrim = motionInputTrim(motion);

        const float leftMovement = static_cast<float>(std::sin(movementPhase));
        // 177 degrees is almost opposite, while the slow phase wander keeps
        // the two trajectories from meeting in the same place every cycle.
        const double rightPhase = movementPhase
                                + juce::degreesToRadians(177.0)
                                + 0.20 * std::sin(driftPhase);
        const float rightMovement = static_cast<float>(std::sin(rightPhase));
        const std::array<float, 2> movement { leftMovement, rightMovement };

        movementPhase += juce::MathConstants<double>::twoPi * rate / currentSampleRate;
        driftPhase += juce::MathConstants<double>::twoPi * 0.017 / currentSampleRate;
        if (movementPhase >= juce::MathConstants<double>::twoPi)
            movementPhase -= juce::MathConstants<double>::twoPi;
        if (driftPhase >= juce::MathConstants<double>::twoPi)
            driftPhase -= juce::MathConstants<double>::twoPi;

        for (int channel = 0; channel < 2; ++channel)
        {
            const float source = limitedInput.getSample(channel, sample) * inputTrim;
            const float delayedSelf = diagnosticBypassLayer2Feedback
                ? 0.0f
                : readSelfDelay(channel) * selfFeedbackGain(space);
            const float boundedInput = std::tanh(source + delayedSelf);
            chorusDelay.setSample(channel, chorusWritePosition, boundedInput);
            directBuffer.setSample(channel, sample, source);

            const float delayMs = 13.2f + movement[static_cast<size_t>(channel)] * depthMs;
            const float modulated = readChorusDelay(channel,
                juce::jmax(1.0f, delayMs * samplesPerMillisecond));

            // A quiet decorrelated pair of taps supplies the gentle diffusion
            // lift as SPACE grows, before either tank begins regenerating it.
            const int opposite = 1 - channel;
            const float diffuseA = readChorusDelay(channel, 5.3f * samplesPerMillisecond);
            const float diffuseB = readChorusDelay(opposite, 8.7f * samplesPerMillisecond);
            const float diffusion = space * (diffuseA - diffuseB) * 0.105f;
            tankInput.setSample(channel, sample, modulated + diffusion);
        }

        chorusWritePosition = (chorusWritePosition + 1) % chorusDelay.getNumSamples();
        selfWritePosition = (selfWritePosition + 1) % selfDelay.getNumSamples();
    }

    for (int channel = 0; channel < 2; ++channel)
    {
        closeTankBuffer.copyFrom(channel, 0, tankInput, channel, 0, samples);
        largeTankBuffer.copyFrom(channel, 0, tankInput, channel, 0, samples);
    }
    closeTank.processStereo(closeTankBuffer.getWritePointer(0),
                            closeTankBuffer.getWritePointer(1), samples);
    largeTank.processStereo(largeTankBuffer.getWritePointer(0),
                            largeTankBuffer.getWritePointer(1), samples);

    int selfWrite = selfBlockStart;
    for (int sample = 0; sample < samples; ++sample)
    {
        const float space = juce::jlimit(0.0f, 1.0f,
            spaceAmount.getCurrentValue());
        const float closeGain = std::cos(space * juce::MathConstants<float>::halfPi);
        const float largeGain = std::sin(space * juce::MathConstants<float>::halfPi);
        const float returnGain = layer1FeedbackGain(space);
        const float cutoff = feedbackHighPassCutoffHz(space);
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoff);
        const float alpha = rc / (rc + 1.0f / static_cast<float>(currentSampleRate));
        const float lowPassCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi
            * feedbackLowPassCutoffHz(space)
            / static_cast<float>(currentSampleRate));

        float left = (closeTankBuffer.getSample(0, sample) * closeGain
                    + largeTankBuffer.getSample(0, sample) * largeGain) * 0.78f
                   + directBuffer.getSample(0, sample) * 0.14f;
        float right = (closeTankBuffer.getSample(1, sample) * closeGain
                     + largeTankBuffer.getSample(1, sample) * largeGain) * 0.78f
                    + directBuffer.getSample(1, sample) * 0.14f;
        toneFilter.processStereoSample(left, right);
        const std::array<float, 2> feedbackSignal { left, right };

        const float chopperAmount = spatialChopperAmount(
            motionAmount.getCurrentValue());
        const float chopperRate = spatialChopperRateHz(
            motionAmount.getCurrentValue());
        const float rawChopper = static_cast<float>(std::sin(chopperPhase));
        const float hardness = 1.0f + 9.0f * chopperAmount * chopperAmount;
        const float shapedChopper = std::tanh(rawChopper * hardness)
                                   / std::tanh(hardness);
        const float pan = shapedChopper * chopperAmount;
        const float panAngle = (pan + 1.0f)
                             * juce::MathConstants<float>::pi * 0.25f;
        const float leftPanGain = std::cos(panAngle) * std::sqrt(2.0f);
        const float rightPanGain = std::sin(panAngle) * std::sqrt(2.0f);
        const float chopperTrim = 1.0f - 0.06f * chopperAmount;
        chopperPhase += juce::MathConstants<double>::twoPi * chopperRate
                      / currentSampleRate;
        if (chopperPhase >= juce::MathConstants<double>::twoPi)
            chopperPhase -= juce::MathConstants<double>::twoPi;

        stage3Feed.setSample(0, sample, left * leftPanGain * chopperTrim);
        stage3Feed.setSample(1, sample, right * rightPanGain * chopperTrim);

        for (int channel = 0; channel < 2; ++channel)
        {
            const float filtered = feedbackSignal[static_cast<size_t>(channel)];

            const auto index = static_cast<size_t>(channel);
            const float hp1 = alpha * (highPassOutput[index]
                                      + filtered
                                      - highPassInput[index]);
            highPassInput[index] = filtered;
            highPassOutput[index] = hp1;
            const float hp2 = alpha * (highPassOutput2[index]
                                      + hp1
                                      - highPassInput2[index]);
            highPassInput2[index] = hp1;
            highPassOutput2[index] = hp2;
            lowPassState[index] += lowPassCoefficient
                                 * (hp2 - lowPassState[index]);
            lowPassState2[index] += lowPassCoefficient
                                  * (lowPassState[index] - lowPassState2[index]);
            const float bandLimited = lowPassState2[index];
            // Both the return to Layer 1 and Layer 2's own delayed return use
            // the same permanently band-limited, 12 dB/octave signal.
            selfDelay.setSample(channel, selfWrite,
                diagnosticBypassLayer2Feedback ? 0.0f : std::tanh(bandLimited));
            feedbackToLayer1.setSample(channel, sample,
                diagnosticBypassLayer2Feedback
                    ? 0.0f
                    : std::tanh(bandLimited * returnGain));
        }
        selfWrite = (selfWrite + 1) % selfDelay.getNumSamples();
    }
}
