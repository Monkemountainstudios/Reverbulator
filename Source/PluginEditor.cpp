#include "PluginEditor.h"

namespace
{
const auto panelBlack = juce::Colour(0xff111315);
const auto deepBlack = juce::Colour(0xff050607);
const auto cobalt = juce::Colour(0xff173e73);
const auto cobaltLight = juce::Colour(0xff315f9d);
const auto warmInk = juce::Colour(0xffc7bca4);
const auto amber = juce::Colour(0xffd68a2d);
const auto darkRed = juce::Colour(0xff5e1718);
const auto lampRed = juce::Colour(0xffb53a2f);

juce::Font condensedFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions("Arial Narrow", bold ? "Bold" : "Regular", height));
}
}

void FineWheelSlider::mouseWheelMove(const juce::MouseEvent& event,
                                     const juce::MouseWheelDetails& wheel)
{
    auto fineWheel = wheel;
    fineWheel.deltaX *= 0.20f;
    fineWheel.deltaY *= 0.20f;
    juce::Slider::mouseWheelMove(event, fineWheel);
}

void SrsLookAndFeel::drawRotarySlider(juce::Graphics& graphics,
                                      int x, int y, int width, int height,
                                      float sliderPos, float startAngle,
                                      float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height));
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.34f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    graphics.setColour(deepBlack.withAlpha(0.80f));
    graphics.fillEllipse(centre.x - radius - 5.0f, centre.y - radius + 6.0f,
                         radius * 2.0f + 10.0f, radius * 2.0f + 10.0f);

    graphics.setColour(juce::Colour(0xff060708));
    graphics.fillEllipse(centre.x - radius - 4.0f, centre.y - radius - 4.0f,
                         radius * 2.0f + 8.0f, radius * 2.0f + 8.0f);
    graphics.setColour(juce::Colour(0xff323638));
    graphics.drawEllipse(centre.x - radius - 4.0f, centre.y - radius - 4.0f,
                         radius * 2.0f + 8.0f, radius * 2.0f + 8.0f, 1.2f);

    for (int tick = 0; tick <= 20; ++tick)
    {
        const float proportion = static_cast<float>(tick) / 20.0f;
        const float tickAngle = startAngle + proportion * (endAngle - startAngle);
        const bool danger = proportion >= 0.90f;
        const float outer = radius + 14.0f;
        const float inner = outer - (tick % 5 == 0 ? 7.0f : 4.0f);
        const juce::Point<float> a(centre.x + std::sin(tickAngle) * inner,
                                   centre.y - std::cos(tickAngle) * inner);
        const juce::Point<float> b(centre.x + std::sin(tickAngle) * outer,
                                   centre.y - std::cos(tickAngle) * outer);
        graphics.setColour(danger ? lampRed.withAlpha(0.90f) : warmInk.withAlpha(0.48f));
        graphics.drawLine({ a, b }, danger ? 1.8f : 1.1f);
    }

    juce::ColourGradient knobGradient(cobaltLight, centre.x - radius, centre.y - radius,
                                      cobalt.darker(0.58f), centre.x + radius,
                                      centre.y + radius, false);
    knobGradient.addColour(0.48, cobalt);
    graphics.setGradientFill(knobGradient);
    graphics.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    graphics.setColour(juce::Colours::white.withAlpha(0.16f));
    juce::Path knobHighlight;
    knobHighlight.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f,
                                0.0f, juce::MathConstants<float>::pi * 1.08f,
                                juce::MathConstants<float>::pi * 1.82f, true);
    graphics.strokePath(knobHighlight, juce::PathStrokeType(2.0f));
    graphics.setColour(deepBlack.withAlpha(0.72f));
    graphics.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    juce::Path pointer;
    pointer.addRoundedRectangle(-1.8f, -radius * 0.79f, 3.6f, radius * 0.54f, 1.5f);
    graphics.setColour(juce::Colour(0xffdce8ef));
    graphics.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

