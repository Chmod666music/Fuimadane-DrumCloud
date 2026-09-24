#pragma once

#include <atomic>
#include <cstdint>

// Read by this plugin instance's UI; written by its DSP instance.
// Atomic fields allow the audio thread to publish snapshots without locks.
struct DrumCloudTelemetry
{
    static constexpr uint32_t kGrainCount = 16;
    static constexpr uint32_t kBoundaryCount = 17;

    std::atomic<float> scanPos{0.0f};
    std::atomic<int> scanMode{0};
    std::atomic<uint32_t> grainCount{0};
    std::atomic<float> grainPos[kGrainCount]{};
    std::atomic<int> activeSlice{-1};
    std::atomic<uint32_t> sliceBoundaryCount{0};
    std::atomic<float> sliceBoundaries[kBoundaryCount]{};
    std::atomic<uint32_t> pitchGeneration{0};
    std::atomic<int> detectedRoot{-1};
    std::atomic<float> detectedFine{0.0f};
    std::atomic<float> detectedConfidence{0.0f};
};

namespace DISTRHO {
DrumCloudTelemetry* getDrumCloudTelemetry(void* pluginInstance) noexcept;
}
