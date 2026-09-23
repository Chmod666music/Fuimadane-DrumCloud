#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DrumCloudTransients {

inline int detect(const float* left, const float* right, int32_t frames,
                  float sampleRate, int32_t* markers, int capacity) noexcept
{
    if (left == nullptr || markers == nullptr || frames <= 0 || capacity <= 0)
        return 0;

    float peak = 0.0f;
    for (int32_t i = 0; i < frames; ++i)
    {
        const float mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        peak = std::max(peak, std::fabs(mono));
    }

    int count = 1;
    markers[0] = 0;
    if (peak < 1.0e-6f || capacity == 1)
        return count;

    const float sr = std::max(8000.0f, sampleRate);
    const int32_t minimumDistance = std::max<int32_t>(1, int32_t(sr * 0.060f));
    int32_t lastMarker = 0;
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float averageNovelty = 0.0f;

    for (int32_t i = 0; i < frames && count < capacity; ++i)
    {
        const float mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        const float magnitude = std::fabs(mono);
        fastEnvelope += 0.12f * (magnitude - fastEnvelope);
        slowEnvelope += 0.002f * (magnitude - slowEnvelope);
        const float novelty = std::max(0.0f, fastEnvelope - slowEnvelope);
        averageNovelty += 0.001f * (novelty - averageNovelty);

        const float adaptiveThreshold = averageNovelty * 5.5f;
        const float absoluteThreshold = peak * 0.004f;
        const bool prominent = fastEnvelope >= peak * 0.025f;
        if (prominent && novelty > std::max(adaptiveThreshold, absoluteThreshold) &&
            i - lastMarker >= minimumDistance)
        {
            markers[count++] = i;
            lastMarker = i;

            // Suppress the same hit's immediate ringing without hiding the next beat.
            fastEnvelope = slowEnvelope;
        }
    }

    return count;
}

} // namespace DrumCloudTransients
