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

    const int32_t transients[] = { 0, 120, 275, 510, 880 };
    assert(transientSliceCount(100, 899, transients, 5, 8) == 5);
    assert(transientFrameRange(100, 899, transients, 5, 0, 8, start, end));
    assert(start == 100 && end == 119);
    assert(transientFrameRange(100, 899, transients, 5, 3, 8, start, end));
    assert(start == 510 && end == 879);
    assert(transientFrameRange(100, 899, transients, 5, 4, 8, start, end));
    assert(start == 880 && end == 899);
    assert(!transientFrameRange(100, 899, transients, 5, 5, 8, start, end));

    // A dense early cluster must not consume every available transient slice.
    const int32_t clustered[] = { 100, 120, 140, 160, 400, 700, 900 };
    const float strengths[] = { 0.5f, 0.9f, 0.8f, 0.7f, 0.6f, 1.0f, 0.55f };
    int32_t selected[4]{};
    const int selectedCount = selectStrongestSpacedMarkers(
        0, 1000, clustered, strengths, 7, 4, 100, selected, 4);
    assert(selectedCount == 4);
    assert(selected[0] == 120);
    assert(selected[1] == 400);
    assert(selected[2] == 700);
    assert(selected[3] == 900);

    std::puts("slicer MIDI mapping and frame boundaries passed");
    return 0;
}
