#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

class PolyphonicNoteState
{
public:
    void reset() noexcept
    {
        fHeld.fill(false);
        fVelocity.fill(0);
        fReleaseLeft.fill(0);
        fSpawnPhase.fill(0.0f);
    }

    void clearSpawnPhases() noexcept { fSpawnPhase.fill(0.0f); }

    void noteOn(uint8_t note, uint8_t velocity) noexcept
    {
        note &= 0x7f;
        fHeld[note] = true;
        fVelocity[note] = velocity & 0x7f;
        fReleaseLeft[note] = 0;
        fSpawnPhase[note] = 0.0f;
    }

    void noteOff(uint8_t note, int32_t releaseSamples) noexcept
    {
        note &= 0x7f;
        fHeld[note] = false;
        fReleaseLeft[note] = std::max<int32_t>(0, releaseSamples);
    }

    bool isHeld(uint8_t note) const noexcept { return fHeld[note & 0x7f]; }
    bool isActive(uint8_t note) const noexcept
    {
        note &= 0x7f;
        return fHeld[note] || fReleaseLeft[note] > 0;
    }
    uint8_t velocity(uint8_t note) const noexcept { return fVelocity[note & 0x7f]; }
    int32_t releaseLeft(uint8_t note) const noexcept { return fReleaseLeft[note & 0x7f]; }

    float tailGain(uint8_t note, int32_t totalReleaseSamples) const noexcept
    {
        note &= 0x7f;
        if (fHeld[note]) return 1.0f;
        if (totalReleaseSamples <= 0 || fReleaseLeft[note] <= 0) return 0.0f;
        const float linear = std::clamp(float(fReleaseLeft[note]) / float(totalReleaseSamples), 0.0f, 1.0f);
        return std::sqrt(linear);
    }

    int advanceSpawn(uint8_t note, float increment) noexcept
    {
        note &= 0x7f;
        fSpawnPhase[note] += std::max(0.0f, increment);
        const int count = std::min(8, int(fSpawnPhase[note]));
        fSpawnPhase[note] -= float(count);
        return count;
    }

    void advanceRelease(uint8_t note) noexcept
    {
        note &= 0x7f;
        if (!fHeld[note] && fReleaseLeft[note] > 0) --fReleaseLeft[note];
    }

    bool anyActive() const noexcept
    {
        for (uint32_t note = 0; note < 128; ++note)
            if (fHeld[note] || fReleaseLeft[note] > 0) return true;
        return false;
    }

private:
    std::array<bool, 128> fHeld{};
    std::array<uint8_t, 128> fVelocity{};
    std::array<int32_t, 128> fReleaseLeft{};
    std::array<float, 128> fSpawnPhase{};
};