LampButton::LampButton(const juce::String& text) : juce::Button(text)
{
    setClickingTogglesState(true);
    setTitle(text);
    setTooltip(text + " tonal circuit");
}

void LampButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    const bool lit = getToggleState();

    graphics.setColour(deepBlack.withAlpha(0.82f));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 3.0f), 5.0f);

    juce::ColourGradient housing(juce::Colour(0xff35383a), bounds.getX(), bounds.getY(),
                                 juce::Colour(0xff070809), bounds.getRight(),
                                 bounds.getBottom(), false);
    graphics.setGradientFill(housing);
    graphics.fillRoundedRectangle(bounds, 5.0f);

    auto lens = bounds.reduced(6.0f, 5.0f);
    if (down)
        lens = lens.reduced(1.0f).translated(0.0f, 1.0f);

    const auto topColour = lit ? lampRed.brighter(highlighted ? 0.22f : 0.10f)
                               : darkRed.brighter(highlighted ? 0.10f : 0.0f);
    const auto bottomColour = lit ? lampRed.darker(0.36f) : darkRed.darker(0.52f);
    juce::ColourGradient lensGradient(topColour, lens.getX(), lens.getY(),
                                      bottomColour, lens.getRight(), lens.getBottom(), false);
    lensGradient.addColour(0.46, lit ? juce::Colour(0xff8e2925) : juce::Colour(0xff481214));
    graphics.setGradientFill(lensGradient);
    graphics.fillRoundedRectangle(lens, 3.5f);

    if (lit)
    {
        juce::ColourGradient glow(juce::Colour(0x50ff7a52), lens.getCentreX(), lens.getY(),
                                  juce::Colours::transparentBlack, lens.getCentreX(),
                                  lens.getBottom(), true);
        graphics.setGradientFill(glow);
        graphics.fillRoundedRectangle(lens.reduced(1.0f), 3.0f);
    }

    graphics.setColour(juce::Colours::white.withAlpha(lit ? 0.28f : 0.10f));
    graphics.drawLine(lens.getX() + 4.0f, lens.getY() + 2.0f,
                      lens.getRight() - 4.0f, lens.getY() + 2.0f, 1.2f);
    graphics.setColour(deepBlack.withAlpha(0.76f));
    graphics.drawRoundedRectangle(lens, 3.5f, 1.4f);

    graphics.setFont(condensedFont(juce::jmin(15.0f, lens.getHeight() * 0.34f), true));
    graphics.setColour(deepBlack.withAlpha(0.92f));
    graphics.drawFittedText(getButtonText(), lens.toNearestInt().translated(0, 1),
                            juce::Justification::centred, 1);
    graphics.setColour((lit ? juce::Colour(0xffffb08a) : juce::Colour(0xffad625a))
                           .withAlpha(lit ? 0.56f : 0.22f));
    graphics.drawFittedText(getButtonText(), lens.toNearestInt().translated(0, -1),
                            juce::Justification::centred, 1);
    graphics.setColour(deepBlack.withAlpha(0.86f));
    graphics.drawFittedText(getButtonText(), lens.toNearestInt(),
                            juce::Justification::centred, 1);
}

