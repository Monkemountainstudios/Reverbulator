#pragma once

#include <JuceHeader.h>

namespace SerialRouting
{
inline float networkActivationForSpace(float space) noexcept
{
    const float position = juce::jlimit(0.0f, 1.0f, space / 0.05f);
    return position * position * (3.0f - 2.0f * position);
}

inline float layer1LiveInputGain(float space) noexcept
{
    const float handoff = juce::jlimit(0.0f, 1.0f, (space - 0.40f) / 0.60f);
    return 1.0f - 0.50f * handoff;
}

inline float layer2DirectInputGain(float space) noexcept
{
    const float position = juce::jlimit(0.0f, 1.0f,
        (space - 0.55f) / 0.45f);
    const float highSpaceLift = position * position * (3.0f - 2.0f * position);
    return 1.0f - layer1LiveInputGain(space) + 0.08f * highSpaceLift;
}

inline float vectorSpectralTailGain(float space) noexcept
{
    const float position = juce::jlimit(0.0f, 1.0f,
        (space - 0.90f) / 0.10f);
    const float eventHorizon = position * position * (3.0f - 2.0f * position);
    return 0.14f * eventHorizon;
}
}
