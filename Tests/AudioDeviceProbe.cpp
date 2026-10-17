#include <JuceHeader.h>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::AudioDeviceManager manager;
    const auto initialError = manager.initialise(2, 2, nullptr, true);
    std::cout << "DEFAULT DUPLEX: "
              << (initialError.isEmpty() ? "OK" : initialError) << "\n";
    manager.closeAudioDevice();
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes(types);

    for (auto* type : types)
    {
        type->scanForDevices();
        const auto outputs = type->getDeviceNames(false);
        const auto inputs = type->getDeviceNames(true);
        std::cout << "TYPE: " << type->getTypeName() << "\n";
        for (const auto& output : outputs)
            std::cout << "  OUT: " << output << "\n";
        for (const auto& input : inputs)
            std::cout << "  IN:  " << input << "\n";

        for (const auto& output : outputs)
        {
            const auto input = inputs.contains(output)
                             ? output
                             : (inputs.isEmpty() ? juce::String() : inputs[0]);
            if (input.isEmpty())
                continue;

            manager.setCurrentAudioDeviceType(type->getTypeName(), true);
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            manager.getAudioDeviceSetup(setup);
            setup.outputDeviceName = output;
            setup.inputDeviceName = input;
            setup.useDefaultOutputChannels = true;
            setup.useDefaultInputChannels = true;
            setup.sampleRate = 0.0;
            setup.bufferSize = type->getTypeName() == "ASIO" ? 256 : 0;
            const auto error = manager.setAudioDeviceSetup(setup, true);
            auto* device = manager.getCurrentAudioDevice();
            const int activeInputs = device == nullptr ? 0
                : device->getActiveInputChannels().countNumberOfSetBits();
            const int activeOutputs = device == nullptr ? 0
                : device->getActiveOutputChannels().countNumberOfSetBits();
            std::cout << "  TRY: " << input << " -> " << output
                      << " | input channels=" << activeInputs
                      << " output channels=" << activeOutputs
                      << " | " << (error.isEmpty() ? "OK" : error) << "\n";
            manager.closeAudioDevice();
        }
    }

    return 0;
}