MassMeter::MassMeter(ReverbulatorAudioProcessor& owner) : processor(owner)
{
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void MassMeter::timerCallback()
{
    const float target = processor.getMassLevel();
    const float rate = target > displayedLevel ? 0.30f : 0.055f;
    displayedLevel += (target - displayedLevel) * rate;

    if (displayedLevel >= peakLevel)
    {
        peakLevel = displayedLevel;
        peakHoldFrames = 18;
    }
    else if (peakHoldFrames > 0)
        --peakHoldFrames;
    else
        peakLevel += (displayedLevel - peakLevel) * 0.028f;

    repaint();
}

void MassMeter::paint(juce::Graphics& graphics)
{
    auto outer = getLocalBounds().toFloat();
    graphics.setColour(deepBlack.withAlpha(0.86f));
    graphics.fillRoundedRectangle(outer.translated(4.0f, 6.0f), 8.0f);

    juce::ColourGradient frameGradient(juce::Colour(0xff4b4e4d), outer.getX(), outer.getY(),
                                       juce::Colour(0xff111313), outer.getRight(),
                                       outer.getBottom(), false);
    graphics.setGradientFill(frameGradient);
    graphics.fillRoundedRectangle(outer, 8.0f);
    graphics.setColour(juce::Colour(0xff676661).withAlpha(0.72f));
    graphics.drawRoundedRectangle(outer.reduced(1.0f), 7.0f, 1.0f);

    auto well = outer.reduced(11.0f);
    graphics.setColour(deepBlack);
    graphics.fillRoundedRectangle(well, 4.0f);

    auto glass = well.reduced(6.0f);
    juce::ColourGradient paper(juce::Colour(0xff623719), glass.getX(), glass.getCentreY(),
                               juce::Colour(0xff6b3d1d), glass.getRight(),
                               glass.getCentreY(), false);
    paper.addColour(0.18, juce::Colour(0xffa76029));
    paper.addColour(0.48, juce::Colour(0xffd49343));
    paper.addColour(0.68, juce::Colour(0xffc07a34));
    paper.addColour(0.86, juce::Colour(0xff945324));
    graphics.setGradientFill(paper);
    graphics.fillRoundedRectangle(glass, 2.0f);

    // The lamp is hottest in the centre and falls away before reaching the
    // bezel, which makes the printed card feel illuminated from behind.
    juce::ColourGradient illumination(juce::Colour(0x7affc668),
                                      glass.getCentreX(), glass.getCentreY(),
                                      juce::Colours::transparentBlack,
                                      glass.getRight() + glass.getWidth() * 0.10f,
                                      glass.getCentreY(), true);
    illumination.addColour(0.54, juce::Colour(0x42ef9c3f));
    graphics.setGradientFill(illumination);
    graphics.fillRoundedRectangle(glass.reduced(2.0f), 2.0f);

    // Deep ambient occlusion beneath the upper bezel and down the left wall.
    // These are deliberately asymmetric to establish a consistent light
    // source above and to the right of the unit.
    juce::ColourGradient topOcclusion(deepBlack.withAlpha(0.78f),
                                      glass.getCentreX(), glass.getY(),
                                      juce::Colours::transparentBlack,
                                      glass.getCentreX(), glass.getY() + 25.0f, false);
    graphics.setGradientFill(topOcclusion);
    graphics.fillRoundedRectangle(glass, 2.0f);

    juce::ColourGradient leftOcclusion(deepBlack.withAlpha(0.68f),
                                       glass.getX(), glass.getCentreY(),
                                       juce::Colours::transparentBlack,
                                       glass.getX() + 18.0f, glass.getCentreY(), false);
    graphics.setGradientFill(leftOcclusion);
    graphics.fillRoundedRectangle(glass, 2.0f);

    juce::ColourGradient edgeFalloff(juce::Colours::transparentBlack,
                                     glass.getCentreX(), glass.getBottom() - 22.0f,
                                     deepBlack.withAlpha(0.38f),
                                     glass.getCentreX(), glass.getBottom(), false);
    graphics.setGradientFill(edgeFalloff);
    graphics.fillRoundedRectangle(glass, 2.0f);

    const auto scale = glass.reduced(13.0f, 35.0f);
    const float rulerLeft = scale.getX() + 20.0f;
    graphics.setFont(condensedFont(9.0f, true));
    for (int tick = 0; tick <= 20; ++tick)
    {
        const float proportion = static_cast<float>(tick) / 20.0f;
        const float y = scale.getBottom() - proportion * scale.getHeight();
        const bool major = tick % 5 == 0;
        graphics.setColour(deepBlack.withAlpha(major ? 0.84f : 0.60f));
        graphics.drawLine(rulerLeft + (major ? 0.0f : 6.0f), y,
                          scale.getRight() - 2.0f, y, major ? 1.5f : 0.9f);
    }

    const juce::StringArray labels { "-60", "-40", "-20", "0" };
    for (int index = 0; index < labels.size(); ++index)
    {
        const float proportion = static_cast<float>(index) / 3.0f;
        const float y = scale.getBottom() - proportion * scale.getHeight();
        graphics.setColour(deepBlack.withAlpha(0.82f));
        graphics.drawText(labels[index], juce::roundToInt(glass.getX() + 2.0f),
                          juce::roundToInt(y - 6.0f), 28, 12, juce::Justification::centredLeft);
    }

    const float needleY = scale.getBottom() - displayedLevel * scale.getHeight();
    graphics.setColour(juce::Colour(0x40ff7b38));
    graphics.fillRect(scale.getX() - 3.0f, needleY - 3.0f, scale.getWidth() + 6.0f, 7.0f);
    graphics.setColour(juce::Colour(0xff6a160e));
    graphics.fillRoundedRectangle(rulerLeft, needleY - 1.4f,
                                  scale.getRight() - rulerLeft, 2.8f, 1.0f);

    const float peakY = scale.getBottom() - peakLevel * scale.getHeight();
    graphics.setColour(juce::Colour(0xffe9a344).withAlpha(0.86f));
    graphics.fillRect(scale.getRight() - 7.0f, peakY - 0.8f, 7.0f, 1.6f);

    graphics.setFont(condensedFont(13.0f, true));
    graphics.setColour(deepBlack.withAlpha(0.88f));
    graphics.drawText("MASS", glass.getX(), glass.getY() + 7.0f,
                      glass.getWidth(), 16.0f, juce::Justification::centred);
    graphics.setFont(condensedFont(7.5f, true));
    graphics.setColour(juce::Colour(0xff711910));
    graphics.drawText("CRITICAL", glass.getX(), glass.getY() + 23.0f,
                      glass.getWidth(), 12.0f, juce::Justification::centred);

    // A narrow reflection on the lit side is enough to read as glass without
    // turning the meter into a glossy modern display.
    juce::ColourGradient glassReflection(juce::Colours::transparentWhite,
                                         glass.getRight() - 13.0f, glass.getCentreY(),
                                         juce::Colours::white.withAlpha(0.16f),
                                         glass.getRight() - 3.0f, glass.getCentreY(), false);
    graphics.setGradientFill(glassReflection);
    graphics.fillRoundedRectangle(glass.reduced(3.0f), 1.5f);

    graphics.setColour(juce::Colours::white.withAlpha(0.13f));
    graphics.drawLine(glass.getRight() - 3.0f, glass.getY() + 10.0f,
                      glass.getRight() - 3.0f, glass.getBottom() - 10.0f, 0.9f);
    graphics.setColour(deepBlack.withAlpha(0.62f));
    graphics.drawLine(glass.getX() + 1.0f, glass.getY() + 5.0f,
                      glass.getX() + 1.0f, glass.getBottom() - 3.0f, 2.0f);
    graphics.drawLine(glass.getX() + 4.0f, glass.getY() + 1.0f,
                      glass.getRight() - 3.0f, glass.getY() + 1.0f, 2.2f);
    graphics.setColour(deepBlack.withAlpha(0.46f));
    graphics.drawRoundedRectangle(glass, 2.0f, 2.2f);
}

PresetStepper::PresetStepper(ReverbulatorAudioProcessor& owner)
    : processor(owner)
{
    setTooltip("Factory preset: upper/lower triangle steps through programs");
    startTimerHz(30);
}

void PresetStepper::selectRelative(int delta)
{
    const int next = juce::jlimit(0, processor.getNumPrograms() - 1,
                                  processor.getCurrentProgram() + delta);
    processor.setCurrentProgram(next);
}

void PresetStepper::mouseUp(const juce::MouseEvent& event)
{
    const float counterWidth = juce::jmin(34.0f,
        static_cast<float>(getWidth()) * 0.18f);
    const float counterX = (static_cast<float>(getWidth()) - counterWidth) * 0.5f;
    if (event.position.x < counterX
        || event.position.x > counterX + counterWidth)
        return;
    const float buttonHeight = getHeight() * 0.25f;
    if (event.position.y <= buttonHeight)
        selectRelative(1);
    else if (event.position.y >= getHeight() - buttonHeight)
        selectRelative(-1);
}

void PresetStepper::timerCallback()
{
    const int program = processor.getCurrentProgram();
    if (program != displayedProgram)
    {
        displayedProgram = program;
        displayedName = processor.getProgramName(program);
        nameHoldFrames = 48;
        nameAlpha = 1.0f;
    }
    else if (nameHoldFrames > 0)
        --nameHoldFrames;
    else
        nameAlpha = juce::jmax(0.0f, nameAlpha - 0.055f);
    repaint();
}

void PresetStepper::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    const float counterWidth = juce::jmin(34.0f, bounds.getWidth() * 0.18f);
    auto counter = juce::Rectangle<float>(
        bounds.getCentreX() - counterWidth * 0.5f, bounds.getY(),
        counterWidth, bounds.getHeight());
    const auto up = counter.removeFromTop(bounds.getHeight() * 0.25f);
    const auto down = counter.removeFromBottom(bounds.getHeight() * 0.25f);
    const auto digit = counter;

    const auto paintButton = [&graphics] (juce::Rectangle<float> area, bool upward)
    {
        graphics.setColour(deepBlack.withAlpha(0.92f));
        graphics.fillRoundedRectangle(area, 1.5f);
        graphics.setColour(juce::Colour(0xff373a3b));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 1.5f, 0.8f);
        juce::Path triangle;
        const auto centre = area.getCentre();
        const float halfWidth = juce::jmin(4.5f, area.getWidth() * 0.20f);
        const float halfHeight = juce::jmin(2.4f, area.getHeight() * 0.26f);
        if (upward)
        {
            triangle.startNewSubPath(centre.x, centre.y - halfHeight);
            triangle.lineTo(centre.x - halfWidth, centre.y + halfHeight);
            triangle.lineTo(centre.x + halfWidth, centre.y + halfHeight);
        }
        else
        {
            triangle.startNewSubPath(centre.x, centre.y + halfHeight);
            triangle.lineTo(centre.x - halfWidth, centre.y - halfHeight);
            triangle.lineTo(centre.x + halfWidth, centre.y - halfHeight);
        }
        triangle.closeSubPath();
        graphics.setColour(warmInk.withAlpha(0.64f));
        graphics.fillPath(triangle);
    };
    paintButton(up, true);
    paintButton(down, false);

    const bool modified = processor.isCurrentProgramModified();
    graphics.setColour(modified ? darkRed.darker(0.45f)
                                : juce::Colour(0xff080909));
    graphics.fillRect(digit);
    juce::ColourGradient drum(modified ? lampRed.darker(0.15f)
                                       : juce::Colour(0xff1b1c1c),
                              digit.getX(), digit.getY(),
                              modified ? darkRed.darker(0.62f)
                                       : juce::Colour(0xff050606), digit.getX(),
                              digit.getBottom(), false);
    drum.addColour(0.5, modified ? juce::Colour(0xff792522)
                                 : juce::Colour(0xff252625));
    graphics.setGradientFill(drum);
    graphics.fillRect(digit.reduced(1.0f, 0.0f));
    graphics.setColour(juce::Colours::white.withAlpha(0.08f));
    graphics.drawLine(digit.getX() + 2.0f, digit.getY() + 1.0f,
                      digit.getRight() - 2.0f, digit.getY() + 1.0f, 0.8f);
    graphics.setFont(condensedFont(digit.getHeight() * 0.88f, true));
    graphics.setColour(juce::Colour(0xffddd6c6).withAlpha(0.86f));
    graphics.drawText(juce::String(juce::jmax(0, displayedProgram)),
                      digit, juce::Justification::centred);
    graphics.setColour(deepBlack.withAlpha(0.90f));
    graphics.drawRect(digit, 1.0f);

    if (nameAlpha > 0.0f)
    {
        const auto nameArea = juce::Rectangle<float>(
            counter.getRight() + 10.0f, bounds.getY(),
            bounds.getRight() - counter.getRight() - 10.0f, bounds.getHeight());
        graphics.setFont(condensedFont(11.0f, true));
        graphics.setColour(warmInk.withAlpha(0.70f * nameAlpha));
        graphics.drawFittedText(displayedName, nameArea.toNearestInt(),
                                juce::Justification::centredLeft, 1);
    }
}

