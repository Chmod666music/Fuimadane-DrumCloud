#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DrumCloudTransients {

inline int detect(const float* left, const float* right, int32_t frames,
                  float sampleRate, int32_t* markers, int capacity,
                  float* strengths = nullptr) noexcept
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
    if (strengths != nullptr) strengths[0] = 1.0f;
    if (peak < 1.0e-6f || capacity == 1)
        return count;

    const float sr = std::max(8000.0f, sampleRate);
    const int32_t minimumDistance = std::max<int32_t>(1, int32_t(sr * 0.060f));
    int32_t lastMarker = 0;
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float averageNovelty = 0.0f;

    for (int32_t i = 0; i < frames; ++i)
    {
        const float mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        const float magnitude = std::fabs(mono);
        fastEnvelope += 0.12f * (magnitude - fastEnvelope);
        slowEnvelope += 0.002f * (magnitude - slowEnvelope);
        const float novelty = std::max(0.0f, fastEnvelope - slowEnvelope);
        averageNovelty += 0.001f * (novelty - averageNovelty);

        const float adaptiveThreshold = averageNovelty * 2.0f;
        const float absoluteThreshold = peak * 0.00035f;
        const bool prominent = fastEnvelope >= peak * 0.004f;
        if (prominent && novelty > std::max(adaptiveThreshold, absoluteThreshold) &&
            i - lastMarker >= minimumDistance)
        {
            const float strength = std::clamp(novelty / peak, 0.0f, 1.0f);
            int slot = -1;
            if (count < capacity)
            {
                slot = count++;
            }
            else if (capacity > 1 && strengths != nullptr)
            {
                int weakest = 1; // marker zero is the fixed region anchor
                for (int candidate = 2; candidate < count; ++candidate)
                    if (strengths[candidate] < strengths[weakest]) weakest = candidate;
                if (strength >= strengths[weakest]) slot = weakest;
            }

            if (slot >= 0)
            {
                markers[slot] = i;
                if (strengths != nullptr) strengths[slot] = strength;
            }
            lastMarker = i;

            // Suppress the same hit's immediate ringing without hiding the next beat.
            fastEnvelope = slowEnvelope;
        }
    }

    // Replacement above is strength-based; restore chronological playback order.
    for (int i = 2; i < count; ++i)
    {
        const int32_t marker = markers[i];
        const float strength = strengths != nullptr ? strengths[i] : 0.0f;
        int j = i;
        while (j > 1 && markers[j - 1] > marker)
        {
            markers[j] = markers[j - 1];
            if (strengths != nullptr) strengths[j] = strengths[j - 1];
            --j;
        }
        markers[j] = marker;
        if (strengths != nullptr) strengths[j] = strength;
    }

    return count;
}

} // namespace DrumCloudTransients
