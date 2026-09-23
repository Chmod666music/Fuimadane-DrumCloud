#include "SliceMapping.hpp"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace DrumCloudSlicer;

    assert(sliceForMidiNote(35, 8) == -1);
    assert(sliceForMidiNote(36, 8) == 0);
    assert(sliceForMidiNote(43, 8) == 7);
    assert(sliceForMidiNote(44, 8) == -1);

    int32_t start = 0;
    int32_t end = 0;
    frameRange(100, 899, 0, 8, start, end);
    assert(start == 100 && end == 199);
    frameRange(100, 899, 7, 8, start, end);
    assert(start == 800 && end == 899);

    // Uneven regions remain contiguous and include every frame exactly once.
    int32_t previousEnd = 99;
    for (int slice = 0; slice < 8; ++slice)
    {
        frameRange(100, 902, slice, 8, start, end);
        assert(start == previousEnd + 1);
        assert(end >= start);
        previousEnd = end;
    }
    assert(previousEnd == 902);

    std::puts("slicer MIDI mapping and frame boundaries passed");
    return 0;
}