ReverbulatorAudioProcessorEditor::ReverbulatorAudioProcessorEditor(
    ReverbulatorAudioProcessor& owner)
    : AudioProcessorEditor(owner), processor(owner), massMeter(owner), presetStepper(owner)
{
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);
    setSize(620, 820);
    setResizable(true, true);
    setResizeLimits(465, 615, 775, 1025);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(620.0 / 820.0);

    configureMacro(mix, "MIX", "mix", mixAttachment);
    configureMacro(space, "SPACE", "space", spaceAttachment);
    configureMacro(motion, "MOTION", "motion", motionAttachment);
    configureMacro(chaos, "CHAOS", "chaos", chaosAttachment);

    addAndMakeVisible(body);
    addAndMakeVisible(vectorOrder);
    addAndMakeVisible(halo);
    addAndMakeVisible(massMeter);
    addAndMakeVisible(presetStepper);
    bodyAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "body", body);
    orderAttachment = std::make_unique<ButtonAttachment>(processor.parameters,
                                                          "vectorOrder", vectorOrder);
    haloAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "halo", halo);
    vectorOrder.setTooltip("VECTOR: Layer 1 to 3 to 2 to 4");
}

ReverbulatorAudioProcessorEditor::~ReverbulatorAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void ReverbulatorAudioProcessorEditor::configureMacro(
    juce::Slider& slider, const juce::String& name, const char* parameterId,
    std::unique_ptr<SliderAttachment>& attachment)
{
    slider.setName(name);
    slider.setTitle(name);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue(true,
        processor.parameters.getParameter(parameterId)->getDefaultValue());
    slider.setTooltip(name + " macro");
    addAndMakeVisible(slider);
    attachment = std::make_unique<SliderAttachment>(processor.parameters, parameterId, slider);
}

