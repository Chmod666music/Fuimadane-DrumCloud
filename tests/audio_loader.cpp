#include "../AudioFileLoader.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

static void u16(std::ofstream& f, uint16_t v)
{
    f.put(char(v)); f.put(char(v >> 8));
}
static void u32(std::ofstream& f, uint32_t v)
{
    for (int i = 0; i < 4; ++i) f.put(char(v >> (i * 8)));
}
static void wav(const char* path, uint32_t frames, bool sparse)
{
    std::ofstream f(path, std::ios::binary);
    const uint32_t bytes = frames * 4; // stereo, signed 16-bit
    f.write("RIFF", 4); u32(f, 36 + bytes); f.write("WAVEfmt ", 8);
    u32(f, 16); u16(f, 1); u16(f, 2); u32(f, 48000);
    u32(f, 192000); u16(f, 4); u16(f, 16);
    f.write("data", 4); u32(f, bytes);
    if (sparse)
    {
        f.seekp(static_cast<std::streamoff>(bytes) - 1, std::ios::cur);
        f.put(0);
    }
    else
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            u16(f, i == 0 ? 16384 : 0);
            u16(f, i == 0 ? 49152 : 0);
        }
    }
}

int main()
{
    wav("small.wav", 4, false);
    wav("formerly_too_long.wav", 9000000, true);
    wav("oversized.wav", 16000001, true);
    LoadedAudio audio;
    std::string error;
    assert(loadAudioFileToFloat("small.wav", audio, &error));
    assert(audio.frames == 4 && audio.channels == 2 && audio.sampleRate == 48000);
    assert(audio.interleaved.size() == 8);
    assert(std::fabs(audio.interleaved[0] - 0.5f) < 0.001f);
    assert(std::fabs(audio.interleaved[1] + 0.5f) < 0.001f);
    AudioPreview preview;
    assert(loadAudioFilePreview("small.wav", preview, &error));
    assert(preview.frames == 4 && preview.channels == 2);
    assert(std::fabs(preview.max[0] - 0.5f) < 0.001f);
    assert(loadAudioFileToFloat("formerly_too_long.wav", audio, &error));
    assert(audio.frames == 9000000);
    assert(loadAudioFilePreview("formerly_too_long.wav", preview, &error));
    assert(preview.frames == 9000000);
    assert(!loadAudioFileToFloat("oversized.wav", audio, &error));
    assert(error.find("limit") != std::string::npos);
    assert(!loadAudioFilePreview("oversized.wav", preview, &error));
    assert(error.find("limit") != std::string::npos);
    assert(loadAudioFilePreview("codec.flac", preview, &error));
    assert(preview.channels == 2 && preview.frames > 40000);
    assert(loadAudioFileToFloat("codec.flac", audio, &error));
    assert(loadAudioFilePreview("codec.mp3", preview, &error));
    assert(preview.channels == 2 && preview.frames > 40000);
    assert(loadAudioFileToFloat("codec.mp3", audio, &error));
    assert(!loadAudioFileToFloat("missing.wav", audio, &error));
    {
        std::ofstream broken("broken.wav", std::ios::binary);
        broken.write("not a wave", 10);
    }
    assert(!loadAudioFileToFloat("broken.wav", audio, &error));
    assert(!loadAudioFilePreview("broken.wav", preview, &error));
    std::remove("broken.wav");
    std::remove("small.wav");
    std::remove("formerly_too_long.wav");
    std::remove("oversized.wav");
}
