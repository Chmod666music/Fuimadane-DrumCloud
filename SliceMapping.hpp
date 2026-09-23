#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace DrumCloudSlicer {

static constexpr int kBaseMidiNote = 36;
static constexpr int kMinSliceCount = 2;
static constexpr int kMaxSliceCount = 16;

inline int clampSliceCount(int count) noexcept
{
    return std::clamp(count, kMinSliceCount, kMaxSliceCount);
}

inline int sliceForMidiNote(int midiNote, int count) noexcept
{
    count = clampSliceCount(count);
    const int slice = midiNote - kBaseMidiNote;
    return slice >= 0 && slice < count ? slice : -1;
}

inline int selectStrongestSpacedMarkers(int32_t regionStart, int32_t regionEnd,
                                        const int32_t* markers, const float* strengths,
                                        int markerCount, int wanted,
                                        int32_t minimumDistance,
                                        int32_t* selected, int capacity) noexcept
{
    if (markers == nullptr || strengths == nullptr || selected == nullptr ||
        markerCount <= 0 || wanted <= 0 || capacity <= 0 || regionEnd <= regionStart)
        return 0;

    wanted = std::min(wanted, capacity);
    minimumDistance = std::max<int32_t>(1, minimumDistance);
    int count = 0;

    // Choose by musical importance first, while preventing one dense attack
    // cluster from consuming every available slice.
    while (count < wanted)
    {
        int best = -1;
        float bestStrength = -1.0f;
        for (int i = 0; i < markerCount; ++i)
        {
            const int32_t marker = markers[i];
            if (marker <= regionStart || marker >= regionEnd)
                continue;

            bool farEnough = true;
            for (int j = 0; j < count; ++j)
            {
                if (std::abs(marker - selected[j]) < minimumDistance)
                {
                    farEnough = false;
                    break;
                }
            }
            if (farEnough && strengths[i] > bestStrength)
            {
                best = i;
                bestStrength = strengths[i];
            }
        }
        if (best < 0)
            break;
        selected[count++] = markers[best];
    }

    std::sort(selected, selected + count);
    return count;
}

inline void frameRange(int32_t regionStart, int32_t regionEnd,
                       int slice, int count,
                       int32_t& sliceStart, int32_t& sliceEnd) noexcept
{
    count = clampSliceCount(count);
    slice = std::clamp(slice, 0, count - 1);
    regionEnd = std::max(regionStart, regionEnd);
    const int32_t length = regionEnd - regionStart + 1;
    sliceStart = regionStart + int32_t((int64_t(length) * slice) / count);
    sliceEnd = regionStart + int32_t((int64_t(length) * (slice + 1)) / count) - 1;
    sliceEnd = std::clamp(sliceEnd, sliceStart, regionEnd);
}

inline int buildTransientBoundaries(int32_t regionStart, int32_t regionEnd,
                                    const int32_t* markers, int markerCount,
                                    int maxSlices, int32_t* boundaries,
                                    int capacity) noexcept
{
    if (boundaries == nullptr || capacity < 2 || regionEnd < regionStart)
        return 0;

    maxSlices = std::min(clampSliceCount(maxSlices), capacity - 1);
    int count = 1;
    boundaries[0] = regionStart;

    if (markers != nullptr)
    {
        for (int i = 0; i < markerCount && count < maxSlices; ++i)
        {
            const int32_t marker = markers[i];
            if (marker <= regionStart || marker >= regionEnd)
                continue;
            if (marker > boundaries[count - 1])
                boundaries[count++] = marker;
        }
    }

    boundaries[count++] = regionEnd + 1; // exclusive final boundary
    return count;
}

inline int transientSliceCount(int32_t regionStart, int32_t regionEnd,
                               const int32_t* markers, int markerCount,
                               int maxSlices) noexcept
{
    int32_t boundaries[kMaxSliceCount + 1]{};
    const int count = buildTransientBoundaries(regionStart, regionEnd, markers,
                                               markerCount, maxSlices,
                                               boundaries, kMaxSliceCount + 1);
    return std::max(0, count - 1);
}

inline bool transientFrameRange(int32_t regionStart, int32_t regionEnd,
                                const int32_t* markers, int markerCount,
                                int slice, int maxSlices,
                                int32_t& sliceStart, int32_t& sliceEnd) noexcept
{
    int32_t boundaries[kMaxSliceCount + 1]{};
    const int count = buildTransientBoundaries(regionStart, regionEnd, markers,
                                               markerCount, maxSlices,
                                               boundaries, kMaxSliceCount + 1);
    const int slices = count - 1;
    if (slice < 0 || slice >= slices)
        return false;
    sliceStart = boundaries[slice];
    sliceEnd = boundaries[slice + 1] - 1;
    return true;
}

} // namespace DrumCloudSlicer
