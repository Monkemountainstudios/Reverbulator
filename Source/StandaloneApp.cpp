#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#if JucePlugin_Build_Standalone

namespace juce
{
class ReverbulatorStandaloneApp final : public JUCEApplication
{
public:
    ReverbulatorStandaloneApp()
    {
        PropertiesFile::Options options;
        options.applicationName = CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.folderName = "";
        appProperties.setStorageParameters(options);
    }

    const String getApplicationName() override { return CharPointer_UTF8(JucePlugin_Name); }
    const String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void anotherInstanceStarted(const String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible(true);
            mainWindow->toFront(true);
        }
    }

    void initialise(const String&) override
    {
        auto* settings = appProperties.getUserSettings();
        discardBrokenSavedDevice(settings);

        auto holder = std::make_unique<StandalonePluginHolder>(settings, false);
        recoverAudioDevice(*holder);

        // Reverbulator is an insert effect, so a muted capture path makes the
        // standalone build appear broken even when the endpoint opened. JUCE
        // defaults to muting potential feedback loops; explicitly opt into the
        // live input expected by this application and remember that choice.
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
        {
            if (device->getActiveInputChannels().countNumberOfSetBits() > 0)
            {
                holder->getMuteInputValue() = false;
                holder->saveAudioDeviceState();
            }
        }

        mainWindow = std::make_unique<StandaloneFilterWindow>(
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour(
                ResizableWindow::backgroundColourId),
            std::move(holder));
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->getPluginHolder()->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay(100, []
            {
                if (auto* app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    static void discardBrokenSavedDevice(PropertySet* settings)
    {
        if (settings == nullptr)
            return;

        if (auto saved = settings->getXmlValue("audioSetup"))
        {
            const auto output = saved->getStringAttribute("audioOutputDeviceName").trim();
            if (output.isEmpty())
                settings->removeValue("audioSetup");
        }
    }

    static bool trySetup(AudioDeviceManager& manager,
                         AudioIODeviceType& type,
                         const String& outputName,
                         const String& inputName)
    {
        AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup(setup);
        setup.outputDeviceName = outputName;
        setup.inputDeviceName = inputName;
        setup.useDefaultOutputChannels = true;
        setup.useDefaultInputChannels = inputName.isNotEmpty();
        setup.sampleRate = 0.0;
        setup.bufferSize = type.getTypeName() == "ASIO" ? 256 : 0;
        return manager.setAudioDeviceSetup(setup, true).isEmpty();
    }

    static void recoverAudioDevice(StandalonePluginHolder& holder)
    {
        auto& manager = holder.deviceManager;
        if (auto* current = manager.getCurrentAudioDevice())
            if (current->getActiveOutputChannels().countNumberOfSetBits() > 0
                && current->getActiveInputChannels().countNumberOfSetBits() > 0)
                return;

        // Prefer low-latency ASIO when it is genuinely usable, then move to
        // Windows' shared endpoint instead of leaving the application blocked
        // by a disconnected interface remembered in its settings file.
       #if JUCE_WINDOWS
        const StringArray preferredTypes { "ASIO", "Windows Audio" };
       #elif JUCE_MAC
        const StringArray preferredTypes { "CoreAudio" };
       #else
        const StringArray preferredTypes { "ALSA", "JACK" };
       #endif
        for (const auto& preferredType : preferredTypes)
        {
            for (auto* type : manager.getAvailableDeviceTypes())
            {
                if (type == nullptr || type->getTypeName() != preferredType)
                    continue;

                type->scanForDevices();
                manager.setCurrentAudioDeviceType(type->getTypeName(), true);
                const auto outputs = type->getDeviceNames(false);
                const auto inputs = type->getDeviceNames(true);

                for (const auto& output : outputs)
                {
                    // ASIO normally exposes one duplex device name. Shared
                    // Windows devices may have independent capture endpoints.
                    String preferredInput;
                    if (inputs.contains(output))
                        preferredInput = output;
                    else if (! inputs.isEmpty())
                        preferredInput = inputs[0];

                    if (preferredInput.isNotEmpty()
                        && trySetup(manager, *type, output, preferredInput))
                    {
                        holder.saveAudioDeviceState();
                        return;
                    }

                    // Opening the UI with output only is more useful than
                    // failing outright; an input can then be chosen in Options.
                    if (trySetup(manager, *type, output, {}))
                    {
                        holder.saveAudioDeviceState();
                        return;
                    }
                }
            }
        }
    }

    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
};
}

JUCE_CREATE_APPLICATION_DEFINE(juce::ReverbulatorStandaloneApp)

#endif
