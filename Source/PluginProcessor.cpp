#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct FactoryPreset
{
    const char* name;
    float mix;
    float space;
    float motion;
    float chaos;
    bool body;
    bool halo;
    bool vector;
};

constexpr std::array<FactoryPreset, 9> factoryPresets {{
    { "MANUAL",         0.36f, 0.32f, 0.24f, 0.08f, false, false, false },
    { "METAL TANK",     0.40f, 0.07f, 0.25f, 0.15f, false, false, false },
    { "WARM EMS",       0.50f, 0.25f, 0.50f, 0.76f, true,  false, false },
    { "METAL STAIRWAY", 0.50f, 0.10f, 0.80f, 0.95f, false, false, true  },
    { "CHOPPER",        0.50f, 0.40f, 0.80f, 0.01f, false, false, false },
    { "WHITE ARCH",     0.60f, 0.60f, 0.65f, 0.60f, false, true,  false },
    { "DEMON CRY",      0.75f, 0.75f, 0.55f, 1.00f, true,  false, false },
    { "EMPTY UNIVERSE", 0.50f, 1.00f, 1.00f, 0.76f, true,  true,  false },
    { "PURE HORROR",    0.75f, 0.20f, 0.25f, 1.00f, true,  false, true  }
}};
}

ReverbulatorAudioProcessor::ReverbulatorAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SRS4_STATE", createParameterLayout())
{
    constexpr std::array<const char*, 7> presetParameters {
        "mix", "space", "motion", "chaos", "body", "halo", "vectorOrder"
    };
    for (const auto* id : presetParameters)
        parameters.addParameterListener(id, this);
}

ReverbulatorAudioProcessor::~ReverbulatorAudioProcessor()
{
    constexpr std::array<const char*, 7> presetParameters {
        "mix", "space", "motion", "chaos", "body", "halo", "vectorOrder"
    };
    for (const auto* id : presetParameters)
        parameters.removeParameterListener(id, this);
}

void ReverbulatorAudioProcessor::parameterChanged(const juce::String&, float)
{
    if (! applyingProgram.load(std::memory_order_relaxed)
        && currentProgram.load(std::memory_order_relaxed) > 0)
        programModified.store(true, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout
ReverbulatorAudioProcessor::createParameterLayout()
{
    using FloatParameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(std::make_unique<FloatParameter>("mix", "Mix", Range(0.0f, 1.0f, 0.001f), 0.36f));
    result.push_back(std::make_unique<FloatParameter>("space", "Space", Range(0.0f, 1.0f, 0.001f), 0.32f));
    result.push_back(std::make_unique<FloatParameter>("motion", "Motion", Range(0.0f, 1.0f, 0.001f), 0.24f));
    result.push_back(std::make_unique<FloatParameter>("chaos", "Chaos", Range(0.0f, 1.0f, 0.001f), 0.08f));
    result.push_back(std::make_unique<BoolParameter>("body", "Body", false));
    result.push_back(std::make_unique<BoolParameter>("halo", "Halo", false));
    result.push_back(std::make_unique<BoolParameter>("vectorOrder", "Vector Order", false));
    return { result.begin(), result.end() };
}

void ReverbulatorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    massEnvelope = 0.0f;
    massLevel.store(0.0f, std::memory_order_relaxed);
    warmRoomLayer.prepare(sampleRate, samplesPerBlock);
    chorusedSpaceLayer.prepare(sampleRate, samplesPerBlock);
    digitalSpectralLayer.prepare(sampleRate, samplesPerBlock);
    vectorChorusedSpaceLayer.prepare(sampleRate, samplesPerBlock);
    vectorDigitalSpectralLayer.prepare(sampleRate, samplesPerBlock);
    gravityBloomLayer.prepare(sampleRate, samplesPerBlock);
    totalSpectralReturn.prepare(sampleRate, samplesPerBlock);
    finalHaloFilter.prepare(sampleRate);
    laterFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    nextFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    vectorNextFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    blendedNextFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    totalFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    nextTotalFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    combinedFeedbackBus.setSize(2, samplesPerBlock, false, true, false);
    mixAmount.reset(sampleRate, 0.035);
    mixAmount.setCurrentAndTargetValue(parameters.getRawParameterValue("mix")->load());
    orderAmount.reset(sampleRate, 0.180);
    orderAmount.setCurrentAndTargetValue(
        parameters.getRawParameterValue("vectorOrder")->load());
    networkActivationAmount.reset(sampleRate, 0.035);
    networkActivationAmount.setCurrentAndTargetValue(
        SerialRouting::networkActivationForSpace(
            parameters.getRawParameterValue("space")->load()));
}

void ReverbulatorAudioProcessor::releaseResources()
{
    massEnvelope = 0.0f;
    warmRoomLayer.reset();
    chorusedSpaceLayer.reset();
    digitalSpectralLayer.reset();
    vectorChorusedSpaceLayer.reset();
    vectorDigitalSpectralLayer.reset();
    gravityBloomLayer.reset();
    totalSpectralReturn.reset();
    finalHaloFilter.reset();
    laterFeedbackBus.clear();
    nextFeedbackBus.clear();
    vectorNextFeedbackBus.clear();
    blendedNextFeedbackBus.clear();
    totalFeedbackBus.clear();
    nextTotalFeedbackBus.clear();
    combinedFeedbackBus.clear();
}

bool ReverbulatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && output == juce::AudioChannelSet::stereo();
}

