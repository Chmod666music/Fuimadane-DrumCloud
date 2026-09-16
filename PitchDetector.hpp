#pragma once

#include <cstdint>

struct PitchDetectionResult
{
    bool valid = false;
    int midiNote = 60;
    float fineTuneCents = 0.0f;
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
};

PitchDetectionResult detectSamplePitch(const float* left,
                                       const float* right,
                                       uint32_t frames,
                                       uint32_t sampleRate);
