#include "../PolyphonicNoteState.hpp"

#include <cstdio>

int main()
{
    PolyphonicNoteState notes;
    notes.reset();

    constexpr uint8_t g = 67;
    constexpr uint8_t d = 62;
    notes.noteOn(g, 100);
    if (!notes.isHeld(g) || notes.velocity(g) != 100) return 1;

    notes.noteOn(d, 90);
    if (!notes.isHeld(g) || !notes.isHeld(d)) return 2;

    notes.noteOff(d, 4800);
    if (!notes.isHeld(g) || notes.isHeld(d) || !notes.isActive(d)) return 3;

    // Releasing D must never steal or silence the still-held G.
    for (int i = 0; i < 4800; ++i) notes.advanceRelease(d);
    if (!notes.isHeld(g) || notes.isActive(d)) return 4;

    const int spawnsG = notes.advanceSpawn(g, 2.25f);
    const int spawnsD = notes.advanceSpawn(d, 1.25f);
    if (spawnsG != 2 || spawnsD != 1) return 5;

    notes.noteOff(g, 100);
    if (!notes.anyActive()) return 6;
    for (int i = 0; i < 100; ++i) notes.advanceRelease(g);
    if (notes.anyActive()) return 7;

    std::puts("polyphonic G -> D -> release D sequence passed");
    return 0;
}
