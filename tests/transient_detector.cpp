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
    assert(markers[1] <= 12050); // refine to the attack edge, not inside its peak
    int strongHits = 0;
    for (int i = 1; i < count; ++i)
        if (strengths[i] >= 0.10f) ++strongHits;
    assert(strongHits == 3);

    // A slower low-frequency attack must refine to its leading edge rather
    // than a quiet zero crossing inside the waveform.
    std::vector<float> slowAttack(48000, 0.0f);
    constexpr int slowStart = 12000;
    for (int i = 0; i < 4800; ++i)
    {
        const float rise = std::min(1.0f, float(i) / 1200.0f);
        const float decay = std::exp(-float(i) / 1800.0f);
        slowAttack[slowStart + i] = rise * decay * std::sin(float(i) * 0.008f);
    }
    int32_t slowMarkers[8]{};
    float slowStrengths[8]{};
    const int slowCount = DrumCloudTransients::detect(
        slowAttack.data(), slowAttack.data(), int32_t(slowAttack.size()),
        sampleRate, slowMarkers, 8, slowStrengths);
    assert(slowCount >= 2);
    assert(slowMarkers[1] >= slowStart - 500);
    assert(slowMarkers[1] <= slowStart + 250);

    // A tiny but extremely sharp click must not outrank a clearly louder
    // musical hit merely because its envelope rises faster.
    std::vector<float> variedHits(48000, 0.0f);
    for (int i = 0; i < 300 && 8000 + i < int(variedHits.size()); ++i)
        variedHits[8000 + i] = 0.08f * std::exp(-float(i) / 35.0f);
    for (int i = 0; i < 2400 && 24000 + i < int(variedHits.size()); ++i)
    {
        const float envelope = std::exp(-float(i) / 600.0f);
        variedHits[24000 + i] = 0.85f * envelope * std::sin(float(i) * 0.12f);
    }
    int32_t variedMarkers[8]{};
    float variedStrengths[8]{};
    const int variedCount = DrumCloudTransients::detect(
        variedHits.data(), variedHits.data(), int32_t(variedHits.size()),
        sampleRate, variedMarkers, 8, variedStrengths);
    assert(variedCount >= 3);
    int quietIndex = -1;
    int loudIndex = -1;
    for (int i = 1; i < variedCount; ++i)
    {
        if (std::abs(variedMarkers[i] - 8000) < 1000) quietIndex = i;
        if (std::abs(variedMarkers[i] - 24000) < 1000) loudIndex = i;
    }
    assert(quietIndex >= 0 && loudIndex >= 0);
    assert(variedStrengths[loudIndex] > variedStrengths[quietIndex] * 2.0f);

    // More candidates than capacity must still scan the complete sample.
    std::vector<float> dense(48000 * 12, 0.0f);
    for (int hit = 1; hit < 110; ++hit)
    {
        const int start = hit * 5000;
        if (start + 100 >= int(dense.size())) break;
        for (int i = 0; i < 100; ++i)
            dense[start + i] = 0.8f * std::exp(-float(i) / 18.0f);
    }
    int32_t limitedMarkers[16]{};
    float limitedStrengths[16]{};
    const int limitedCount = DrumCloudTransients::detect(
        dense.data(), dense.data(), int32_t(dense.size()), sampleRate,
        limitedMarkers, 16, limitedStrengths);
    assert(limitedCount == 16);
    assert(limitedMarkers[limitedCount - 1] > int32_t(dense.size() * 0.80f));
    std::puts("transient detector records strength and separates ghost hits");
    return 0;
}
