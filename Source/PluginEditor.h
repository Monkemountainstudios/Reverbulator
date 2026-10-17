#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SrsLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
};

class FineWheelSlider final : public juce::Slider
{
public:
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
};

class LampButton final : public juce::Button
{
public:
    explicit LampButton(const juce::String& text);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

class MassMeter final : public juce::Component, private juce::Timer
{
public:
    explicit MassMeter(ReverbulatorAudioProcessor&);
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    ReverbulatorAudioProcessor& processor;
    float displayedLevel = 0.0f;
    float peakLevel = 0.0f;
    int peakHoldFrames = 0;
};

class PresetStepper final : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    explicit PresetStepper(ReverbulatorAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void selectRelative(int delta);

    ReverbulatorAudioProcessor& processor;
    int displayedProgram = -1;
    int nameHoldFrames = 0;
    float nameAlpha = 0.0f;
    juce::String displayedName;
};

class ReverbulatorAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ReverbulatorAudioProcessorEditor(ReverbulatorAudioProcessor&);
    ~ReverbulatorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void configureMacro(juce::Slider&, const juce::String&, const char*,
                        std::unique_ptr<SliderAttachment>&);
    void drawScrew(juce::Graphics&, juce::Point<float>, float, float) const;

    ReverbulatorAudioProcessor& processor;
    SrsLookAndFeel lookAndFeel;
    FineWheelSlider mix, space, motion, chaos;
    LampButton body { "BODY" };
    LampButton vectorOrder { "VECTOR" };
    LampButton halo { "HALO" };
    MassMeter massMeter;
    PresetStepper presetStepper;

    std::unique_ptr<SliderAttachment> mixAttachment, spaceAttachment;
    std::unique_ptr<SliderAttachment> motionAttachment, chaosAttachment;
    std::unique_ptr<ButtonAttachment> bodyAttachment, orderAttachment, haloAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbulatorAudioProcessorEditor)
};