void ReverbulatorAudioProcessorEditor::drawScrew(juce::Graphics& graphics,
                                                  juce::Point<float> centre,
                                                  float radius, float angle) const
{
    graphics.setColour(deepBlack.withAlpha(0.75f));
    graphics.fillEllipse(centre.x - radius - 2.0f, centre.y - radius + 3.0f,
                         radius * 2.0f + 4.0f, radius * 2.0f + 4.0f);
    juce::ColourGradient metal(juce::Colour(0xff646767), centre.x - radius, centre.y - radius,
                               juce::Colour(0xff17191a), centre.x + radius,
                               centre.y + radius, false);
    graphics.setGradientFill(metal);
    graphics.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    graphics.setColour(deepBlack.withAlpha(0.86f));
    const auto direction = juce::Point<float>(std::cos(angle), std::sin(angle)) * radius * 0.58f;
    graphics.drawLine({ centre - direction, centre + direction }, 2.2f);
    graphics.setColour(juce::Colours::white.withAlpha(0.14f));
    juce::Path screwHighlight;
    screwHighlight.addCentredArc(centre.x, centre.y, radius - 1.0f, radius - 1.0f,
                                 0.0f, juce::MathConstants<float>::pi * 1.05f,
                                 juce::MathConstants<float>::pi * 1.72f, true);
    graphics.strokePath(screwHighlight, juce::PathStrokeType(1.0f));
}

void ReverbulatorAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient backdrop(juce::Colour(0xff050607), 0.0f, 0.0f,
                                  juce::Colour(0xff181b1d), bounds.getWidth(),
                                  bounds.getHeight(), false);
    backdrop.addColour(0.48, panelBlack);
    graphics.setGradientFill(backdrop);
    graphics.fillAll();

    auto face = bounds.reduced(bounds.getWidth() * 0.026f);
    graphics.setColour(deepBlack.withAlpha(0.92f));
    graphics.fillRoundedRectangle(face.translated(0.0f, bounds.getHeight() * 0.008f), 12.0f);
    juce::ColourGradient faceGradient(juce::Colour(0xff24282a), face.getX(), face.getY(),
                                      juce::Colour(0xff0b0d0e), face.getRight(),
                                      face.getBottom(), false);
    faceGradient.addColour(0.34, juce::Colour(0xff171a1c));
    graphics.setGradientFill(faceGradient);
    graphics.fillRoundedRectangle(face, 10.0f);

    // Fine deterministic stippling keeps the panel from reading as flat CGI.
    juce::Random random(0x53525334);
    for (int i = 0; i < 520; ++i)
    {
        const float px = face.getX() + random.nextFloat() * face.getWidth();
        const float py = face.getY() + random.nextFloat() * face.getHeight();
        graphics.setColour(juce::Colours::white.withAlpha(0.012f + random.nextFloat() * 0.018f));
        graphics.fillRect(px, py, 0.8f, 0.8f);
    }

    graphics.setColour(juce::Colours::white.withAlpha(0.11f));
    graphics.drawLine(face.getX() + 11.0f, face.getY() + 2.0f,
                      face.getRight() - 11.0f, face.getY() + 2.0f, 1.2f);
    graphics.drawLine(face.getX() + 2.0f, face.getY() + 11.0f,
                      face.getX() + 2.0f, face.getBottom() - 11.0f, 1.0f);
    graphics.setColour(deepBlack.withAlpha(0.76f));
    graphics.drawLine(face.getX() + 10.0f, face.getBottom() - 2.0f,
                      face.getRight() - 10.0f, face.getBottom() - 2.0f, 2.0f);
    graphics.drawLine(face.getRight() - 2.0f, face.getY() + 10.0f,
                      face.getRight() - 2.0f, face.getBottom() - 10.0f, 2.0f);

    const float scale = bounds.getWidth() / 620.0f;
    drawScrew(graphics, { 38.0f * scale, 38.0f * scale }, 9.0f * scale, 0.28f);
    drawScrew(graphics, { bounds.getRight() - 38.0f * scale, 38.0f * scale },
              9.0f * scale, 1.34f);
    drawScrew(graphics, { 38.0f * scale, bounds.getBottom() - 38.0f * scale },
              9.0f * scale, 2.52f);
    drawScrew(graphics, { bounds.getRight() - 38.0f * scale,
                           bounds.getBottom() - 38.0f * scale },
              9.0f * scale, 0.76f);

    // Hollow geometric logotype: the outlined glyphs evoke the continuous-
    // line display lettering found on late-70s laboratory instruments.
    juce::GlyphArrangement titleGlyphs;
    titleGlyphs.addFittedText(
        juce::Font(juce::FontOptions("Bahnschrift", "Light", 34.0f * scale)),
        "REVERBULATOR", 88.0f * scale, 39.0f * scale, 444.0f * scale,
        42.0f * scale, juce::Justification::centred, 1);
    juce::Path titlePath;
    titleGlyphs.createPath(titlePath);
    auto titleShadow = titlePath;
    titleShadow.applyTransform(juce::AffineTransform::translation(
        1.2f * scale, 2.0f * scale));
    graphics.setColour(deepBlack.withAlpha(0.88f));
    graphics.strokePath(titleShadow, juce::PathStrokeType(2.4f * scale));
    graphics.setColour(warmInk.withAlpha(0.90f));
    graphics.strokePath(titlePath, juce::PathStrokeType(1.25f * scale));
    auto titleHighlight = titlePath;
    titleHighlight.applyTransform(juce::AffineTransform::translation(
        -0.45f * scale, -0.55f * scale));
    graphics.setColour(juce::Colours::white.withAlpha(0.12f));
    graphics.strokePath(titleHighlight, juce::PathStrokeType(0.55f * scale));

    graphics.setFont(condensedFont(12.0f * scale, true));
    graphics.setColour(warmInk.withAlpha(0.53f));
    graphics.drawText("SRS-4  /  SERIAL REVERBULATOR SYSTEM", 0,
                      juce::roundToInt(82.0f * scale), getWidth(),
                      juce::roundToInt(18.0f * scale), juce::Justification::centred);

    const std::array<std::pair<juce::String, juce::Point<float>>, 4> labels {{
        { "MIX", { 154.0f, 358.0f } },
        { "SPACE", { 466.0f, 358.0f } },
        { "MOTION", { 154.0f, 642.0f } },
        { "CHAOS", { 466.0f, 642.0f } }
    }};
    graphics.setFont(condensedFont(17.0f * scale, true));
    for (const auto& label : labels)
    {
        const int width = juce::roundToInt(120.0f * scale);
        const int x = juce::roundToInt(label.second.x * scale) - width / 2;
        const int y = juce::roundToInt(label.second.y * scale);
        graphics.setColour(juce::Colour(0xff555148).withAlpha(0.45f));
        graphics.drawText(label.first, x - 1, y, width, juce::roundToInt(22.0f * scale),
                          juce::Justification::centred);
        graphics.drawText(label.first, x + 1, y, width, juce::roundToInt(22.0f * scale),
                          juce::Justification::centred);
        graphics.setColour(deepBlack.withAlpha(0.92f));
        graphics.drawText(label.first, x, y + 2, width, juce::roundToInt(22.0f * scale),
                          juce::Justification::centred);
        graphics.setColour(warmInk.withAlpha(0.76f));
        graphics.drawText(label.first, x, y, width, juce::roundToInt(22.0f * scale),
                          juce::Justification::centred);
    }

    graphics.setFont(condensedFont(8.0f * scale));
    graphics.setColour(warmInk.withAlpha(0.33f));
    graphics.drawText("MONKE MOUNTAIN AUDIO LABORATORY", 0,
                      juce::roundToInt(779.0f * scale), getWidth(),
                      juce::roundToInt(12.0f * scale), juce::Justification::centred);
}

void ReverbulatorAudioProcessorEditor::resized()
{
    const float sx = static_cast<float>(getWidth()) / 620.0f;
    const float sy = static_cast<float>(getHeight()) / 820.0f;
    const auto scaled = [sx, sy](float x, float y, float w, float h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                                    juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    mix.setBounds(scaled(72, 165, 164, 184));
    space.setBounds(scaled(384, 165, 164, 184));
    motion.setBounds(scaled(72, 449, 164, 184));
    chaos.setBounds(scaled(384, 449, 164, 184));
    massMeter.setBounds(scaled(256, 158, 108, 481));
    presetStepper.setBounds(scaled(0, 103, 620, 42));
    body.setBounds(scaled(74, 685, 132, 58));
    vectorOrder.setBounds(scaled(244, 685, 132, 58));
    halo.setBounds(scaled(414, 685, 132, 58));
}

