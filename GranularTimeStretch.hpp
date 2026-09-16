#pragma once

#include <algorithm>
#include <cmath>

namespace DrumCloudTimeStretch
{

static constexpr float kMinimumRatio = 0.25f;
static constexpr float kMaximumRatio = 4.0f;

inline float clampRatio(const float ratio) noexcept
{
    if (!std::isfinite(ratio)) return 1.0f;
    return std::clamp(ratio, kMinimumRatio, kMaximumRatio);
}

// A ratio above 1 makes the source timeline longer; a ratio below 1 makes it
// shorter. Grain pitch remains independent because this only changes the scan
// clock, never the per-grain sample increment.
inline float scaleScanRate(const float rate, const float ratio) noexcept
{
    return rate / clampRatio(ratio);
}

} // namespace DrumCloudTimeStretch