int ReverbulatorAudioProcessor::getNumPrograms()
{
    return static_cast<int>(factoryPresets.size());
}

int ReverbulatorAudioProcessor::getCurrentProgram()
{
    return currentProgram.load(std::memory_order_relaxed);
}

void ReverbulatorAudioProcessor::setCurrentProgram(int index)
{
    index = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& preset = factoryPresets[static_cast<size_t>(index)];
    applyingProgram.store(true, std::memory_order_relaxed);
    const auto setParameter = [this] (const char* id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
            parameter->endChangeGesture();
        }
    };
    setParameter("mix", preset.mix);
    setParameter("space", preset.space);
    setParameter("motion", preset.motion);
    setParameter("chaos", preset.chaos);
    setParameter("body", preset.body ? 1.0f : 0.0f);
    setParameter("halo", preset.halo ? 1.0f : 0.0f);
    setParameter("vectorOrder", preset.vector ? 1.0f : 0.0f);
    currentProgram.store(index, std::memory_order_relaxed);
    programModified.store(false, std::memory_order_relaxed);
    applyingProgram.store(false, std::memory_order_relaxed);
}

const juce::String ReverbulatorAudioProcessor::getProgramName(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return {};
    return factoryPresets[static_cast<size_t>(index)].name;
}

void ReverbulatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int samples = buffer.getNumSamples();
    dryBuffer.makeCopyOf(buffer, true);
    layer1Input.setSize(2, samples, false, false, true);
    layer2Input.setSize(2, samples, false, false, true);
    if (laterFeedbackBus.getNumSamples() != samples)
    {
        laterFeedbackBus.setSize(2, samples, false, true, false);
        laterFeedbackBus.clear();
    }
    if (totalFeedbackBus.getNumSamples() != samples)
    {
        totalFeedbackBus.setSize(2, samples, false, true, false);
        totalFeedbackBus.clear();
    }
    combinedFeedbackBus.makeCopyOf(laterFeedbackBus, true);
    for (int channel = 0; channel < 2; ++channel)
        combinedFeedbackBus.addFrom(channel, 0, totalFeedbackBus,
                                    channel, 0, samples);

    const float space = parameters.getRawParameterValue("space")->load();
    const float layer1LiveGain = SerialRouting::layer1LiveInputGain(space);
    const float layer2DirectGain = SerialRouting::layer2DirectInputGain(space);
    for (int channel = 0; channel < 2; ++channel)
    {
        layer1Input.copyFrom(channel, 0, dryBuffer, channel, 0, samples);
        layer1Input.applyGain(channel, 0, samples, layer1LiveGain);
    }

    const bool body = parameters.getRawParameterValue("body")->load() > 0.5f;
    const bool halo = parameters.getRawParameterValue("halo")->load() > 0.5f;
    warmRoomLayer.process(layer1Input, combinedFeedbackBus,
                          layer1Output, layer1ToLayer3, layer1ToLayer4,
                          { space,
                            parameters.getRawParameterValue("motion")->load(),
                            parameters.getRawParameterValue("chaos")->load(),
                            body, false });

    for (int channel = 0; channel < 2; ++channel)
    {
        layer2Input.copyFrom(channel, 0, layer1Output, channel, 0, samples);
        layer2Input.addFrom(channel, 0, dryBuffer, channel, 0, samples,
                            layer2DirectGain);
    }

    const ChorusedSpaceLayer::MacroState layer2Macros {
        space,
        parameters.getRawParameterValue("motion")->load(),
        parameters.getRawParameterValue("chaos")->load(),
        body,
        false
    };
    chorusedSpaceLayer.process(layer2Input, layer2Output,
                               nextFeedbackBus, layer2Macros);

    digitalSpectralLayer.process(layer2Output, layer1ToLayer3, layer3Output,
        { space,
          parameters.getRawParameterValue("motion")->load(),
          parameters.getRawParameterValue("chaos")->load(),
          body,
          false });

    // Alternate identity-preserving topology: Layer 1 -> Layer 3 -> Layer 2.
    // Both middle chains run continuously, so ORDER can crossfade without
    // emptying either tank or exposing a discontinuous routing change.
    vectorBypassBuffer.setSize(2, samples, false, false, true);
    vectorBypassBuffer.clear();
    vectorDigitalSpectralLayer.process(layer1Output, vectorBypassBuffer,
        vectorLayer3Output,
        { space,
          parameters.getRawParameterValue("motion")->load(),
          parameters.getRawParameterValue("chaos")->load(),
          body,
          false });
    vectorLayer2Input.setSize(2, samples, false, false, true);
    for (int channel = 0; channel < 2; ++channel)
    {
        vectorLayer2Input.copyFrom(channel, 0, vectorLayer3Output,
                                   channel, 0, samples);
        vectorLayer2Input.addFrom(channel, 0, dryBuffer, channel, 0, samples,
                                  layer2DirectGain);
    }
    vectorChorusedSpaceLayer.process(vectorLayer2Input, vectorLayer2Output,
                                     vectorNextFeedbackBus, layer2Macros);

    layer4MainInput.setSize(2, samples, false, false, true);
    layer4Layer2Tap.setSize(2, samples, false, false, true);
    blendedNextFeedbackBus.setSize(2, samples, false, false, true);
    orderAmount.setTargetValue(
        parameters.getRawParameterValue("vectorOrder")->load());
    for (int sample = 0; sample < samples; ++sample)
    {
        const float vectorMix = orderAmount.getNextValue();
        const float vectorSpectralTail = SerialRouting::vectorSpectralTailGain(space);
        for (int channel = 0; channel < 2; ++channel)
        {
            const float vectorMain = vectorLayer2Output.getSample(channel, sample)
                + vectorLayer3Output.getSample(channel, sample) * vectorSpectralTail;
            layer4MainInput.setSample(channel, sample, juce::jmap(vectorMix,
                layer3Output.getSample(channel, sample),
                vectorMain));
            layer4Layer2Tap.setSample(channel, sample, juce::jmap(vectorMix,
                layer2Output.getSample(channel, sample),
                vectorLayer2Output.getSample(channel, sample)));
            blendedNextFeedbackBus.setSample(channel, sample, juce::jmap(vectorMix,
                nextFeedbackBus.getSample(channel, sample),
                vectorNextFeedbackBus.getSample(channel, sample)));
        }
    }
    // The selected Layer 2 return is deliberately delayed by one host block.
    laterFeedbackBus.makeCopyOf(blendedNextFeedbackBus, true);

    gravityBloomLayer.process(layer4MainInput, dryBuffer, layer1ToLayer4,
        layer4Layer2Tap,
        layer4Output,
        { space,
          parameters.getRawParameterValue("motion")->load(),
          parameters.getRawParameterValue("chaos")->load(),
          body,
          halo });

    // HALO's main low cut and presence lift happen once here. Earlier stages
    // retain their low-mid spatial information; guarded feedback paths keep
    // their own dedicated safety filtering.
    finalHaloFilter.setModes(false, halo);
    networkActivationAmount.setTargetValue(
        SerialRouting::networkActivationForSpace(space));
    for (int sample = 0; sample < samples; ++sample)
    {
        float left = layer4Output.getSample(0, sample);
        float right = layer4Output.getSample(1, sample);
        finalHaloFilter.processStereoSample(left, right);
        const float networkActivation = networkActivationAmount.getNextValue();
        layer4Output.setSample(0, sample, left * networkActivation);
        layer4Output.setSample(1, sample, right * networkActivation);
    }

    totalSpectralReturn.process(layer4Output, nextTotalFeedbackBus,
        space,
        parameters.getRawParameterValue("motion")->load(),
        parameters.getRawParameterValue("chaos")->load());
    totalFeedbackBus.makeCopyOf(nextTotalFeedbackBus, true);

    mixAmount.setTargetValue(parameters.getRawParameterValue("mix")->load());
    for (int sample = 0; sample < samples; ++sample)
    {
        const float mix = mixAmount.getNextValue();
        const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < 2; ++channel)
            buffer.setSample(channel, sample,
                dryBuffer.getSample(channel, sample) * dryGain
                + layer4Output.getSample(channel, sample) * wetGain);
    }

    // MASS follows the energy accumulated in the reverb network, not the dry
    // input level. Future layers will be folded into the same measurement.
    float sumSquares = 0.0f;
    const int channels = juce::jmax(1, buffer.getNumChannels());
    const int measuredSamples = juce::jmax(1, samples);
    for (int channel = 0; channel < layer4Output.getNumChannels(); ++channel)
    {
        const auto* data = layer4Output.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample)
            sumSquares += data[sample] * data[sample];
    }

    const float rms = std::sqrt(sumSquares / static_cast<float>(channels * measuredSamples));
    // MASS is a calibrated view of wet-network RMS, not an extra-gain meter.
    // Its full travel is -60 to 0 dB; numerical dust below -60 dB rests at
    // the bottom instead of looking like stored audible energy.
    const float wetDb = juce::Decibels::gainToDecibels(rms, -100.0f);
    const float target = wetDb <= -60.0f ? 0.0f
        : juce::jlimit(0.0f, 1.0f, (wetDb + 60.0f) / 60.0f);
    const float seconds = target > massEnvelope ? 0.045f : 0.72f;
    const float coefficient = std::exp(-static_cast<float>(buffer.getNumSamples())
                                       / static_cast<float>(currentSampleRate * seconds));
    massEnvelope = target + coefficient * (massEnvelope - target);
    massLevel.store(massEnvelope, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ReverbulatorAudioProcessor::createEditor()
{
    return new ReverbulatorAudioProcessorEditor(*this);
}

void ReverbulatorAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("currentProgram",
                      currentProgram.load(std::memory_order_relaxed), nullptr);
    state.setProperty("programModified",
                      programModified.load(std::memory_order_relaxed), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void ReverbulatorAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        const bool restoredModified = static_cast<bool>(
            state.getProperty("programModified", false));
        currentProgram.store(juce::jlimit(0, getNumPrograms() - 1,
            static_cast<int>(state.getProperty("currentProgram", 0))),
            std::memory_order_relaxed);
        applyingProgram.store(true, std::memory_order_relaxed);
        parameters.replaceState(state);
        applyingProgram.store(false, std::memory_order_relaxed);
        programModified.store(restoredModified, std::memory_order_relaxed);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverbulatorAudioProcessor();
}

