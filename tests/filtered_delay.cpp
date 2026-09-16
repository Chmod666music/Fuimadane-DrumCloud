#include "../FilteredStereoDelay.hpp"

#include <cmath>
#include <cstdio>

int main()
{
    constexpr float sr = 48000.0f;
    FilteredStereoDelay delay;
    delay.init(sr);

    delay.setParameters(0, 10.0f, 20.0f, 0.0f, 1.0f, 0.0f);
    float l = 0.7f, r = -0.3f;
    delay.process(l, r);
    if (std::fabs(l - 0.7f) > 1.0e-6f || std::fabs(r + 0.3f) > 1.0e-6f) return 1;

    delay.reset();
    delay.setParameters(1, 10.0f, 20.0f, 0.0f, 1.0f, 0.0f);
    float peakL = 0.0f, peakR = 0.0f;
    int peakAtL = -1, peakAtR = -1;
    for (int i = 0; i < 1200; ++i)
    {
        l = i == 0 ? 1.0f : 0.0f;
        r = i == 0 ? 1.0f : 0.0f;
        delay.process(l, r);
        if (std::fabs(l) > peakL) { peakL = std::fabs(l); peakAtL = i; }
        if (std::fabs(r) > peakR) { peakR = std::fabs(r); peakAtR = i; }
    }
    std::printf("stereo peaks L=%d/%.3f R=%d/%.3f\n", peakAtL, peakL, peakAtR, peakR);
    if (std::abs(peakAtL - 480) > 1 || std::abs(peakAtR - 960) > 1) return 2;

    delay.reset();
    delay.setParameters(2, 10.0f, 10.0f, 0.6f, 1.0f, 0.0f);
    float firstLeft = 0.0f, secondRight = 0.0f;
    for (int i = 0; i < 1100; ++i)
    {
        l = i == 0 ? 1.0f : 0.0f;
        r = 0.0f;
        delay.process(l, r);
        if (i == 480) firstLeft = l;
        if (i == 960) secondRight = r;
    }
    std::printf("ping firstL=%.3f secondR=%.3f\n", firstLeft, secondRight);
    if (firstLeft < 0.70f || secondRight < 0.35f) return 3;
    return 0;
}
