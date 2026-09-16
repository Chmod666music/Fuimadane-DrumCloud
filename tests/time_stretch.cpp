#include "../GranularTimeStretch.hpp"

#include <cmath>
#include <cstdio>

static bool closeEnough(const float a, const float b)
{
    return std::fabs(a - b) < 0.00001f;
}

int main()
{
    using namespace DrumCloudTimeStretch;

    if (!closeEnough(scaleScanRate(1.0f, 1.0f), 1.0f)) return 1;
    if (!closeEnough(scaleScanRate(1.0f, 2.0f), 0.5f)) return 2;
    if (!closeEnough(scaleScanRate(1.0f, 0.5f), 2.0f)) return 3;
    if (!closeEnough(clampRatio(0.0f), kMinimumRatio)) return 4;
    if (!closeEnough(clampRatio(20.0f), kMaximumRatio)) return 5;
    if (!closeEnough(clampRatio(NAN), 1.0f)) return 6;

    std::puts("granular time-stretch scan-rate mapping passed");
    return 0;
}
