#include "TransientDetector.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr float sampleRate = 48000.0f;
    constexpr int frames = 96000;
    std::vector<float> audio(frames, 0.0f);

    auto addHit = [&](int start, float amplitude)
    {
        for (int i = 0; i < 1800 && start + i < frames; ++i)
        {
            const float envelope = std::exp(-float(i) / 260.0f);
            audio[start + i] += amplitude * envelope * std::sin(float(i) * 0.31f);
        }
    };

    addHit(12000, 1.0f);
    addHit(14500, 0.015f); // ringing/ghost hit: must be ignored
    addHit(36000, 0.72f);
    addHit(39000, 0.02f);  // another small false-transient candidate
    addHit(65000, 0.85f);

    int32_t markers[16]{};
    float strengths[16]{};
    const int count = DrumCloudTransients::detect(
        audio.data(), audio.data(), frames, sampleRate, markers, 16, strengths);

    assert(count >= 4);
    assert(std::abs(markers[1] - 12000) < 500);
    int strongHits = 0;
    for (int i = 1; i < count; ++i)
        if (strengths[i] >= 0.012f) ++strongHits;
    assert(strongHits == 3);
    std::puts("transient detector records strength and separates ghost hits");
    return 0;
}
