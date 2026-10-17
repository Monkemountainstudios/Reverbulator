#include <JuceHeader.h>
#include "ChorusedSpaceLayer.h"
#include "DigitalSpectralLayer.h"
#include "GravityBloomLayer.h"
#include "SerialRouting.h"
#include "StageToneFilter.h"
#include "TotalSpectralReturn.h"
#include "WarmRoomLayer.h"
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int totalSamples = 48000;

struct RenderResult
{
    juce::AudioBuffer<float> output { 2, totalSamples };
    juce::AudioBuffer<float> skip2 { 2, totalSamples };
    juce::AudioBuffer<float> skip23 { 2, totalSamples };
};

RenderResult render(float space, float motion, float chaos)
{
    WarmRoomLayer layer;
    layer.prepare(sampleRate, blockSize);
    RenderResult result;
    juce::AudioBuffer<float> input(2, blockSize);
    juce::AudioBuffer<float> feedback(2, blockSize);
    juce::AudioBuffer<float> out, skip2, skip23;

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, totalSamples - offset);
        input.setSize(2, count, false, false, true);
        feedback.setSize(2, count, false, false, true);
        input.clear();
        feedback.clear();
        if (offset == 0)
        {
            input.setSample(0, 0, 1.0f);
            input.setSample(1, 0, 1.0f);
        }

        layer.process(input, feedback, out, skip2, skip23, { space, motion, chaos });
        for (int channel = 0; channel < 2; ++channel)
        {
            result.output.copyFrom(channel, offset, out, channel, 0, count);
            result.skip2.copyFrom(channel, offset, skip2, channel, 0, count);
            result.skip23.copyFrom(channel, offset, skip23, channel, 0, count);
        }
    }
    return result;
}

double energy(const juce::AudioBuffer<float>& buffer, int start = 0, int length = totalSamples)
{
    double sum = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = start; sample < start + length; ++sample)
        {
            const double value = buffer.getSample(channel, sample);
            sum += value * value;
        }
    return sum;
}

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

struct Layer2Render
{
    juce::AudioBuffer<float> output { 2, totalSamples };
    juce::AudioBuffer<float> returnToLayer1 { 2, totalSamples };
};

Layer2Render renderLayer2(float space, float motion, float chaos)
{
    ChorusedSpaceLayer layer;
    layer.prepare(sampleRate, blockSize);
    Layer2Render result;
    juce::AudioBuffer<float> input(2, blockSize);
    juce::AudioBuffer<float> output, feedback;

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, totalSamples - offset);
        input.setSize(2, count, false, false, true);
        input.clear();
        if (offset == 0)
        {
            input.setSample(0, 0, 1.0f);
            input.setSample(1, 0, 1.0f);
        }

        layer.process(input, output, feedback, { space, motion, chaos });
        for (int channel = 0; channel < 2; ++channel)
        {
            result.output.copyFrom(channel, offset, output, channel, 0, count);
            result.returnToLayer1.copyFrom(channel, offset, feedback, channel, 0, count);
        }
    }
    return result;
}

double filteredSineRms(float frequency, bool body, bool halo)
{
    StageToneFilter filter;
    filter.prepare(sampleRate);
    filter.setModes(body, halo);
    double sumSquares = 0.0;
    int measured = 0;
    constexpr int sineSamples = static_cast<int>(sampleRate);
    for (int sample = 0; sample < sineSamples; ++sample)
    {
        const float value = std::sin(juce::MathConstants<double>::twoPi
                                   * frequency * sample / sampleRate);
        float left = value;
        float right = value;
        filter.processStereoSample(left, right);
        if (sample >= sineSamples / 2)
        {
            sumSquares += static_cast<double>(left) * left;
            ++measured;
        }
    }
    return std::sqrt(sumSquares / static_cast<double>(measured));
}

}

