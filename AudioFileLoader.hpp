#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct LoadedAudio
{
    std::vector<float> interleaved; // frames * channels
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
};

bool loadAudioFileToFloat(const char* path, LoadedAudio& out, std::string* err = nullptr);

struct AudioPreview
{
    static constexpr uint32_t kBins = 1024;
    float min[kBins]{};
    float max[kBins]{};
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
};

// Streams only a small decoder block at a time; does not retain the PCM.
bool loadAudioFilePreview(const char* path, AudioPreview& out, std::string* err = nullptr);
