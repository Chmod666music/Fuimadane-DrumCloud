#include "AudioFileLoader.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

#define DR_WAV_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "thirdparty/dr_libs/dr_mp3.h"

// At most 16 million frames and 32 million decoded float samples (128 MiB).
// Streaming reads avoid decoding an untrusted compressed file into an unbounded allocation.
static constexpr uint64_t kMaxFrames = 16000000;
static constexpr uint64_t kMaxDecodedFloats = 32000000;
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
        const std::unique_ptr<drwav, void(*)(drwav*)> close(&wav,
            [](drwav* p){ drwav_uninit(p); });
        return readBounded(out, wav.channels, wav.sampleRate, wav.totalPCMFrameCount,
            [&wav](uint64_t n, float* p){ return drwav_read_pcm_frames_f32(&wav, n, p); }, err);
    }
    if (ext == "flac")
    {
        drflac* flac = drflac_open_file(path, nullptr);
        if (!flac)
        {
            if (err) *err = "dr_flac failed";
            return false;
        }
        const std::unique_ptr<drflac, void(*)(drflac*)> close(flac, drflac_close);
        return readBounded(out, flac->channels, flac->sampleRate, flac->totalPCMFrameCount,
            [flac](uint64_t n, float* p){ return drflac_read_pcm_frames_f32(flac, n, p); }, err);
    }
    if (ext == "mp3")
    {
        drmp3 mp3{};
        if (!drmp3_init_file(&mp3, path, nullptr))
        {
            if (err) *err = "dr_mp3 failed";
            return false;
        }
        const std::unique_ptr<drmp3, void(*)(drmp3*)> close(&mp3, drmp3_uninit);
        return readBounded(out, mp3.channels, mp3.sampleRate, 0,
            [&mp3](uint64_t n, float* p){ return drmp3_read_pcm_frames_f32(&mp3, n, p); }, err);
    }
    if (err) *err = "unsupported type";
    return false;
}

template <typename Read>
static bool streamPreview(AudioPreview& out, uint32_t channels, uint32_t sampleRate,
                          uint64_t totalFrames, Read read, std::string* err)
{
    if (channels < 1 || channels > 8 || sampleRate < 1 || sampleRate > 384000)
    {
        if (err) *err = "invalid channel count or sample rate";
        return false;
    }
    const uint64_t limit = std::min(kMaxFrames, kMaxDecodedFloats / channels);
    if (totalFrames > limit)
    {
        if (err) *err = "sample exceeds the decoded size limit";
        return false;
    }
    if (totalFrames == 0)
    {
        if (err) *err = "sample has no audio frames";
        return false;
    }
    for (uint32_t i = 0; i < AudioPreview::kBins; ++i)
    {
        out.min[i] = 1.0f;
        out.max[i] = -1.0f;
    }
    std::vector<float> block(static_cast<size_t>(kChunkFrames * channels));
    uint64_t frames = 0;
    while (frames < limit)
    {
        const uint64_t requested = std::min(kChunkFrames, limit - frames);
        const uint64_t got = read(requested, block.data());
        if (got > requested)
        {
            if (err) *err = "decoder returned too many frames";
            return false;
        }
        for (uint64_t i = 0; i < got; ++i)
        {
            const uint64_t frame = frames + i;
            // Frame limits are well below UINT64_MAX, so multiplication cannot overflow.
            const uint32_t bin = static_cast<uint32_t>(
                std::min<uint64_t>(AudioPreview::kBins - 1,
                    (frame * AudioPreview::kBins) / totalFrames));
            const float* pcm = block.data() + static_cast<size_t>(i * channels);
            float best = pcm[0];
            for (uint32_t ch = 1; ch < channels; ++ch)
                if (std::abs(pcm[ch]) > std::abs(best)) best = pcm[ch];
            out.min[bin] = std::min(out.min[bin], best);
            out.max[bin] = std::max(out.max[bin], best);
        }
        frames += got;
        if (got < requested) break;
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
    if (!frames)
    {
        if (err) *err = "sample has no audio frames";
        return false;
    }
    for (uint32_t i = 0; i < AudioPreview::kBins; ++i)
    {
        if (out.min[i] > out.max[i]) out.min[i] = out.max[i] = 0.0f;
        out.min[i] = std::clamp(out.min[i], -1.0f, 1.0f);
        out.max[i] = std::clamp(out.max[i], -1.0f, 1.0f);
    }
    out.channels = channels;
    out.sampleRate = sampleRate;
    out.frames = frames;
    return true;
}

bool loadAudioFilePreview(const char* path, AudioPreview& out, std::string* err)
{
    out = AudioPreview{};
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
        const std::unique_ptr<drwav, void(*)(drwav*)> close(&wav,
            [](drwav* p){ drwav_uninit(p); });
        return streamPreview(out, wav.channels, wav.sampleRate, wav.totalPCMFrameCount,
            [&wav](uint64_t n, float* p){ return drwav_read_pcm_frames_f32(&wav, n, p); }, err);
    }
    if (ext == "flac")
    {
        drflac* flac = drflac_open_file(path, nullptr);
        if (!flac)
        {
            if (err) *err = "dr_flac failed";
            return false;
        }
        const std::unique_ptr<drflac, void(*)(drflac*)> close(flac, drflac_close);
        return streamPreview(out, flac->channels, flac->sampleRate,
            flac->totalPCMFrameCount,
            [flac](uint64_t n, float* p){ return drflac_read_pcm_frames_f32(flac, n, p); }, err);
    }
    if (ext == "mp3")
    {
        drmp3 mp3{};
        if (!drmp3_init_file(&mp3, path, nullptr))
        {
            if (err) *err = "dr_mp3 failed";
            return false;
        }
        const std::unique_ptr<drmp3, void(*)(drmp3*)> close(&mp3, drmp3_uninit);
        const uint64_t count = drmp3_get_pcm_frame_count(&mp3);
        // Counting unindexed MP3s may move the file cursor; seek back before decoding.
        if (count && !drmp3_seek_to_pcm_frame(&mp3, 0))
        {
            if (err) *err = "dr_mp3 seek failed";
            return false;
        }
        return streamPreview(out, mp3.channels, mp3.sampleRate, count,
            [&mp3](uint64_t n, float* p){ return drmp3_read_pcm_frames_f32(&mp3, n, p); }, err);
    }
    if (err) *err = "unsupported type";
    return false;
}
