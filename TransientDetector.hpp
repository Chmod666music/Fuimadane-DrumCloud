#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DrumCloudTransients {

inline int32_t refineOnset(const float* left, const float* right, int32_t detected,
                           int32_t frames, float sampleRate, float referenceLevel,
                           float globalPeak) noexcept
{
    // Multi-millisecond blocks do not mistake a low-frequency zero crossing
    // inside a kick/bass transient for silence. The longer look-back also
    // reaches the beginning of slower attacks.
    const int32_t block = std::max<int32_t>(8, int32_t(sampleRate * 0.003f));
    const int32_t search = std::max<int32_t>(block, int32_t(sampleRate * 0.100f));
    const int32_t first = std::max<int32_t>(0, detected - search);
    const float quietThreshold = std::max(globalPeak * 0.0002f, referenceLevel * 0.10f);
    int32_t quietEnd = -1;
    int32_t minimumEnd = detected;
    float minimumEnergy = 1.0e30f;

    for (int32_t end = detected; end > first; end -= block)
    {
        const int32_t begin = std::max(first, end - block);
        float energy = 0.0f;
        for (int32_t i = begin; i < end; ++i)
        {
            const float mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            energy += std::fabs(mono);
        }
        energy /= float(std::max<int32_t>(1, end - begin));
        if (energy < minimumEnergy)
        {
            minimumEnergy = energy;
            minimumEnd = end;
        }
        if (energy <= quietThreshold)
        {
            quietEnd = end;
            break;
        }
    }

    // One block of pre-roll avoids trimming the first cycle of the attack.
    const int32_t onset = (quietEnd >= 0 ? quietEnd : minimumEnd) - block;
    return std::clamp<int32_t>(onset, 0, std::max<int32_t>(0, frames - 1));
}

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
            // Novelty alone overvalues tiny, click-like attacks and can make a
            // quiet tick outrank the main drum hit. Measure the event's actual
            // peak around the threshold crossing as well. Peak level carries
            // most of the musical-importance score, while novelty still helps
            // distinguish a real new onset from a loud sustained tail.
            const int32_t peakLookBehind = std::max<int32_t>(1, int32_t(sr * 0.010f));
            const int32_t peakLookAhead = std::max<int32_t>(1, int32_t(sr * 0.080f));
            const int32_t peakFirst = std::max<int32_t>(0, i - peakLookBehind);
            const int32_t peakLast = std::min<int32_t>(frames, i + peakLookAhead);
            float eventPeak = 0.0f;
            for (int32_t peakFrame = peakFirst; peakFrame < peakLast; ++peakFrame)
            {
                const float peakMono = right != nullptr
                    ? 0.5f * (left[peakFrame] + right[peakFrame])
                    : left[peakFrame];
                eventPeak = std::max(eventPeak, std::fabs(peakMono));
            }
            const float noveltyStrength = std::clamp(novelty / peak, 0.0f, 1.0f);
            const float peakStrength = std::clamp(eventPeak / peak, 0.0f, 1.0f);
            const float strength = 0.35f * noveltyStrength + 0.65f * peakStrength;
            const int32_t onset = refineOnset(left, right, i, frames, sr,
                                              fastEnvelope, peak);
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
                markers[slot] = onset;
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
