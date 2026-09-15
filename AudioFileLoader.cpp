#include "AudioFileLoader.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>

#define DR_WAV_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_mp3.h"

// At most 8 million frames and 16 million decoded float samples (64 MiB).
// Streaming reads avoid decoding an untrusted compressed file into an unbounded allocation.
static constexpr uint64_t kMaxFrames = 8000000;
static constexpr uint64_t kMaxDecodedFloats = 16000000;
static constexpr uint64_t kChunkFrames = 4096;

static std::string lowerExt(const char* path)
{
    std::string s(path ? path : "");
    const auto p = s.find_last_of('.');
    if (p == std::string::npos) return "";
    std::string ext = s.substr(p + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    return ext;
}

template <typename Read>
static bool readBounded(LoadedAudio& out, uint32_t channels, uint32_t sampleRate,
                        uint64_t knownFrames, Read read, std::string* err)
{
    if (channels < 1 || channels > 8 || sampleRate < 1 || sampleRate > 384000)
    {
        if (err) *err = "invalid channel count or sample rate";
        return false;
    }
    const uint64_t limit = std::min(kMaxFrames, kMaxDecodedFloats / channels);
    if (knownFrames > limit)
    {
        if (err) *err = "sample exceeds the decoded size limit";
        return false;
    }
    out.channels = channels;
    out.sampleRate = sampleRate;
    uint64_t frames = 0;
    while (frames < limit)
    {
        const uint64_t request = std::min(kChunkFrames, limit - frames);
        const size_t offset = static_cast<size_t>(frames * channels);
        out.interleaved.resize(offset + static_cast<size_t>(request * channels));
        const uint64_t got = read(request, out.interleaved.data() + offset);
        if (got > request)
        {
            if (err) *err = "decoder returned too many frames";
            return false;
        }
        frames += got;
        out.interleaved.resize(static_cast<size_t>(frames * channels));
        if (got < request) break;
    }
    if (frames == limit)
    {
        float extra[8]{};
        if (read(1, extra) != 0)
        {
            if (err) *err = "sample exceeds the decoded size limit";
            return false;
        }
    }
    if (frames == 0)
    {
        if (err) *err = "sample has no audio frames";
        return false;
    }
    out.frames = frames;
    return true;
}

bool loadAudioFileToFloat(const char* path, LoadedAudio& out, std::string* err)
{
    out = LoadedAudio{};
    if (!path || !path[0])
    {
        if (err) *err = "empty path";
        return false;
    }

    const std::string ext = lowerExt(path);
    if (ext == "wav")
    {
        drwav wav{};
        if (!drwav_init_file(&wav, path, nullptr))
        {
            if (err) *err = "dr_wav failed";
            return false;
        }
        const bool ok = readBounded(out, wav.channels, wav.sampleRate, wav.totalPCMFrameCount,
            [&wav](uint64_t n, float* p){ return drwav_read_pcm_frames_f32(&wav, n, p); }, err);
        drwav_uninit(&wav);
        return ok;
    }
    if (ext == "flac")
    {
        drflac* flac = drflac_open_file(path, nullptr);
        if (!flac)
        {
            if (err) *err = "dr_flac failed";
            return false;
        }
        const bool ok = readBounded(out, flac->channels, flac->sampleRate, flac->totalPCMFrameCount,
            [flac](uint64_t n, float* p){ return drflac_read_pcm_frames_f32(flac, n, p); }, err);
        drflac_close(flac);
        return ok;
    }
    if (ext == "mp3")
    {
        drmp3 mp3{};
        if (!drmp3_init_file(&mp3, path, nullptr))
        {
            if (err) *err = "dr_mp3 failed";
            return false;
        }
        const bool ok = readBounded(out, mp3.channels, mp3.sampleRate, 0,
            [&mp3](uint64_t n, float* p){ return drmp3_read_pcm_frames_f32(&mp3, n, p); }, err);
        drmp3_uninit(&mp3);
        return ok;
    }
    if (err) *err = "unsupported type";
    return false;
}