int main()
{
    const auto plate = render(0.0f, 0.0f, 0.0f);
    const auto roomAtForty = render(0.40f, 0.0f, 0.0f);
    const auto roomAtMaximum = render(1.0f, 0.0f, 0.0f);
    const auto calmMidway = render(0.20f, 0.0f, 0.0f);
    const auto chaotic = render(0.20f, 0.0f, 1.0f);

    if (! allFinite(plate.output) || energy(plate.output) <= 1.0e-8)
    {
        std::cerr << "Warm-room impulse response is invalid\n";
        return 1;
    }

    double clampDifference = 0.0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < totalSamples; ++sample)
            clampDifference += std::abs(roomAtForty.output.getSample(channel, sample)
                                       - roomAtMaximum.output.getSample(channel, sample));
    if (clampDifference > 1.0e-4)
    {
        std::cerr << "SPACE continues changing Layer 1 above 40%\n";
        return 2;
    }

    const int earlyWindow = static_cast<int>(sampleRate * 0.030);
    if (energy(chaotic.output, 0, earlyWindow) <= energy(calmMidway.output, 0, earlyWindow))
    {
        std::cerr << "CHAOS did not add early-reflection energy\n";
        return 3;
    }

    const double mainEnergy = energy(roomAtForty.output);
    const double skip2Ratio = std::sqrt(energy(roomAtForty.skip2) / mainEnergy);
    const double skip23Ratio = std::sqrt(energy(roomAtForty.skip23) / mainEnergy);
    if (std::abs(skip2Ratio - 0.16) > 0.002 || std::abs(skip23Ratio - 0.065) > 0.002)
    {
        std::cerr << "Layer bypass tap gains are incorrect\n";
        return 4;
    }

    const auto layer2BelowReturn = renderLayer2(0.59f, 1.0f, 0.40f);
    const auto layer2Large = renderLayer2(1.0f, 1.0f, 0.40f);
    if (! allFinite(layer2Large.output) || energy(layer2Large.output) <= 1.0e-8)
    {
        std::cerr << "Chorused-space impulse response is invalid\n";
        return 5;
    }

    if (energy(layer2BelowReturn.returnToLayer1) > 1.0e-12
        || energy(layer2Large.returnToLayer1) <= 1.0e-10)
    {
        std::cerr << "Layer 2 to Layer 1 feedback threshold is incorrect\n";
        return 6;
    }

    if (std::abs(ChorusedSpaceLayer::chorusAmountForChaos(0.50f)
               - ChorusedSpaceLayer::chorusAmountForChaos(1.00f)) > 1.0e-6f)
    {
        std::cerr << "CHAOS chorus amount does not stop growing at 50%\n";
        return 7;
    }

    const float normalSpeedSpan = ChorusedSpaceLayer::movementRateHz(0.90f)
                                - ChorusedSpaceLayer::movementRateHz(0.0f);
    const float finalTenPercentSpan = ChorusedSpaceLayer::movementRateHz(1.0f)
                                   - ChorusedSpaceLayer::movementRateHz(0.90f);
    if (finalTenPercentSpan < normalSpeedSpan * 10.0f)
    {
        std::cerr << "CHAOS final 10% is not sufficiently ludicrous\n";
        return 8;
    }

    if (ChorusedSpaceLayer::layer1FeedbackGain(0.60f) != 0.0f
        || ChorusedSpaceLayer::layer1FeedbackGain(1.0f) > 0.011f)
    {
        std::cerr << "Layer 2 to Layer 1 feedback ceiling is too high\n";
        return 9;
    }

    if (ChorusedSpaceLayer::feedbackHighPassCutoffHz(0.30f) < 450.0f
        || ChorusedSpaceLayer::feedbackHighPassCutoffHz(1.0f) > 600.0f
        || ChorusedSpaceLayer::feedbackLowPassCutoffHz(0.0f) < 5000.0f
        || ChorusedSpaceLayer::feedbackLowPassCutoffHz(1.0f) > 3600.0f)
    {
        std::cerr << "Permanent feedback low cut is outside its safe range\n";
        return 10;
    }

    if (ChorusedSpaceLayer::spatialChopperAmount(0.50f) != 0.0f
        || ChorusedSpaceLayer::spatialChopperAmount(1.0f) < 0.999f
        || std::abs(ChorusedSpaceLayer::spatialChopperRateHz(0.50f) - 0.10f) > 1.0e-6f
        || std::abs(ChorusedSpaceLayer::spatialChopperRateHz(1.0f) - 0.90f) > 1.0e-6f)
    {
        std::cerr << "Upper-half MOTION chopper mapping is incorrect\n";
        return 11;
    }

    if (ChorusedSpaceLayer::motionInputTrim(0.0f) > 0.90f
        || std::abs(ChorusedSpaceLayer::motionInputTrim(0.50f) - 1.0f) > 1.0e-6f)
    {
        std::cerr << "MOTION loudness compensation is incorrect\n";
        return 12;
    }

    if (ChorusedSpaceLayer::chorusMotionAmount(0.0f) != 0.0f
        || ChorusedSpaceLayer::chorusMotionAmount(0.50f) < 0.999f
        || std::abs(ChorusedSpaceLayer::chorusMotionAmount(0.50f)
                  - ChorusedSpaceLayer::chorusMotionAmount(1.0f)) > 1.0e-6f)
    {
        std::cerr << "Chorus depth continues changing above 50% MOTION\n";
        return 13;
    }

    if (ChorusedSpaceLayer::selfFeedbackGain(0.90f) != 0.0f
        || ChorusedSpaceLayer::selfFeedbackGain(1.0f) < 0.014f
        || ChorusedSpaceLayer::selfFeedbackGain(1.0f) > 0.018f)
    {
        std::cerr << "Layer 2 self-feedback threshold is incorrect\n";
        return 14;
    }

    if (std::abs(SerialRouting::layer1LiveInputGain(0.40f) - 1.0f) > 1.0e-6f
        || std::abs(SerialRouting::layer1LiveInputGain(1.0f) - 0.50f) > 1.0e-6f
        || std::abs(SerialRouting::layer2DirectInputGain(1.0f) - 0.58f) > 1.0e-6f
        || std::abs(DigitalSpectralLayer::serialDryGainForSpace(0.55f) - 0.18f) > 1.0e-6f
        || std::abs(DigitalSpectralLayer::serialDryGainForSpace(1.0f) - 0.22f) > 1.0e-6f)
    {
        std::cerr << "Layer 1 to Layer 2 live-input handoff is incorrect\n";
        return 15;
    }

    if (SerialRouting::networkActivationForSpace(0.0f) != 0.0f
        || SerialRouting::networkActivationForSpace(0.01f) <= 0.0f
        || SerialRouting::networkActivationForSpace(0.01f) >= 1.0f
        || SerialRouting::networkActivationForSpace(0.05f) < 0.999f)
    {
        std::cerr << "SPACE wet-network ignition mapping is incorrect\n";
        return 41;
    }

    if (DigitalSpectralLayer::stereoPreservationGainForSpace(0.45f) != 0.0f
        || DigitalSpectralLayer::stereoPreservationGainForSpace(1.0f) < 0.159f
        || DigitalSpectralLayer::stereoPreservationGainForSpace(1.0f) > 0.161f)
    {
        std::cerr << "Layer 3 stereo preservation mapping is incorrect\n";
        return 36;
    }

    const double unfilteredHigh = filteredSineRms(15000.0f, false, false);
    const double bodyHigh = filteredSineRms(15000.0f, true, false);
    const double unfilteredLow = filteredSineRms(100.0f, false, false);
    const double haloLow = filteredSineRms(100.0f, false, true);
    const double unfilteredPresence = filteredSineRms(4200.0f, false, false);
    const double haloPresence = filteredSineRms(4200.0f, false, true);
    if (bodyHigh >= unfilteredHigh * 0.65)
    {
        std::cerr << "BODY high-cut is not attenuating the top end\n";
        return 16;
    }
    if (haloLow >= unfilteredLow * 0.25)
    {
        std::cerr << "HALO low-cut is not attenuating the low end\n";
        return 17;
    }
    if (haloPresence <= unfilteredPresence * 1.10)
    {
        std::cerr << "HALO presence resonance is missing\n";
        return 30;
    }

    // Exercise the real, block-delayed Layer 2 -> Layer 1 -> Layer 2 loop at
    // the most aggressive macro settings for long enough to expose a runaway.
    WarmRoomLayer loopLayer1;
    ChorusedSpaceLayer loopLayer2;
    loopLayer1.prepare(sampleRate, blockSize);
    loopLayer2.prepare(sampleRate, blockSize);
    juce::AudioBuffer<float> loopInput(2, blockSize);
    juce::AudioBuffer<float> loopFeedback(2, blockSize);
    juce::AudioBuffer<float> nextLoopFeedback;
    juce::AudioBuffer<float> loopLayer1Out, loopSkip2, loopSkip23, loopLayer2Out;
    loopFeedback.clear();
    float peak = 0.0f;
    constexpr int stressSamples = static_cast<int>(sampleRate * 6.0);
    for (int offset = 0; offset < stressSamples; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, stressSamples - offset);
        loopInput.setSize(2, count, false, false, true);
        loopInput.clear();
        if (offset == 0)
        {
            loopInput.setSample(0, 0, 1.0f);
            loopInput.setSample(1, 0, 1.0f);
        }

        loopLayer1.process(loopInput, loopFeedback,
                           loopLayer1Out, loopSkip2, loopSkip23,
                           { 1.0f, 1.0f, 1.0f });
        loopLayer2.process(loopLayer1Out, loopLayer2Out,
                           nextLoopFeedback, { 1.0f, 1.0f, 1.0f });
        loopFeedback.makeCopyOf(nextLoopFeedback, true);

        if (! allFinite(loopLayer2Out))
        {
            std::cerr << "Maximum-feedback network produced non-finite samples\n";
            return 18;
        }
        for (int channel = 0; channel < 2; ++channel)
            peak = juce::jmax(peak, loopLayer2Out.getMagnitude(channel, 0, count));
    }

    if (peak > 4.0f)
    {
        std::cerr << "Maximum-feedback network ran away\n";
        return 19;
    }

    if (DigitalSpectralLayer::activationForSpace(0.30f) != 0.0f
        || DigitalSpectralLayer::activationForSpace(0.70f) < 0.999f
        || DigitalSpectralLayer::flutterAmountForChaos(0.75f) != 0.0f
        || DigitalSpectralLayer::flutterAmountForChaos(1.0f) < 0.999f
        || std::abs(DigitalSpectralLayer::flutterRateHz(1.0f) - 3.0f) > 1.0e-6f)
    {
        std::cerr << "Layer 3 macro boundaries are incorrect\n";
        return 20;
    }

    if (DigitalSpectralLayer::bandPredelayMs(0, 3, 1.0f)
            <= DigitalSpectralLayer::bandPredelayMs(0, 2, 1.0f)
        || DigitalSpectralLayer::bandPredelayMs(0, 3, 1.0f)
            == DigitalSpectralLayer::bandPredelayMs(1, 3, 1.0f)
        || std::abs(DigitalSpectralLayer::targetDecaySeconds(0.90f) - 12.0f) > 0.01f
        || std::abs(DigitalSpectralLayer::targetDecaySeconds(1.0f) - 90.0f) > 0.01f
        || DigitalSpectralLayer::tankFeedbackForSpace(1.0f) < 0.99f
        || DigitalSpectralLayer::tankFeedbackForSpace(1.0f) >= 0.995f)
    {
        std::cerr << "Layer 3 predelay or long-decay mapping is incorrect\n";
        return 21;
    }

    if (SerialRouting::vectorSpectralTailGain(0.90f) != 0.0f
        || SerialRouting::vectorSpectralTailGain(1.0f) < 0.139f
        || GravityBloomLayer::layer3PreservationGain(1.0f) < 0.379f)
    {
        std::cerr << "Event-horizon tail preservation mapping is incorrect\n";
        return 40;
    }

    constexpr float minimumTankDrift = 4.0f;
    if (DigitalSpectralLayer::tankModulationDepthSamples(0.0f, 0.0f)
            < minimumTankDrift
        || DigitalSpectralLayer::tankModulationDepthSamples(0.0f, 1.0f)
            < minimumTankDrift
        || DigitalSpectralLayer::tankModulationDepthSamples(1.0f, 0.0f)
            < minimumTankDrift
        || ChorusedSpaceLayer::internalDecorrelationDepthMs(0.0f, 0.0f)
            < 0.28f)
    {
        std::cerr << "Internal anti-modal modulation floor is missing\n";
        return 26;
    }

    DigitalSpectralLayer layer3;
    layer3.prepare(sampleRate, blockSize);
    juce::AudioBuffer<float> layer3Input(2, blockSize);
    juce::AudioBuffer<float> layer3Bypass(2, blockSize);
    juce::AudioBuffer<float> layer3Output;
    layer3Input.clear();
    layer3Bypass.clear();
    for (int offset = 0; offset < static_cast<int>(sampleRate); offset += blockSize)
        layer3.process(layer3Input, layer3Bypass, layer3Output,
                       { 1.0f, 1.0f, 1.0f, false, false });
    float layer3Peak = 0.0f;
    double layer3TailEnergy = 0.0;
    for (int offset = 0; offset < stressSamples; offset += blockSize)
    {
        layer3Input.clear();
        layer3Bypass.clear();
        if (offset == 0)
        {
            layer3Input.setSample(0, 0, 1.0f);
            layer3Input.setSample(1, 0, 1.0f);
        }
        layer3.process(layer3Input, layer3Bypass, layer3Output,
                       { 1.0f, 1.0f, 1.0f, false, false });
        if (! allFinite(layer3Output))
        {
            std::cerr << "Layer 3 produced non-finite samples\n";
            return 22;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            layer3Peak = juce::jmax(layer3Peak,
                layer3Output.getMagnitude(channel, 0, blockSize));
            if (offset > static_cast<int>(sampleRate * 0.10))
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const double value = layer3Output.getSample(channel, sample);
                    layer3TailEnergy += value * value;
                }
        }
    }
    if (layer3Peak <= 1.0e-6f || layer3Peak > 4.0f
        || layer3TailEnergy <= 1.0e-8)
    {
        std::cerr << "Layer 3 impulse response is invalid\n";
        return 23;
    }

    // Regression for the previously troublesome corner: maximum decay with
    // both animation macros parked at zero. The late window may decay slowly,
    // but it must not gather energy after the excitation has stopped.
    layer3.reset();
    for (int offset = 0; offset < static_cast<int>(sampleRate); offset += blockSize)
    {
        layer3Input.clear();
        layer3Bypass.clear();
        layer3.process(layer3Input, layer3Bypass, layer3Output,
                       { 1.0f, 0.0f, 0.0f, false, false });
    }
    double calmEarlyEnergy = 0.0;
    double calmLateEnergy = 0.0;
    constexpr int calmStressSamples = static_cast<int>(sampleRate * 7.0);
    for (int offset = 0; offset < calmStressSamples; offset += blockSize)
    {
        layer3Input.clear();
        layer3Bypass.clear();
        if (offset == 0)
        {
            layer3Input.setSample(0, 0, 1.0f);
            layer3Input.setSample(1, 0, 1.0f);
        }
        layer3.process(layer3Input, layer3Bypass, layer3Output,
                       { 1.0f, 0.0f, 0.0f, false, false });
        if (! allFinite(layer3Output))
        {
            std::cerr << "Layer 3 calm maximum-space tail became non-finite\n";
            return 24;
        }
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const int absoluteSample = offset + sample;
                const double value = layer3Output.getSample(channel, sample);
                if (absoluteSample >= static_cast<int>(sampleRate)
                    && absoluteSample < static_cast<int>(sampleRate * 2.0))
                    calmEarlyEnergy += value * value;
                if (absoluteSample >= static_cast<int>(sampleRate * 6.0)
                    && absoluteSample < static_cast<int>(sampleRate * 7.0))
                    calmLateEnergy += value * value;
            }
    }
    if (calmEarlyEnergy <= 1.0e-10
        || calmLateEnergy > calmEarlyEnergy * 1.10)
    {
        std::cerr << "Layer 3 calm maximum-space tail gathered energy\n";
        return 25;
    }

    if (GravityBloomLayer::activationForSpace(0.55f) != 0.0f
        || GravityBloomLayer::activationForSpace(0.85f) < 0.999f
        || std::abs(GravityBloomLayer::bloomDelayMs(0, 0.90f) - 180.0f) > 0.1f
        || std::abs(GravityBloomLayer::bloomDelayMs(1, 0.90f) - 290.0f) > 0.1f
        || std::abs(GravityBloomLayer::bloomDelayMs(0, 1.0f) - 1200.0f) > 0.1f
        || std::abs(GravityBloomLayer::bloomDelayMs(1, 1.0f) - 1800.0f) > 0.1f
        || GravityBloomLayer::dryInjectionGain(1.0f) < 0.119f
        || GravityBloomLayer::chorusPreservationGain(0.72f) != 0.0f
        || GravityBloomLayer::chorusPreservationGain(1.0f) < 0.064f
        || GravityBloomLayer::chorusPreservationGain(1.0f) > 0.066f
        || GravityBloomLayer::chorusSidePreservationGain(0.72f) != 0.0f
        || GravityBloomLayer::chorusSidePreservationGain(1.0f) < 0.044f
        || GravityBloomLayer::chorusSidePreservationGain(1.0f) > 0.046f
        || GravityBloomLayer::shimmerAmountForSpace(0.75f, true) != 0.0f
        || GravityBloomLayer::shimmerAmountForSpace(1.0f, true) < 0.179f
        || GravityBloomLayer::shimmerAmountForSpace(1.0f, false) != 0.0f)
    {
        std::cerr << "Layer 4 macro or long-predelay mapping is incorrect\n";
        return 27;
    }

    GravityBloomLayer layer4;
    layer4.prepare(sampleRate, blockSize);
    juce::AudioBuffer<float> layer4Main(2, blockSize);
    juce::AudioBuffer<float> layer4Dry(2, blockSize);
    juce::AudioBuffer<float> layer4WarmTap(2, blockSize);
    juce::AudioBuffer<float> layer4ChorusTap(2, blockSize);
    juce::AudioBuffer<float> layer4Output;
    for (int offset = 0; offset < static_cast<int>(sampleRate); offset += blockSize)
    {
        layer4Main.clear();
        layer4Dry.clear();
        layer4WarmTap.clear();
        layer4ChorusTap.clear();
        layer4.process(layer4Main, layer4Dry, layer4WarmTap, layer4ChorusTap,
                       layer4Output,
                       { 1.0f, 1.0f, 1.0f, false, false });
    }
    float layer4Peak = 0.0f;
    double layer4LateEnergy = 0.0;
    for (int offset = 0; offset < stressSamples; offset += blockSize)
    {
        layer4Main.clear();
        layer4Dry.clear();
        layer4WarmTap.clear();
        layer4ChorusTap.clear();
        if (offset == 0)
        {
            layer4Main.setSample(0, 0, 1.0f);
            layer4Main.setSample(1, 0, 1.0f);
            layer4Dry.setSample(0, 0, 1.0f);
            layer4Dry.setSample(1, 0, 1.0f);
            layer4WarmTap.setSample(0, 0, 0.065f);
            layer4WarmTap.setSample(1, 0, 0.065f);
            layer4ChorusTap.setSample(0, 0, 0.25f);
            layer4ChorusTap.setSample(1, 0, 0.25f);
        }
        layer4.process(layer4Main, layer4Dry, layer4WarmTap, layer4ChorusTap,
                       layer4Output,
                       { 1.0f, 1.0f, 1.0f, false, false });
        if (! allFinite(layer4Output))
        {
            std::cerr << "Layer 4 produced non-finite samples\n";
            return 28;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            layer4Peak = juce::jmax(layer4Peak,
                layer4Output.getMagnitude(channel, 0, blockSize));
            if (offset >= static_cast<int>(sampleRate * 2.0))
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const double value = layer4Output.getSample(channel, sample);
                    layer4LateEnergy += value * value;
                }
        }
    }
    if (layer4Peak <= 1.0e-6f || layer4Peak > 4.0f
        || layer4LateEnergy <= 1.0e-10)
    {
        std::cerr << "Layer 4 impulse response is invalid\n";
        return 29;
    }

    if (TotalSpectralReturn::activationForSpace(0.90f) != 0.0f
        || TotalSpectralReturn::activationForSpace(1.0f) < 0.999f
        || TotalSpectralReturn::maximumGainForSpace(1.0f) > 0.0046f
        || TotalSpectralReturn::bandFrequencyHz(0) < 4000.0f
        || TotalSpectralReturn::bandFrequencyHz(2) > 13500.0f)
    {
        std::cerr << "Total spectral return mapping is incorrect\n";
        return 31;
    }

    TotalSpectralReturn totalReturn;
    totalReturn.prepare(sampleRate, blockSize);
    juce::AudioBuffer<float> totalInput(2, blockSize);
    juce::AudioBuffer<float> totalOutput;
    double totalReturnEnergy = 0.0;
    float totalReturnPeak = 0.0f;
    for (int offset = 0; offset < static_cast<int>(sampleRate * 2.0);
         offset += blockSize)
    {
        totalInput.clear();
        if (offset == 0)
        {
            totalInput.setSample(0, 0, 1.0f);
            totalInput.setSample(1, 0, 1.0f);
        }
        totalReturn.process(totalInput, totalOutput, 1.0f, 1.0f, 1.0f);
        if (! allFinite(totalOutput))
        {
            std::cerr << "Total spectral return produced non-finite samples\n";
            return 32;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            totalReturnPeak = juce::jmax(totalReturnPeak,
                totalOutput.getMagnitude(channel, 0, blockSize));
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const double value = totalOutput.getSample(channel, sample);
                totalReturnEnergy += value * value;
            }
        }
    }
    if (totalReturnEnergy <= 1.0e-14 || totalReturnPeak > 0.0121f)
    {
        std::cerr << "Total spectral return is silent or exceeded its ceiling\n";
        return 33;
    }

    // Run the complete serial topology at its most aggressive settings. This
    // includes both block-delayed returns, the long Layer 3 tank, Layer 4's
    // HALO shimmer, and the new post-Layer-4 spectral return into Layer 1.
    WarmRoomLayer fullLayer1;
    ChorusedSpaceLayer fullLayer2;
    DigitalSpectralLayer fullLayer3;
    GravityBloomLayer fullLayer4;
    TotalSpectralReturn fullReturn;
    StageToneFilter fullFinalHalo;
    fullLayer1.prepare(sampleRate, blockSize);
    fullLayer2.prepare(sampleRate, blockSize);
    fullLayer3.prepare(sampleRate, blockSize);
    fullLayer4.prepare(sampleRate, blockSize);
    fullReturn.prepare(sampleRate, blockSize);
    fullFinalHalo.prepare(sampleRate);
    fullFinalHalo.setModes(false, true);

    juce::AudioBuffer<float> fullDry(2, blockSize);
    juce::AudioBuffer<float> fullLayer1Input(2, blockSize);
    juce::AudioBuffer<float> fullLayer2Input(2, blockSize);
    juce::AudioBuffer<float> fullLayer1Out, fullSkip2, fullSkip23;
    juce::AudioBuffer<float> fullLayer2Out, fullLayer3Out, fullLayer4Out;
    juce::AudioBuffer<float> fullLayer2Feedback(2, blockSize);
    juce::AudioBuffer<float> fullTotalFeedback(2, blockSize);
    juce::AudioBuffer<float> fullCombinedFeedback(2, blockSize);
    juce::AudioBuffer<float> nextFullLayer2Feedback, nextFullTotalFeedback;
    fullLayer2Feedback.clear();
    fullTotalFeedback.clear();
    float fullNetworkPeak = 0.0f;
    float fullReturnPeak = 0.0f;
    std::array<double, 3> fullMidEnergy {};
    std::array<double, 3> fullSideEnergy {};
    constexpr int fullStressSamples = static_cast<int>(sampleRate * 12.0);
    for (int offset = 0; offset < fullStressSamples; offset += blockSize)
    {
        fullDry.clear();
        if (offset == 0)
        {
            fullDry.setSample(0, 0, 1.0f);
            fullDry.setSample(1, 0, 1.0f);
        }

        fullCombinedFeedback.makeCopyOf(fullLayer2Feedback, true);
        for (int channel = 0; channel < 2; ++channel)
            fullCombinedFeedback.addFrom(channel, 0, fullTotalFeedback,
                                         channel, 0, blockSize);

        for (int channel = 0; channel < 2; ++channel)
        {
            fullLayer1Input.copyFrom(channel, 0, fullDry, channel, 0,
                                     blockSize);
            fullLayer1Input.applyGain(channel, 0, blockSize,
                SerialRouting::layer1LiveInputGain(1.0f));
        }
        fullLayer1.process(fullLayer1Input, fullCombinedFeedback,
                           fullLayer1Out, fullSkip2, fullSkip23,
                           { 1.0f, 1.0f, 1.0f, false, false });

        for (int channel = 0; channel < 2; ++channel)
        {
            fullLayer2Input.copyFrom(channel, 0, fullLayer1Out, channel, 0,
                                     blockSize);
            fullLayer2Input.addFrom(channel, 0, fullDry, channel, 0,
                                    blockSize,
                                    SerialRouting::layer2DirectInputGain(1.0f));
        }
        fullLayer2.process(fullLayer2Input, fullLayer2Out,
                           nextFullLayer2Feedback,
                           { 1.0f, 1.0f, 1.0f, false, false });
        fullLayer2Feedback.makeCopyOf(nextFullLayer2Feedback, true);

        fullLayer3.process(fullLayer2Out, fullSkip2, fullLayer3Out,
                           { 1.0f, 1.0f, 1.0f, false, false });
        fullLayer4.process(fullLayer3Out, fullDry, fullSkip23, fullLayer2Out,
                           fullLayer4Out,
                           { 1.0f, 1.0f, 1.0f, false, true });
        for (int sample = 0; sample < blockSize; ++sample)
        {
            float left = fullLayer4Out.getSample(0, sample);
            float right = fullLayer4Out.getSample(1, sample);
            fullFinalHalo.processStereoSample(left, right);
            fullLayer4Out.setSample(0, sample, left);
            fullLayer4Out.setSample(1, sample, right);
        }
        fullReturn.process(fullLayer4Out, nextFullTotalFeedback,
                           1.0f, 1.0f, 1.0f);
        fullTotalFeedback.makeCopyOf(nextFullTotalFeedback, true);

        const std::array<const juce::AudioBuffer<float>*, 3> measuredLayers {
            &fullLayer2Out, &fullLayer3Out, &fullLayer4Out
        };
        for (size_t layer = 0; layer < measuredLayers.size(); ++layer)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const double left = measuredLayers[layer]->getSample(0, sample);
                const double right = measuredLayers[layer]->getSample(1, sample);
                const double mid = 0.5 * (left + right);
                const double side = 0.5 * (left - right);
                fullMidEnergy[layer] += mid * mid;
                fullSideEnergy[layer] += side * side;
            }

        if (! allFinite(fullLayer4Out) || ! allFinite(fullTotalFeedback))
        {
            std::cerr << "Complete four-layer feedback network became non-finite\n";
            return 34;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            fullNetworkPeak = juce::jmax(fullNetworkPeak,
                fullLayer4Out.getMagnitude(channel, 0, blockSize));
            fullReturnPeak = juce::jmax(fullReturnPeak,
                fullTotalFeedback.getMagnitude(channel, 0, blockSize));
        }
    }
    if (fullNetworkPeak <= 1.0e-6f || fullNetworkPeak > 4.0f
        || fullReturnPeak > 0.0121f)
    {
        std::cerr << "Complete four-layer feedback network exceeded its bounds\n";
        return 35;
    }

    const std::array<double, 3> widthRatios {
        fullSideEnergy[0] / juce::jmax(1.0e-20, fullMidEnergy[0]),
        fullSideEnergy[1] / juce::jmax(1.0e-20, fullMidEnergy[1]),
        fullSideEnergy[2] / juce::jmax(1.0e-20, fullMidEnergy[2])
    };
    std::cout << "Maximum-SPACE side/mid energy ratios: L2=" << widthRatios[0]
              << " L3=" << widthRatios[1]
              << " L4=" << widthRatios[2] << '\n';
    if (widthRatios[1] < 0.75 || widthRatios[1] > 1.20
        || widthRatios[2] < 0.82 || widthRatios[2] > 1.25)
    {
        std::cerr << "High-SPACE stereo image collapsed or became excessive\n";
        return 37;
    }

    // Exercise the alternate identity-preserving order at the same extreme:
    // Layer 1 -> Layer 3 -> Layer 2 -> Layer 4, with both feedback arcs active
    // and HALO applied once after the complete network.
    WarmRoomLayer vectorLayer1;
    DigitalSpectralLayer vectorLayer3;
    ChorusedSpaceLayer vectorLayer2;
    GravityBloomLayer vectorLayer4;
    TotalSpectralReturn vectorReturn;
    StageToneFilter vectorFinalHalo;
    vectorLayer1.prepare(sampleRate, blockSize);
    vectorLayer3.prepare(sampleRate, blockSize);
    vectorLayer2.prepare(sampleRate, blockSize);
    vectorLayer4.prepare(sampleRate, blockSize);
    vectorReturn.prepare(sampleRate, blockSize);
    vectorFinalHalo.prepare(sampleRate);
    vectorFinalHalo.setModes(false, true);

    juce::AudioBuffer<float> vectorDry(2, blockSize);
    juce::AudioBuffer<float> vectorLayer1Input(2, blockSize);
    juce::AudioBuffer<float> vectorLayer2Input(2, blockSize);
    juce::AudioBuffer<float> vectorLayer1Out, vectorSkip2, vectorSkip23;
    juce::AudioBuffer<float> vectorLayer2Out, vectorLayer3Out, vectorLayer4Out;
    juce::AudioBuffer<float> vectorLayer4Input(2, blockSize);
    juce::AudioBuffer<float> vectorZeroBypass(2, blockSize);
    juce::AudioBuffer<float> vectorLayer2Feedback(2, blockSize);
    juce::AudioBuffer<float> vectorTotalFeedback(2, blockSize);
    juce::AudioBuffer<float> vectorCombinedFeedback(2, blockSize);
    juce::AudioBuffer<float> nextVectorLayer2Feedback, nextVectorTotalFeedback;
    vectorZeroBypass.clear();
    vectorLayer2Feedback.clear();
    vectorTotalFeedback.clear();
    float vectorPeak = 0.0f;
    float vectorReturnPeak = 0.0f;
    std::array<double, 12> vectorWindowEnergy {};
    constexpr int vectorStressSamples = static_cast<int>(sampleRate * 120.0);
    for (int offset = 0; offset < vectorStressSamples; offset += blockSize)
    {
        vectorDry.clear();
        if (offset == 0)
        {
            vectorDry.setSample(0, 0, 1.0f);
            vectorDry.setSample(1, 0, 1.0f);
        }
        vectorCombinedFeedback.makeCopyOf(vectorLayer2Feedback, true);
        for (int channel = 0; channel < 2; ++channel)
            vectorCombinedFeedback.addFrom(channel, 0, vectorTotalFeedback,
                                           channel, 0, blockSize);
        for (int channel = 0; channel < 2; ++channel)
        {
            vectorLayer1Input.copyFrom(channel, 0, vectorDry, channel, 0,
                                       blockSize);
            vectorLayer1Input.applyGain(channel, 0, blockSize,
                SerialRouting::layer1LiveInputGain(1.0f));
        }
        vectorLayer1.process(vectorLayer1Input, vectorCombinedFeedback,
                             vectorLayer1Out, vectorSkip2, vectorSkip23,
                             { 1.0f, 1.0f, 1.0f, false, false });
        vectorLayer3.process(vectorLayer1Out, vectorZeroBypass, vectorLayer3Out,
                             { 1.0f, 1.0f, 1.0f, false, false });
        for (int channel = 0; channel < 2; ++channel)
        {
            vectorLayer2Input.copyFrom(channel, 0, vectorLayer3Out, channel, 0,
                                       blockSize);
            vectorLayer2Input.addFrom(channel, 0, vectorDry, channel, 0,
                                      blockSize,
                                      SerialRouting::layer2DirectInputGain(1.0f));
        }
        vectorLayer2.process(vectorLayer2Input, vectorLayer2Out,
                             nextVectorLayer2Feedback,
                             { 1.0f, 1.0f, 1.0f, false, false });
        vectorLayer2Feedback.makeCopyOf(nextVectorLayer2Feedback, true);
        vectorLayer4Input.makeCopyOf(vectorLayer2Out, true);
        for (int channel = 0; channel < 2; ++channel)
            vectorLayer4Input.addFrom(channel, 0, vectorLayer3Out, channel, 0,
                                      blockSize,
                                      SerialRouting::vectorSpectralTailGain(1.0f));
        vectorLayer4.process(vectorLayer4Input, vectorDry, vectorSkip23,
                             vectorLayer2Out, vectorLayer4Out,
                             { 1.0f, 1.0f, 1.0f, false, true });
        for (int sample = 0; sample < blockSize; ++sample)
        {
            float left = vectorLayer4Out.getSample(0, sample);
            float right = vectorLayer4Out.getSample(1, sample);
            vectorFinalHalo.processStereoSample(left, right);
            vectorLayer4Out.setSample(0, sample, left);
            vectorLayer4Out.setSample(1, sample, right);
        }
        vectorReturn.process(vectorLayer4Out, nextVectorTotalFeedback,
                             1.0f, 1.0f, 1.0f);
        vectorTotalFeedback.makeCopyOf(nextVectorTotalFeedback, true);
        if (! allFinite(vectorLayer4Out) || ! allFinite(vectorTotalFeedback))
        {
            std::cerr << "VECTOR topology became non-finite\n";
            return 38;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            vectorPeak = juce::jmax(vectorPeak,
                vectorLayer4Out.getMagnitude(channel, 0, blockSize));
            vectorReturnPeak = juce::jmax(vectorReturnPeak,
                vectorTotalFeedback.getMagnitude(channel, 0, blockSize));
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const int absoluteSample = offset + sample;
                const size_t window = static_cast<size_t>(juce::jlimit(0, 11,
                    absoluteSample / static_cast<int>(sampleRate * 10.0)));
                const double value = vectorLayer4Out.getSample(channel, sample);
                vectorWindowEnergy[window] += value * value;
            }
        }
    }
    if (vectorPeak <= 1.0e-6f || vectorPeak > 4.0f
        || vectorReturnPeak > 0.0121f)
    {
        std::cerr << "VECTOR topology exceeded its safety bounds\n";
        return 39;
    }
    std::cout << "VECTOR 10-second tail RMS dB:";
    for (const double windowEnergy : vectorWindowEnergy)
    {
        const double rms = std::sqrt(windowEnergy
            / (2.0 * sampleRate * 10.0));
        std::cout << ' ' << juce::Decibels::gainToDecibels(rms, -180.0);
    }
    std::cout << '\n';

    std::cout << "Both layer orders and guarded total return DSP tests passed\n";
    return 0;
}
