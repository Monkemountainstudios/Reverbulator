#include <JuceHeader.h>
#include "PluginEditor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    ReverbulatorAudioProcessor processor;
    if (auto* body = processor.parameters.getParameter("body"))
        body->setValueNotifyingHost(1.0f);
    ReverbulatorAudioProcessorEditor editor(processor);
    editor.setSize(620, 820);
    editor.resized();

    auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    const auto output = juce::File::getCurrentWorkingDirectory()
                            .getChildFile("reverbulator-ui-meter-depth.png");
    auto stream = output.createOutputStream();
    if (stream == nullptr)
        return 1;

    juce::PNGImageFormat png;
    if (! png.writeImageToStream(image, *stream))
        return 2;

    stream->flush();
    return output.existsAsFile() ? 0 : 3;
}
