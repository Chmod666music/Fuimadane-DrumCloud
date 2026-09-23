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
    const int count = DrumCloudTransients::detect(
        audio.data(), audio.data(), frames, sampleRate, markers, 16);

    assert(count == 4); // region start plus three musical hits
    assert(std::abs(markers[1] - 12000) < 500);
    assert(std::abs(markers[2] - 36000) < 500);
    assert(std::abs(markers[3] - 65000) < 500);
    std::puts("transient detector rejects low-level ghost hits");
    return 0;
}
