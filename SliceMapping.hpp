#pragma once

#include <algorithm>
#include <cstdint>

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

} // namespace DrumCloudSlicer
