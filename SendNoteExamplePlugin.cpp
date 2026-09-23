/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2024 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _USE_MATH_DEFINES
#include "DistrhoPluginInfo.h"
#include "DistrhoPlugin.hpp"
#include "AudioFileLoader.hpp"
#include "PitchDetector.hpp"
#include "FilteredStereoDelay.hpp"
#include "PolyphonicNoteState.hpp"
#include "GranularTimeStretch.hpp"
#include "SliceMapping.hpp"

// 👇 SÆT FILTER-KLASSEN IND HER 👇
struct SvfStereo {
    float ic1eqL=0, ic2eqL=0, ic1eqR=0, ic2eqR=0;
    void process(float& L, float& R, float cutoff, float res, float sr, float type) {
        float g = std::tan(3.14159265358979323846f * cutoff / sr);
        float k = 2.0f - 2.0f * res;
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;

        float v3L = L - ic2eqL;
        float v1L = a1 * ic1eqL + a2 * v3L;
        float v2L = ic2eqL + a2 * ic1eqL + a3 * v3L;
        ic1eqL = 2.0f * v1L - ic1eqL;
        ic2eqL = 2.0f * v2L - ic2eqL;

        float v3R = R - ic2eqR;
        float v1R = a1 * ic1eqR + a2 * v3R;
        float v2R = ic2eqR + a2 * ic1eqR + a3 * v3R;
        ic1eqR = 2.0f * v1R - ic1eqR;
        ic2eqR = 2.0f * v2R - ic2eqR;

        L = (type < 0.5f) ? v2L : (L - k * v1L - v2L);
        R = (type < 0.5f) ? v2R : (R - k * v1R - v2R);
    }
};
// 👆 SLUT PÅ FILTER 👆
// 👇 CLOUD REVERB ALGORITME START 👇
struct DelayLine {
    std::vector<float> buffer;
    uint32_t pos = 0;
    void init(uint32_t length) {
        buffer.assign(length, 0.0f);
        pos = 0;
    }
    float read() const { return buffer.empty() ? 0.0f : buffer[pos]; }
    void write(float v) {
        if (buffer.empty()) return;
        buffer[pos] = v;
        if (++pos >= buffer.size()) pos = 0;
    }
};

struct CloudReverb {
    DelayLine dl[4];
    float lp[4] = {0,0,0,0};
    void init(float sr) {
        dl[0].init(sr * 0.0313f);
        dl[1].init(sr * 0.0371f);
        dl[2].init(sr * 0.0411f);
        dl[3].init(sr * 0.0437f);
    }
    void process(float& L, float& R, float size, float wet) {
        if (wet < 0.001f) return; // Spar CPU hvis den er slukket
        float fb = 0.7f + size * 0.28f;
        float damp = 0.2f + size * 0.3f;

        float r0 = dl[0].read(); float r1 = dl[1].read();
        float r2 = dl[2].read(); float r3 = dl[3].read();

        lp[0] = r0 * (1.0f - damp) + lp[0] * damp;
        lp[1] = r1 * (1.0f - damp) + lp[1] * damp;
        lp[2] = r2 * (1.0f - damp) + lp[2] * damp;
        lp[3] = r3 * (1.0f - damp) + lp[3] * damp;

        float inMix = (L + R) * 0.5f;

        float w0 = lp[0] + lp[1] + lp[2] + lp[3];
        float w1 = lp[0] - lp[1] + lp[2] - lp[3];
        float w2 = lp[0] + lp[1] - lp[2] - lp[3];
        float w3 = lp[0] - lp[1] - lp[2] + lp[3];

        dl[0].write(inMix + w0 * 0.5f * fb);
        dl[1].write(inMix + w1 * 0.5f * fb);
        dl[2].write(inMix + w2 * 0.5f * fb);
        dl[3].write(inMix + w3 * 0.5f * fb);

        L = L * (1.0f - wet) + (r0 - r1) * wet;
        R = R * (1.0f - wet) + (r2 - r3) * wet;
    }
};
// 👆 CLOUD REVERB ALGORITME SLUT 👆



#include <cmath>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <system_error>
#include <fstream>
#include <cstdlib>   // getenv
#include <atomic>
#include "DrumCloudParams.hpp"


// ------------------------------------------------------------
// Sample path cache helpers (Bitwig-safe persistence)

START_NAMESPACE_DISTRHO

std::atomic<float> gDrumCloudUiScanPos{0.0f};
std::atomic<int>   gDrumCloudUiScanMode{0};
static constexpr uint32_t kUiGrainMarkerCount = 16;
std::atomic<uint32_t> gDrumCloudUiGrainCount{0};
std::atomic<float> gDrumCloudUiGrainPos[kUiGrainMarkerCount];
std::atomic<int> gDrumCloudUiActiveSlice{-1};
static constexpr uint32_t kUiSliceBoundaryCount = 17;
std::atomic<uint32_t> gDrumCloudUiSliceBoundaryCount{0};
std::atomic<float> gDrumCloudUiSliceBoundaries[kUiSliceBoundaryCount];
std::atomic<uint32_t> gDrumCloudDetectedPitchGeneration{0};
std::atomic<int> gDrumCloudDetectedRoot{-1};
std::atomic<float> gDrumCloudDetectedFine{0.0f};
std::atomic<float> gDrumCloudDetectedConfidence{0.0f};

static void dirtyDbgLog(const char* fmt, ...)
{
    FILE* fp = std::fopen("/tmp/drumcloud-dirty.log", "a");
    if (!fp) return;
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(fp, fmt, args);
    va_end(args);
    std::fputc('\n', fp);
    std::fclose(fp);
}





static std::string getCachePath()
{
    const char* home = std::getenv("HOME");
    if (!home)
        return "/tmp/drumcloud-sample-cache.txt";

    return std::string(home) + "/.config/drumcloud-sample-cache.txt";
}


[[maybe_unused]]
static bool cacheLookup(uint32_t id, std::string& outPath)
{
    const std::string fn = getCachePath();
    std::FILE* fp = std::fopen(fn.c_str(), "r");
    if (!fp) return false;

    char line[4096];
    while (std::fgets(line, sizeof(line), fp))
    {
        unsigned rid = 0;
        char rpath[4000] = {0};

        if (std::sscanf(line, "%u\t%3999[^\n]", &rid, rpath) == 2)
        {
            if ((uint32_t)rid == id)
            {
                outPath = rpath;
                std::fclose(fp);
                return true;
            }
        }
    }

    std::fclose(fp);
    return false;
}

// ✅ Sæt dem HER (global scope, før plugin-klassen)
[[maybe_unused]] static inline float idToNorm24(const uint32_t id24)
{
    const uint32_t v = (id24 & 0xFFFFFFu);
    return float(v) / 16777215.0f;
}

[[maybe_unused]] static inline uint32_t norm24ToId(const float norm)
{
    const float n = (norm < 0.0f) ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    return (uint32_t)std::lround(n * 16777215.0f) & 0xFFFFFFu;
}



static bool cacheRead(uint32_t id, std::string& outPath)
{
    outPath.clear();

    if (id == 0)
        return false;

    const std::string fn = getCachePath();

    FILE* fp = std::fopen(fn.c_str(), "r");
    if (!fp)
        return false;

    char line[4096];
    while (std::fgets(line, sizeof(line), fp))
    {
        unsigned rid = 0;
        char pbuf[4096] = {};

        if (std::sscanf(line, "%u\t%4095[^\n]", &rid, pbuf) == 2)
        {
            if ((uint32_t)rid == id)
            {
                outPath = pbuf;
                std::fclose(fp);
                return true;
            }
        }
    }

    std::fclose(fp);
    return false;
}

[[maybe_unused]]
static void cacheWrite(uint32_t id, const std::string& path)
{
    if (id == 0 || path.empty())
        return;

    const std::string fn = getCachePath();

    // best-effort: ensure ~/.config exists
    if (const char* home = std::getenv("HOME"))
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(home) / ".config", ec);
    }

    if (std::FILE* fp = std::fopen(fn.c_str(), "a"))
    {
        std::fprintf(fp, "%u\t%s\n", (unsigned)id, path.c_str());
        std::fclose(fp);
    }
}

[[maybe_unused]]
static uint32_t sampleIdFromPath(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return 0;

    uint32_t h = 2166136261u; // FNV-1a
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
    {
        h ^= (uint32_t)(*p);
        h *= 16777619u;
    }

    h &= 0x00FFFFFFu; // 24-bit
    if (h == 0) h = 1;
    return h;
}








#define DRUMCLOUD_DEBUG 0

#if DRUMCLOUD_DEBUG
  #define DCLOG(...) std::fprintf(stderr, __VA_ARGS__)
#else
  #define DCLOG(...) do{}while(0)
#endif









// -----------------------------------------------------------------------------------------------------------
// Granular v1 (CPU-light) - loop source + snap-to markers (percussion oriented)

static inline uint32_t xorshift32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
static inline float frand01(uint32_t& s) {
    return (xorshift32(s) & 0x00FFFFFFu) / float(0x01000000u); // [0..1)
}

struct Grain
{
    float   pos = 0.0f;
    float   step = 1.0f;
    float   amp = 0.0f;
    int32_t age = 0;
    int32_t dur = 0;
    int32_t delay = 0;
    bool    active = false;
    uint8_t note = 60;
    bool    releasing = false;
    float   rel = 1.0f;      // 1 -> 0
    float   relDec = 0.0f;   // pr sample
    float   regionStart = 0.0f;
    float   regionEnd = 0.0f;

};

static bool loadWav16(const char* path,
                      std::vector<float>& outL,
                      std::vector<float>& outR,
                      uint32_t& outSR)
{
    outL.clear();
    outR.clear();
    outSR = 0;

    std::ifstream f(path, std::ios::binary);
    if (!f.good())
        return false;

    auto read_u32 = [&](uint32_t& v) { f.read(reinterpret_cast<char*>(&v), 4); return f.good(); };
    auto read_u16 = [&](uint16_t& v) { f.read(reinterpret_cast<char*>(&v), 2); return f.good(); };

    char riff[4]{};
    uint32_t riffSize = 0;
    char wave[4]{};

    f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;
    if (!read_u32(riffSize)) return false;
    f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    // Parse chunks
    uint16_t audioFormat = 0;
    uint16_t numCh = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;

    std::streampos dataPos = 0;
    uint32_t dataSize = 0;

    while (f.good() && (!dataPos || audioFormat == 0))
    {
        char id[4]{};
        uint32_t size = 0;
        f.read(id, 4);
        if (!f.good()) break;
        if (!read_u32(size)) break;

        if (std::strncmp(id, "fmt ", 4) == 0)
        {
            uint16_t blockAlign = 0;
            uint32_t byteRate = 0;

            read_u16(audioFormat);
            read_u16(numCh);
            read_u32(sampleRate);
            read_u32(byteRate);
            read_u16(blockAlign);
            read_u16(bitsPerSample);

            // Skip any extra fmt bytes
            if (size > 16)
                f.seekg(size - 16, std::ios::cur);
        }
        else if (std::strncmp(id, "data", 4) == 0)
        {
            dataPos = f.tellg();
            dataSize = size;
            f.seekg(size, std::ios::cur);
        }
        else
        {
            // Skip unknown chunk
            f.seekg(size, std::ios::cur);
        }

        // Chunks are word-aligned
        if (size & 1)
            f.seekg(1, std::ios::cur);
    }

    // Only PCM 16-bit supported in this minimal loader
    if (audioFormat != 1 || bitsPerSample != 16 || (numCh != 1 && numCh != 2) || dataPos == 0 || dataSize == 0)
        return false;

    outSR = sampleRate;

    // Read samples
    const uint32_t bytesPerFrame = numCh * 2;
    const uint32_t frames = dataSize / bytesPerFrame;

    outL.resize(frames);
    outR.resize(frames);

    f.clear();
    f.seekg(dataPos);

    for (uint32_t i = 0; i < frames; ++i)
    {
        int16_t s0 = 0;
        int16_t s1 = 0;
        f.read(reinterpret_cast<char*>(&s0), 2);
        if (numCh == 2) f.read(reinterpret_cast<char*>(&s1), 2);

        const float l = float(s0) / 32768.0f;
        const float r = (numCh == 2) ? (float(s1) / 32768.0f) : l;

        outL[i] = l;
        outR[i] = r;
    }

    return true;
}



class GranularEngine
{
public:
    float getScanPosNorm() const
    {
        float start = fSampleStartNorm;
        float end = fSampleEndNorm;
        if (fSliceMode != 0 && fActiveSlice >= 0)
        {
            int32_t frameStart = 0;
            int32_t frameEnd = 0;
            if (getVoiceRegionFrames(DrumCloudSlicer::kBaseMidiNote + fActiveSlice,
                                     frameStart, frameEnd) && sampleLen > 1)
            {
                start = float(frameStart) / float(sampleLen - 1);
                end = float(frameEnd) / float(sampleLen - 1);
            }
        }
        return start + fScanPos * (end - start);
    } // absolute 0..1 sample position

    int getSliceMode() const noexcept { return fSliceMode; }
    int getSliceCount() const noexcept { return fSliceCount; }
    int getActiveSlice() const noexcept { return fActiveSlice; }
    void setSliceMode(float value) noexcept
    {
        fSliceMode = std::clamp(int(std::lround(value)), 0, 2);
        if (fSliceMode == 0) fActiveSlice = -1;
        resetActiveVoicePlayback();
    }
    void setSliceCount(float value) noexcept
    {
        fSliceCount = std::clamp(int(std::lround(value)), 2, 16);
        if (fActiveSlice >= fSliceCount) fActiveSlice = -1;
        resetActiveVoicePlayback();
    }
    void init(double sampleRate)
{
    sr = (sampleRate > 1.0) ? float(sampleRate) : 48000.0f;

    for (int i = 0; i < kWinSize; ++i) {
    const float ph = float(i) / float(kWinSize - 1);
    win[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * ph);
}
    win[0] = 1.0f;  // TEST: gør starten ikke-silent


            makeTestLoop();
        makeMarkers();

    

        fDensityNorm = fBaseDensityNorm;
        updateDensityFromNorm();
        updateScanSlewCoeff();


    reset(); // ✅ semikolon
}


// Decoding and preview generation run only on the worker's private engine.
    struct PreparedSample {
        std::vector<float> left, right;
        uint32_t sampleRate = 0;
        int length = 0;
        int32_t markers[64]{};
        int markerCount = 0;
        int transientMarkerCount = 0;
        float waveMin[1024]{};
        float waveMax[1024]{};
        PitchDetectionResult pitch;
    };

    static std::unique_ptr<PreparedSample> prepareSample(const std::string& path)
    {
        std::unique_ptr<GranularEngine> scratch(new GranularEngine);
        if (!scratch->loadSample(path.c_str()))
            return nullptr;
        std::unique_ptr<PreparedSample> prepared(new PreparedSample);
        prepared->left = std::move(scratch->sampleL);
        prepared->right = std::move(scratch->sampleR);
        prepared->sampleRate = scratch->sampleSR;
        prepared->length = scratch->sampleLen;
        prepared->markerCount = scratch->markerCount;
        prepared->transientMarkerCount = scratch->transientMarkerCount;
        std::memcpy(prepared->markers, scratch->markers, sizeof(prepared->markers));
        std::memcpy(prepared->waveMin, scratch->waveMin, sizeof(prepared->waveMin));
        std::memcpy(prepared->waveMax, scratch->waveMax, sizeof(prepared->waveMax));
        prepared->pitch = detectSamplePitch(prepared->left.data(), prepared->right.data(),
                                            uint32_t(std::max(0, prepared->length)),
                                            prepared->sampleRate);
        return prepared;
    }

    // Swaps ownership in constant time; the worker later destroys the old vectors.
    void applyPreparedSample(PreparedSample& prepared) noexcept
    {
        // Retire grains referencing the old sample, while keeping held MIDI notes.
        activeCount = 0;
        fNotes.clearSpawnPhases();
        for (int i = 0; i < kMaxGrains; ++i) grains[i].active = false;
        sampleL.swap(prepared.left);
        sampleR.swap(prepared.right);
        sampleLen = prepared.length;
        sampleSR = prepared.sampleRate;
        markerCount = prepared.markerCount;
        transientMarkerCount = prepared.transientMarkerCount;
        std::memcpy(markers, prepared.markers, sizeof(markers));
        std::memcpy(waveMin, prepared.waveMin, sizeof(waveMin));
        std::memcpy(waveMax, prepared.waveMax, sizeof(waveMax));
    }


void reset()
{
    // Independent state for all 128 MIDI notes.
    fNotes.reset();

    // Active grain bookkeeping
    activeCount = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        activeIdx[i] = 0;
        grains[i].active = false;   // (lad resten være)
    }

    // Optional: hvis du vil have deterministisk reset
    // fPitchRate = 1.0f;
    // rng = 0x12345678u;
}
    bool isSilent() const
    {
        for (int i = 0; i < kMaxGrains; ++i)
            if (grains[i].active)
                return false;
        return !fNotes.anyActive();
    }



    void setVelToGrainSize(float amt) noexcept
{
    if (amt < 0.0f) amt = 0.0f;
    if (amt > 1.0f) amt = 1.0f;
    fVelToGrainSize = amt;
}

void setReleaseMs(float ms)
{
    if (ms < 5.0f) ms = 5.0f;
    if (ms > 5000.0f) ms = 5000.0f;
    fReleaseMs = ms;
}

float getReleaseMs() const noexcept
{
    return fReleaseMs;
}


float getBaseDensityNorm() const noexcept
{
    return fBaseDensityNorm;
}

void setBaseDensityNorm(float v) noexcept
{
    fBaseDensityNorm = std::clamp(v, 0.0f, 1.0f);
    fDensityNorm = fBaseDensityNorm;
    updateDensityFromNorm();
}

float getVelToDensity() const noexcept
{
    return fVelToDensity;
}

void setVelToDensity(float amt) noexcept
{
    fVelToDensity = std::clamp(amt, 0.0f, 1.0f);
}

float getPitchRateParam() const noexcept
{
    return fPitchRateParam;
}

void setPitchRateParam(float v) noexcept
{
    fPitchRateParam = std::clamp(v, 0.5f, 2.0f);
}

float getRootNote() const noexcept
{
    return fRootNote;
}

void setRootNote(float note) noexcept
{
    fRootNote = std::clamp(std::round(note), 0.0f, 127.0f);
}

float getSampleFineTune() const noexcept
{
    return fSampleFineTuneCents;
}

void setSampleFineTune(float cents) noexcept
{
    fSampleFineTuneCents = std::clamp(cents, -100.0f, 100.0f);
}

float getGrainAttackMs() const noexcept
{
    return fGrainAttackMs;
}

void setGrainAttackMs(float ms) noexcept
{
    fGrainAttackMs = std::clamp(ms, 0.0f, 500.0f);
}

float getGrainReleaseMs() const noexcept
{
    return fGrainReleaseMs;
}

float getTimeStretchRatio() const noexcept
{
    return fTimeStretchRatio;
}

void setTimeStretchRatio(float ratio) noexcept
{
    fTimeStretchRatio = DrumCloudTimeStretch::clampRatio(ratio);
}

void setGrainReleaseMs(float ms) noexcept
{
    fGrainReleaseMs = std::clamp(ms, 0.0f, 1000.0f);
}

float getScanSpeed() const noexcept
{
    return fScanSpeed;
}

void setScanSpeed(float v) noexcept
{
    fScanSpeed = std::clamp(v, 0.0f, 2.0f);
}

float getScanMode() const noexcept
{
    return float(fScanMode);
}

void setScanMode(float v)
{
    const int mode = int(std::lround(v));
    const int newMode = std::clamp(mode, 0, 3);
    if (newMode != fScanMode && newMode == kScanForward)
        resetActiveVoicePlayback();
    fScanMode = newMode;
    fScanHoldSamples = 0.0f;
    fScanTargetPos = fScanPos;
}

float getPlaybackMode() const noexcept
{
    return float(fPlaybackMode);
}

void setPlaybackMode(float v)
{
    const int mode = std::clamp(int(std::lround(v)), 0, 2);
    if (mode != fPlaybackMode)
    {
        fPlaybackMode = mode;
        resetActiveVoicePlayback();
    }
}

float getScanJumpRateHz() const noexcept
{
    return fScanJumpRateHz;
}

void setScanJumpRateHz(float hz)
{
    fScanJumpRateHz = std::clamp(hz, 0.1f, 40.0f);
}

float getScanJumpAmount() const noexcept
{
    return fScanJumpAmount;
}

void setScanJumpAmount(float v)
{
    fScanJumpAmount = std::clamp(v, 0.0f, 1.0f);
}

float getScanJumpSmoothMs() const noexcept
{
    return fScanSlewMs;
}

void setScanJumpSmoothMs(float ms)
{
    fScanSlewMs = std::clamp(ms, 0.0f, 500.0f);
    updateScanSlewCoeff();
}

float getTempoSyncRateMultiplier() const noexcept
{
    switch (fTempoSyncRateChoice)
    {
    case 0: return 0.5f;
    case 2: return 2.0f;
    default: return 1.0f;
    }
}

void setTempoSyncRateChoice(float value) noexcept
{
    if (value < 0.25f)
        fTempoSyncRateChoice = 0;
    else if (value < 0.75f)
        fTempoSyncRateChoice = 1;
    else
        fTempoSyncRateChoice = 2;
}

float getTempoSyncRateChoice() const noexcept
{
    switch (fTempoSyncRateChoice)
    {
    case 0: return 0.0f;
    case 2: return 1.0f;
    default: return 0.5f;
    }
}

void setTempoSyncInfo(float bpm, bool playing) noexcept
{
    fTempoSyncBpm = std::clamp(bpm, 20.0f, 300.0f);
    fTempoSyncPlaying = playing;
}

void setTempoSyncPhase(float phase) noexcept
{
    phase = phase - std::floor(phase);
    if (phase < 0.0f) phase += 1.0f;
    fTempoSyncPhase = phase;
}

    // ---- start / spread / snap control ----
    void setStartPosNorm(float v)
    {
    v = std::clamp(v, 0.0f, 1.0f);

    // Dead zone near zero so "left edge" becomes true zero
    if (sampleLen > 0) {
        const float eps = 1.0f / float(sampleLen);  // ~1 sample
        if (v <= eps) v = 0.0f;
    } else {
        if (v <= 0.0005f) v = 0.0f;
    }

    fStartPosNorm = v;
    }


    void setPosSpreadNorm(float v) noexcept
    {
        fPosSpreadNorm = std::clamp(v, 0.0f, 1.0f);
    }

    void setSnapMs(float ms) noexcept
    {
        fSnapMs = (ms < 0.0f) ? 0.0f : ms;
    }

    float getStartPosNorm() const noexcept
    {
        return fStartPosNorm;
    }

    float getPosSpreadNorm() const noexcept
    {
        return fPosSpreadNorm;
    }

    float getSnapMs() const noexcept
    {
        return fSnapMs;
    }

    float getSampleStartNorm() const noexcept { return fSampleStartNorm; }
    float getSampleEndNorm() const noexcept { return fSampleEndNorm; }

    void setSampleStartNorm(float value) noexcept
    {
        fSampleStartNorm = std::clamp(value, 0.0f, fSampleEndNorm - 0.001f);
    }

    void setSampleEndNorm(float value) noexcept
    {
        fSampleEndNorm = std::clamp(value, fSampleStartNorm + 0.001f, 1.0f);
    }

    uint32_t copyActiveGrainPositions(float* dst, uint32_t capacity) const noexcept
    {
        if (dst == nullptr || capacity == 0 || sampleLen <= 1) return 0;
        const uint32_t count = std::min<uint32_t>(capacity, uint32_t(std::max(0, activeCount)));
        for (uint32_t i = 0; i < count; ++i)
        {
            const Grain& grain = grains[activeIdx[i]];
            dst[i] = std::clamp(grain.pos / float(sampleLen - 1), 0.0f, 1.0f);
        }
        return count;
    }

void trigger(int note, int vel)
{
    const uint8_t midiNote = uint8_t(note & 0x7F);
    const uint8_t velocity = uint8_t(vel & 0x7F);
    if (fSliceMode != 0)
    {
        const int slice = int(midiNote) - DrumCloudSlicer::kBaseMidiNote;
        if (slice < 0 || slice >= getEffectiveSliceCount())
            return;
        fActiveSlice = slice;
    }
    fNotes.noteOn(midiNote, velocity);
    resetVoicePlayback(midiNote);

    // Each note receives its own burst and then its own continuous grain stream.
    const float burstWindowSec = 0.030f;
    int burst = int(std::round(grainsPerSecondForVelocity(velocity) * burstWindowSec));
    burst = std::clamp(burst, 2, 3);
    for (int i = 0; i < burst; ++i)
        spawnOneGrain(midiNote, velocity, 1.0f);
}
 // ✅ VIGTIG: trigger slutter her





    void noteOff(int note)
{
    const uint8_t midiNote = uint8_t(note & 0x7F);
    const int32_t relSamples = std::max<int32_t>(1, int32_t((fReleaseMs * 0.001f) * sr));
    fNotes.noteOff(midiNote, relSamples);
    const float dec = 1.0f / float(relSamples);

    DCLOG("[noteOff] note=%d relSamples=%d activeCount=%d\n",
          int(midiNote), int(relSamples), int(activeCount));

    for (int i = 0; i < activeCount; ++i)
    {
        Grain& g = grains[activeIdx[i]];
        if (g.active && g.note == midiNote && !g.releasing)
        {
            g.releasing = true;
            g.rel = 1.0f;
            g.relDec = dec;
        }
    }
}





void process(float* outL, float* outR, uint32_t frames)

{
    if (loopLen <= 0)
        return;

    if (fScanEnabled && sampleLen > 0 && sr > 1.0f)
    {
        if (fScanMode == kScanHold)
        {
            fScanPos = std::clamp(fStartPosNorm, 0.0f, 1.0f);
            fScanTargetPos = fScanPos;
            fScanWanderVel = 0.0f;
            fScanHoldSamples = 0.0f;
        }
        else if (fScanMode == kScanForward)
        {
            // Forward movement is advanced per MIDI voice below.  Keeping it
            // per voice prevents a newly triggered note from moving another
            // note's playhead.
            fScanTargetPos = fScanPos;
        }
        else if (fScanMode == kScanRandomJump)
        {
            const float speedScale = std::max(0.05f, fScanSpeed);
            const float effectiveJumpRateHz = std::max(
                0.10f,
                DrumCloudTimeStretch::scaleScanRate(fScanJumpRateHz * speedScale,
                                                    fTimeStretchRatio));

            fScanHoldSamples -= float(frames);

            while (fScanHoldSamples <= 0.0f)
            {
                const float center = fStartPosNorm;
                const float halfRange = 0.5f * fScanJumpAmount;

                float p = center + (rand01(rng) * 2.0f - 1.0f) * halfRange;
                p = std::clamp(p, 0.0f, 1.0f);

                fScanTargetPos = p;
                fScanHoldSamples += sr / effectiveJumpRateHz;
            }
        }
        else if (fScanMode == kScanTempoSync)
        {
            if (fTempoSyncPlaying)
            {
                const float beatsPerSec = DrumCloudTimeStretch::scaleScanRate(
                    (fTempoSyncBpm / 60.0f) * getTempoSyncRateMultiplier(),
                    fTimeStretchRatio);
                fTempoSyncPhase += (beatsPerSec * float(frames)) / sr;
                fTempoSyncPhase -= std::floor(fTempoSyncPhase);
                fScanPos = std::clamp(fTempoSyncPhase, 0.0f, 1.0f);
                fScanTargetPos = fScanPos;
            }
        }

        if (fScanMode != kScanTempoSync && fScanMode != kScanHold)
        {
            float delta = fScanTargetPos - fScanPos;
            if (delta > 0.5f)
                delta -= 1.0f;
            else if (delta < -0.5f)
                delta += 1.0f;

            const float slew = std::clamp(fScanSlewCoeff * float(frames), 0.0f, 1.0f);
            fScanPos += delta * slew;

            while (fScanPos < 0.0f) fScanPos += 1.0f;
            while (fScanPos >= 1.0f) fScanPos -= 1.0f;
        }
    }

    // ---- push scan pos to UI (meter) ----
    

  

    for (uint32_t f = 0; f < frames; ++f)
    {
        float l = 0.0f;
        float r = 0.0f;

        // Every held/releasing MIDI note owns an independent grain clock.
        const int32_t totalRelease = std::max<int32_t>(1, int32_t((fReleaseMs * 0.001f) * sr));
        for (uint32_t note = 0; note < 128; ++note)
        {
            const uint8_t midiNote = uint8_t(note);
            if (!fNotes.isActive(midiNote)) continue;

            const uint8_t velocity = fNotes.velocity(midiNote);
            const float voiceGain = fNotes.tailGain(midiNote, totalRelease);
            advanceVoicePlayback(midiNote);

            // ONE SHOT stops creating grains at the end marker. Existing
            // grains keep their natural envelopes, so the ending stays clean.
            const bool maySpawn = !(fScanEnabled &&
                                    fScanMode == kScanForward &&
                                    fPlaybackMode == kPlaybackOneShot &&
                                    fVoicePlaybackFinished[midiNote]);
            if (maySpawn)
            {
                const float gps = grainsPerSecondForVelocity(velocity) * voiceGain;
                const int spawnCount = fNotes.advanceSpawn(midiNote, gps / sr);
                for (int spawn = 0; spawn < spawnCount; ++spawn)
                    spawnOneGrain(midiNote, velocity, voiceGain);
            }

            fNotes.advanceRelease(midiNote);
        }

        int i = 0;
        while (i < activeCount)
        {
            Grain& g = grains[activeIdx[i]];

            if (!g.active)
            {
                swapRemove(i);
                continue;
            }

            if (g.delay > 0)
            {
                --g.delay;
                ++i;
                continue;
            }

            if (g.age >= g.dur)
            {
                g.active = false;
                swapRemove(i);
                continue;
            }

            float sL = 0.0f;
float sR = 0.0f;

if (sampleLen > 0)
{
    // sample (ingen wrap – clamp + linear interpolation)
    const int32_t i0 = int32_t(g.pos);
    const float frac = g.pos - float(i0);


    if (i0 < 0 || i0 >= sampleLen)
    {
        // out of range -> kill grain
        g.active = false;
        swapRemove(i);
        continue;
    }

    int32_t i1 = i0 + 1;
    if (i1 >= sampleLen) i1 = sampleLen - 1;

    const float aL = sampleL[i0];
    const float bL = sampleL[i1];
    const float aR = sampleR[i0];
    const float bR = sampleR[i1];

    sL = aL + (bL - aL) * frac;
    sR = aR + (bR - aR) * frac;
}
else
{
    // fallback til loop (som før)
    readLoopLinear(g.pos, sL, sR);
}


            const float w = windowAt(g.age, g.dur);

            float relGain = 1.0f;
            if (g.releasing)
            {
                g.rel -= g.relDec;
                if (g.rel <= 0.0f)
                {
                    g.active = false;
                    swapRemove(i);
                    continue;
                }
                relGain = g.rel * g.rel; // smooth fade-out curve
            }

            const float vv = (sL + sR) * 0.5f * w * g.amp * relGain;
            l += vv;
            r += vv;

            if (sampleLen > 0)
{
    g.pos += g.step;
    const float regionStart = g.regionStart;
    const float regionEnd = g.regionEnd;
    if (g.pos < regionStart || g.pos >= regionEnd)
    {
        if (!g.releasing)
        {
            g.releasing = true;
            g.rel = std::min(g.rel, 0.35f);
            const float quickRelMs = 2.0f;
            const float quickRelSamples = std::max(1.0f, sr * (quickRelMs / 1000.0f));
            g.relDec = std::max(g.relDec, 1.0f / quickRelSamples);
        }

        g.pos = std::clamp(g.pos, regionStart, regionEnd);
    }
}
else
{
    g.pos = wrapPos(g.pos + g.step);
}

++g.age;


            ++i;
        }

        outL[f] += std::tanh(l * 0.9f);
        outR[f] += std::tanh(r * 0.9f);
    }
}

// ---- sample buffer ----
std::vector<float> sampleL;
std::vector<float> sampleR;
uint32_t sampleSR = 0;
int sampleLen = 0;









private:
static float densityNormToGps(float densityNorm) noexcept
{
    const float n = std::clamp(densityNorm, 0.0f, 1.0f);
    return 0.2f + (10.0f - 0.2f) * std::pow(n, 1.8f);
}

float grainsPerSecondForVelocity(uint8_t velocity) const noexcept
{
    const float velNorm = float(velocity) / 127.0f;
    const float velShaped = velNorm * velNorm;
    const float density = std::clamp(
        fBaseDensityNorm * (1.0f - fVelToDensity) + velShaped * fVelToDensity,
        0.0f, 1.0f);
    return densityNormToGps(density);
}

void updateDensityFromNorm()
{
    fGrainsPerSec = densityNormToGps(fDensityNorm);
}

void updateScanSlewCoeff()
{
    const float ms = std::max(1.0f, fScanSlewMs);
    const float samples = (ms * 0.001f) * sr;
    fScanSlewCoeff = (samples > 1.0f) ? (1.0f / samples) : 1.0f;
}

// ---- Random helpers (samme "kontrol-lag") ----

static inline float rand01(uint32_t& rng)
{
    rng = rng * 1664525u + 1013904223u;
    return float(rng >> 8) * (1.0f / 16777216.0f);
}

int32_t getRegionStartFrame() const noexcept
{
    if (sampleLen <= 1) return 0;
    return std::clamp<int32_t>(int32_t(std::floor(fSampleStartNorm * float(sampleLen - 1))), 0, sampleLen - 1);
}

int32_t getRegionEndFrame() const noexcept
{
    if (sampleLen <= 1) return 0;
    return std::clamp<int32_t>(int32_t(std::ceil(fSampleEndNorm * float(sampleLen - 1))), 0, sampleLen - 1);
}

int getEffectiveSliceCount() const noexcept
{
    if (fSliceMode == 0) return 0;
    if (fSliceMode == 1) return fSliceCount;
    return DrumCloudSlicer::transientSliceCount(getRegionStartFrame(), getRegionEndFrame(),
                                                markers, transientMarkerCount, fSliceCount);
}

bool getVoiceRegionFrames(int note, int32_t& start, int32_t& end) const noexcept
{
    start = getRegionStartFrame();
    end = getRegionEndFrame();
    if (fSliceMode == 0) return true;
    const int slice = note - DrumCloudSlicer::kBaseMidiNote;
    if (slice < 0) return false;
    if (fSliceMode == 1)
    {
        if (slice >= fSliceCount) return false;
        DrumCloudSlicer::frameRange(start, end, slice, fSliceCount, start, end);
        return true;
    }
    return DrumCloudSlicer::transientFrameRange(start, end, markers, transientMarkerCount,
                                                slice, fSliceCount, start, end);
}

public:
uint32_t copySliceBoundaries(float* dst, uint32_t capacity) const noexcept
{
    if (dst == nullptr || capacity < 2 || sampleLen <= 1 || fSliceMode == 0) return 0;
    int32_t frames[DrumCloudSlicer::kMaxSliceCount + 1]{};
    int count = 0;
    if (fSliceMode == 1)
    {
        count = std::min<int>(fSliceCount + 1, int(capacity));
        const int32_t start = getRegionStartFrame();
        const int32_t end = getRegionEndFrame();
        const int32_t length = end - start + 1;
        for (int i = 0; i < count; ++i)
            frames[i] = start + int32_t((int64_t(length) * i) / fSliceCount);
    }
    else
    {
        count = DrumCloudSlicer::buildTransientBoundaries(
            getRegionStartFrame(), getRegionEndFrame(), markers, transientMarkerCount,
            fSliceCount, frames, std::min<int>(int(capacity), DrumCloudSlicer::kMaxSliceCount + 1));
    }
    for (int i = 0; i < count; ++i)
        dst[i] = std::clamp(float(frames[i]) / float(sampleLen - 1), 0.0f, 1.0f);
    return uint32_t(std::max(0, count));
}

private:
inline float computeStartNorm(int note, uint32_t& rng) const
{
    float base = fStartPosNorm;
    if (fScanEnabled)
    {
        base = (fScanMode == kScanForward && note >= 0 && note < 128)
             ? fVoicePlaybackPos[uint8_t(note)]
             : fScanPos;
    }

    const float spread = fPosSpreadNorm;  // 0..1

    const float u = rand01(rng) * 2.0f - 1.0f; // [-1,1]
    float s = base + u * spread;

    if (s < 0.0f) s = 0.0f;
    else if (s > 1.0f) s = 1.0f;
    return s;
}

void swapRemove(int idx)
{
    const int last = activeCount - 1;
    if (idx != last)
        activeIdx[idx] = activeIdx[last];
    activeCount = std::max(0, last);
}


    float wrapPos(float p) const
    {
        const float len = float(loopLen);
        while (p < 0.0f) p += len;
        while (p >= len) p -= len;
        return p;
    }

    void readLoopLinear(float pos, float& oL, float& oR) const
    {
        const int32_t i0 = int32_t(pos);
        const float frac = pos - float(i0);

        int32_t i1 = i0 + 1;
        if (i1 >= loopLen) i1 -= loopLen;

        const float aL = loopL[i0];
        const float bL = loopL[i1];
        const float aR = loopR[i0];
        const float bR = loopR[i1];

        oL = aL + (bL - aL) * frac;
        oR = aR + (bR - aR) * frac;
    }



    int32_t seekZeroCrossNear(int32_t start, int32_t radius) const
    {
        if (sampleLen <= 2) return std::clamp(start, 0, std::max(0, sampleLen - 2));

        start = std::clamp(start, 0, sampleLen - 2);
        radius = std::max(0, radius);

        auto monoAt = [&](int32_t i) -> float
        {
            return 0.5f * (sampleL[i] + sampleR[i]);
        };

        const int32_t lo = std::max<int32_t>(0, start - radius);
        const int32_t hi = std::min<int32_t>(sampleLen - 2, start + radius);

        int32_t best = start;
        float bestScore = std::fabs(monoAt(start));

        for (int32_t i = lo; i <= hi; ++i)
        {
            const float a = monoAt(i);
            const float b = monoAt(i + 1);
            const float score = std::fabs(a) + std::fabs(b);

            if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f))
                return i;

            if (score < bestScore)
            {
                bestScore = score;
                best = i;
            }
        }

        return best;
    }

    float windowAt(int32_t age, int32_t dur) const
    {
        if (dur <= 1 || age < 0 || age >= dur) return 0.0f;

        float attackFrames = fGrainAttackMs * 0.001f * sr;
        float releaseFrames = fGrainReleaseMs * 0.001f * sr;
        const float available = float(dur - 1);
        const float requested = attackFrames + releaseFrames;

        // Very short grains still receive both slopes without exceeding their life.
        if (requested > available && requested > 0.0f)
        {
            const float scale = available / requested;
            attackFrames *= scale;
            releaseFrames *= scale;
        }

        float envelope = 1.0f;
        if (attackFrames > 0.0f && float(age) < attackFrames)
        {
            const float t = std::clamp(float(age) / attackFrames, 0.0f, 1.0f);
            const float a = std::sin(t * float(M_PI) * 0.5f);
            envelope = a * a;
        }

        const float remaining = float(dur - 1 - age);
        if (releaseFrames > 0.0f && remaining < releaseFrames)
        {
            const float t = std::clamp(remaining / releaseFrames, 0.0f, 1.0f);
            const float r = std::sin(t * float(M_PI) * 0.5f);
            envelope = std::min(envelope, r * r);
        }

        return envelope;
    }

    int32_t snapBackward(int32_t raw, int32_t radius) const
    {
    if (markerCount <= 0) return raw;

    if (raw <= radius)
        return 0;

    int lo = 0, hi = markerCount;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (markers[mid] <= raw) lo = mid + 1;
        else hi = mid;
    }

    if (lo == 0)
        return raw;

    const int32_t best = markers[lo - 1];
    return (std::abs(best - raw) <= radius) ? best : raw;
    }



    void makeMarkers()
    {
        markerCount = 0;
        if (loopLen <= 0) return;

        const float a = 0.01f;
        float envPrev = 0.0f;
        float env = 0.0f;

        const int32_t minDist = int32_t(sr * 0.030f);
        int32_t lastMarker = -minDist;

        float avg = 0.0f;
        const float avgA = 0.0015f;

        float currOn = 0.0f;

        for (int32_t i = 0; i < loopLen; ++i)
        {
            const float m = 0.5f * (loopL[i] + loopR[i]);
            const float absM = std::fabs(m);

            env += a * (absM - env);

            const float d = env - envPrev;
            envPrev = env;

            const float on = (d > 0.0f) ? d : 0.0f;

            avg += avgA * (on - avg);
            currOn = on;

            if (i < 2) continue;

            const float thr = avg * 6.0f;

            if (currOn > thr && (i - lastMarker) >= minDist)
            {
                if (markerCount < kMaxMarkers)
                {
                    markers[markerCount++] = i;
                    lastMarker = i;
                }
            }
        }

        if (markerCount == 0)
        {
            const int32_t step = int32_t(sr * 0.125f);
            for (int32_t p = 0; p < loopLen && markerCount < kMaxMarkers; p += step)
                markers[markerCount++] = p;
        }
    }

    void makeTestLoop()
    {
        loopLen = int32_t(sr * 2.0f);
        if (loopLen > kMaxLoop) loopLen = kMaxLoop;

        for (int i = 0; i < loopLen; ++i) {
            loopL[i] = 0.0f;
            loopR[i] = 0.0f;
        }

        auto addPulse = [&](int32_t start, float freq, float decayMs, float amp)
        {
            const int32_t N = std::min(loopLen - start, int32_t(sr * 0.30f));
            const float decay = std::exp(-1.0f / (sr * (decayMs / 1000.0f)));
            float env = 1.0f;
            float ph = 0.0f;
            const float inc = 2.0f * float(M_PI) * freq / sr;

            for (int32_t n = 0; n < N; ++n) {
                const float s = std::sin(ph) * env * amp;
                loopL[start + n] += s;
                loopR[start + n] += s;
                ph += inc;
                env *= decay;
            }
        };

        addPulse(0,               55.0f, 80.0f, 0.9f);
        addPulse(int32_t(sr),     55.0f, 80.0f, 0.9f);
        addPulse(int32_t(sr*0.5f), 180.0f, 60.0f, 0.6f);
        addPulse(int32_t(sr*1.5f), 180.0f, 60.0f, 0.6f);

        for (int i = 0; i < loopLen; ++i) {
            if (loopL[i] > 1.0f) loopL[i] = 1.0f;
            else if (loopL[i] < -1.0f) loopL[i] = -1.0f;

            if (loopR[i] > 1.0f) loopR[i] = 1.0f;
            else if (loopR[i] < -1.0f) loopR[i] = -1.0f;
        }
    }
    void buildWaveformPreview()
{
    if (sampleL.empty())
        return;

    const int n = sampleL.size();
    const int step = std::max(1, n / kWavePreviewSize);

    for (int i = 0; i < kWavePreviewSize; ++i)
    {
        int start = i * step;
        int end   = std::min(start + step, n);

        float mn =  1.0f;
        float mx = -1.0f;

        for (int j = start; j < end; ++j)
        {
            float v = sampleL[j];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }

        waveMin[i] = mn;
        waveMax[i] = mx;
    }
}


    bool loadSample(const char* path)
{
    if (!path || !path[0])
        return false;

    LoadedAudio a;
    std::string err;
    if (!loadAudioFileToFloat(path, a, &err))
    {
#if DRUMCLOUD_DEBUG
        std::fprintf(stderr, "[DrumCloud] loadAudioFileToFloat failed: %s\n", err.c_str());
#endif
        return false;
    }

    if (a.frames == 0 || a.channels == 0)
        return false;

    const size_t frames = (size_t)a.frames;

    // Prepare vectors
    sampleL.resize(frames);
    sampleR.resize(frames);

    if (a.channels == 1)
    {
        for (size_t i = 0; i < frames; ++i)
        {
            const float v = a.interleaved[i];
            sampleL[i] = v;
            sampleR[i] = v;
        }
    }
    else
    {
        // take first two channels as L/R
        for (size_t i = 0; i < frames; ++i)
        {
            const size_t base = i * (size_t)a.channels;
            sampleL[i] = a.interleaved[base + 0];
            sampleR[i] = a.interleaved[base + 1];
        }
    }

    sampleLen = (int32_t)frames;
    sampleSR  = (int32_t)a.sampleRate;

    
    makeMarkersFromSample();

    // ensure marker 0 exists (markers is a fixed array)
    


    buildWaveformPreview();


#if DRUMCLOUD_DEBUG
    std::fprintf(stderr, "[DrumCloud] loaded '%s' frames=%d sr=%d ch=%u\n",
                 path, sampleLen, sampleSR, a.channels);
#endif

    return true;
}




void makeMarkersFromSample()
{
    markerCount = 0;
    transientMarkerCount = 0;
    if (sampleLen <= 0) return;

    const float srMark = (sampleSR > 0) ? float(sampleSR) : sr;

    const float a = 0.01f;
    float envPrev = 0.0f;
    float env = 0.0f;

    const int32_t minDist = int32_t(srMark * 0.030f);
    int32_t lastMarker = -minDist;

    float avg = 0.0f;
    const float avgA = 0.0015f;

    float currOn = 0.0f;

    // ✅ Always reserve marker 0 first (if room)
    if (markerCount < kMaxMarkers)
    {
        markers[markerCount++] = 0;
        lastMarker = 0;
    }

    for (int32_t i = 0; i < sampleLen; ++i)
    {
        const float m = 0.5f * (sampleL[i] + sampleR[i]);
        const float absM = std::fabs(m);

        env += a * (absM - env);

        const float d = env - envPrev;
        envPrev = env;

        const float on = (d > 0.0f) ? d : 0.0f;

        avg += avgA * (on - avg);
        currOn = on;

        if (i < 2) continue;

        const float thr = avg * 6.0f;

        if (currOn > thr && (i - lastMarker) >= minDist)
        {
            if (markerCount < kMaxMarkers)
            {
                // ✅ Avoid duplicate 0 (and other duplicates later) cheaply:
                // only add if not same as previous stored marker
                if (markers[markerCount - 1] != i)
                {
                    markers[markerCount++] = i;
                    lastMarker = i;
                }
            }
        }
    }

    // Preserve the real analysis result before adding snap-only fallback markers.
    transientMarkerCount = markerCount;

    // ✅ If we only have the forced "0" marker, add fallback grid markers.
    // (Your old code triggers when markerCount==0; now it's at least 1.)
    if (markerCount == 1)
    {
        const int32_t step = int32_t(srMark * 0.125f);
        for (int32_t p = step; p < sampleLen && markerCount < kMaxMarkers; p += step)
            markers[markerCount++] = p;
    }

    if (markerCount > 1)
        std::sort(markers, markers + markerCount);

    // ✅ Ensure last marker exists (sampleLen-1) and avoid duplicate
    const int32_t last = sampleLen - 1;
    if (markerCount < kMaxMarkers)
    {
        if (markerCount == 0 || markers[markerCount - 1] != last)
            markers[markerCount++] = last;
    }
    else
    {
        // If full, at least force the last slot to be the end
        markers[markerCount - 1] = last;
    }

    // ✅ Re-sort after appending last
    if (markerCount > 1)
        std::sort(markers, markers + markerCount);

    // ✅ Optional: compact duplicates after sort (super-safe)
    if (markerCount > 1)
    {
        int w = 1;
        for (int r = 1; r < markerCount; ++r)
        {
            if (markers[r] != markers[w - 1])
                markers[w++] = markers[r];
        }
        markerCount = w;
    }

#ifdef DRUMCLOUD_DEBUG
    DCLOG("[markers] count=%d first=%d last=%d\n",
          markerCount,
          markerCount > 0 ? markers[0] : -1,
          markerCount > 0 ? markers[markerCount - 1] : -1);
#endif
}











    Grain& allocGrain()
{
    for (int i = 0; i < kMaxGrains; ++i)
        if (!grains[i].active)
            return grains[i];

    if (activeCount > 0) {
        Grain& g = grains[activeIdx[0]];
        swapRemove(0);
        return g;
    }
    return grains[0];

}

void spawnOneGrain(int note, int vel, float densityMul)
{
    const float d = 1.0f * densityMul;

    Grain& g = allocGrain();
    g.active = true;

    // -------------------------------
    // 1) Compute duration FIRST
    // -------------------------------
    const float grainMsBase = 18.0f + frand01(rng) * 48.0f;

    const float velNorm  = float(vel) / 127.0f;
    const float velCurve = velNorm * velNorm;

    const float minMs = 22.0f;
    const float maxMs = 280.0f;

    const float grainMsTarget = minMs + (maxMs - minMs) * velCurve;

    const float amt = fVelToGrainSize;
    const float grainMs = grainMsBase + (grainMsTarget - grainMsBase) * amt;

    g.dur = std::max(16, int32_t(sr * (grainMs / 1000.0f)));
    g.age = 0;

    // -------------------------------
// 2) Choose start position (BEGIN-based)
// -------------------------------
if (sampleLen > 0)
{
    // Base and spread are relative to the selected sample region.
    int32_t regionStart = 0;
    int32_t regionEnd = 0;
    if (!getVoiceRegionFrames(note, regionStart, regionEnd)) return;
    const int32_t regionLength = std::max<int32_t>(1, regionEnd - regionStart + 1);
    const float startNorm = computeStartNorm(note, rng); // 0..1 inside region
    const float base = float(regionStart) + startNorm * float(regionLength - 1);

    const float spread = fPosSpreadNorm * float(regionLength) * 0.5f;

    // rand in [-1, +1]
    const float r01 = float(rng = rng * 196314165u + 907633515u) / float(0xffffffffu);
    const float r11 = (r01 * 2.0f) - 1.0f;

    float pos = base + r11 * spread;

    // Clamp so the grain stays inside the selected region where possible.
    const int32_t maxStart = std::max<int32_t>(regionStart, regionEnd - g.dur);
    if (pos < float(regionStart)) pos = float(regionStart);
    if (pos > float(maxStart)) pos = float(maxStart);

    // detect "start is essentially zero"
    const bool startIsZero =
    (!fScanEnabled) && (fStartPosNorm <= (1.0f / float(sampleLen)));


    if (startIsZero)
    {
        pos = float(regionStart); // force selected region start
    }
    else if (fSnapMs > 0.0f && markerCount > 0)
{
    const int32_t radius = int32_t((fSnapMs * 0.001f) * sr);
    const int32_t raw = int32_t(pos);
    const int32_t snapped = snapBackward(raw, radius);
    pos = float(snapped);
}


    // tiny local zero-cross seek to reduce clicks/pops on grain start
    const int32_t zcRadius = std::max<int32_t>(1, int32_t(sr * 0.00075f)); // ~0.75 ms
    pos = float(seekZeroCrossNear(int32_t(pos), zcRadius));
    pos = std::clamp(pos, float(regionStart), float(regionEnd));

    g.pos = pos;
    g.regionStart = float(regionStart);
    g.regionEnd = float(regionEnd);
}
else
{
    g.pos = 0.0f;
}




    g.amp = 0.10f + 0.42f * d;
    if (densityMul >= 0.95f)
        g.amp *= 1.18f;

    const float noteSemitones = fSliceMode != 0
        ? fSampleFineTuneCents / 100.0f
        : (float(note) - fRootNote) + fSampleFineTuneCents / 100.0f;
    const float sampleRateRatio = (sampleSR > 0 && sr > 0.0f)
        ? float(sampleSR) / sr
        : 1.0f;
    g.step = fPitchRateParam * std::pow(2.0f, noteSemitones / 12.0f) * sampleRateRatio;

    const float semis  = fSliceMode != 0 ? 0.0f : (frand01(rng) - 0.5f) * 1.0f;
    const float detune = std::pow(2.0f, semis / 12.0f);
    g.step *= detune;

    g.note = uint8_t(note & 0x7F);
    g.releasing = false;
    g.rel = 1.0f;
    g.relDec = 0.0f;

    // For tight drum hits: no random start delay
    // (keeps Start Position visually & audibly aligned)
    g.delay = 0;


    const int idx = int(&g - grains);
    if (activeCount < kMaxGrains)
        activeIdx[activeCount++] = idx;
}
private:
    void resetVoicePlayback(uint8_t note) noexcept
    {
        fVoicePlaybackPos[note] = std::clamp(fStartPosNorm, 0.0f, 1.0f);
        fVoicePlaybackDirection[note] = 1;
        fVoicePlaybackFinished[note] = false;
        fUiVoiceNote = note;
        fScanPos = fVoicePlaybackPos[note];
        fScanTargetPos = fScanPos;
    }

    void resetActiveVoicePlayback() noexcept
    {
        for (uint32_t note = 0; note < 128; ++note)
        {
            if (fNotes.isActive(uint8_t(note)))
                resetVoicePlayback(uint8_t(note));
        }
    }

    void advanceVoicePlayback(uint8_t note) noexcept
    {
        if (!fScanEnabled || fScanMode != kScanForward || sr <= 1.0f)
            return;
        if (fPlaybackMode == kPlaybackOneShot && fVoicePlaybackFinished[note])
            return;

        const float scanRate = DrumCloudTimeStretch::scaleScanRate(fScanSpeed, fTimeStretchRatio);
        float pos = fVoicePlaybackPos[note];
        const float step = scanRate / sr;

        if (fPlaybackMode == kPlaybackPingPong)
        {
            pos += step * float(fVoicePlaybackDirection[note]);
            if (pos >= 1.0f)
            {
                pos = 2.0f - pos;
                fVoicePlaybackDirection[note] = -1;
            }
            else if (pos <= 0.0f)
            {
                pos = -pos;
                fVoicePlaybackDirection[note] = 1;
            }
            pos = std::clamp(pos, 0.0f, 1.0f);
        }
        else
        {
            pos += step;
            if (fPlaybackMode == kPlaybackOneShot)
            {
                if (pos >= 1.0f)
                {
                    pos = 1.0f;
                    fVoicePlaybackFinished[note] = true;
                }
            }
            else
            {
                pos -= std::floor(pos);
            }
        }

        fVoicePlaybackPos[note] = pos;
        if (note == fUiVoiceNote)
        {
            fScanPos = pos;
            fScanTargetPos = pos;
        }
    }

    // ---- member variables ----
    static constexpr int kMaxGrains = 64;
    static constexpr int kWinSize   = 1024;

    enum ScanMode
    {
        kScanHold = 0,
        kScanForward = 1,
        kScanRandomJump = 2,
        kScanTempoSync = 3
    };

    enum PlaybackMode
    {
        kPlaybackLoop = 0,
        kPlaybackOneShot = 1,
        kPlaybackPingPong = 2
    };

    float sr = 48000.0f;
    float fPitchRateParam = 1.0f;
    float fRootNote = 60.0f;
    float fSampleFineTuneCents = 0.0f;
    float fGrainAttackMs = 10.0f;
    float fGrainReleaseMs = 80.0f;
    float fTimeStretchRatio = 1.0f;
    PolyphonicNoteState fNotes;
    float fVoicePlaybackPos[128]{};
    int8_t fVoicePlaybackDirection[128]{};
    bool fVoicePlaybackFinished[128]{};
    uint8_t fUiVoiceNote = 0;
    int fPlaybackMode = kPlaybackLoop;
    int fSliceMode = 0;
    int fSliceCount = 8;
    int fActiveSlice = -1;

    static constexpr int kMaxLoop = 48000 * 4;
    float loopL[kMaxLoop]{};
    float loopR[kMaxLoop]{};
    int32_t loopLen = 0;
    uint32_t rng = 0x12345678u;

    float fBaseDensityNorm = 0.72f;
    float fVelToDensity = 0.42f;
    float fDensityNorm  = 0.72f;
    float fGrainsPerSec = 10.0f;

    float fVelToGrainSize = 0.58f;

     // ---- position random / snap ----
    float fStartPosNorm  = 0.0f;   // 0..1 within selected region
    float fPosSpreadNorm = 0.0f;   // 0..1
    float fSampleStartNorm = 0.0f; // absolute sample region
    float fSampleEndNorm = 1.0f;
    float fSnapMs        = 10.0f;  // ms

    
    float fScanPos   = 0.0f;   // 0..1
    float fScanSpeed = 0.5f;  // cycles per second / movement scale
    bool  fScanEnabled = true;
    int   fScanMode = kScanHold;
    float fScanJumpRateHz = 27.0f;
    float fScanJumpAmount = 0.69f;
    float fScanHoldSamples = 0.0f;
    float fScanTargetPos = 0.0f;
    float fScanWanderVel = 0.0f;
    float fTempoSyncBpm = 120.0f;
    bool  fTempoSyncPlaying = false;
    float fTempoSyncPhase = 0.0f;
    int   fTempoSyncRateChoice = 1; // 0=0.5x, 1=1x, 2=2x
    float fScanSlewMs = 272.0f;
    float fScanSlewCoeff = 1.0f;



    static constexpr int kMaxMarkers = 64;
    int32_t markers[kMaxMarkers]{};
    int markerCount = 0;
    int transientMarkerCount = 0;

    Grain grains[kMaxGrains]{};
    int   activeIdx[kMaxGrains]{};
    int   activeCount = 0;

    float win[kWinSize]{};
    float fReleaseMs = 3000.0f;

    static constexpr int kWavePreviewSize = 1024;
    float waveMin[kWavePreviewSize]{};
    float waveMax[kWavePreviewSize]{};

    
}; // <-- slut på den forrige class/struct






// (så kommer plugin-klassen)
enum DrumCloudStates {
    stateSamplePath = 0,
    stateCount
};

class SendNoteExamplePlugin : public Plugin
{
public:
    SendNoteExamplePlugin()
    : Plugin(paramCount, 1, stateCount)
    {
        fWorker = std::thread([this]{ workerLoop(); });
    }

    ~SendNoteExamplePlugin() override
    {
        {
            std::lock_guard<std::mutex> lock(fWorkerMutex);
            fWorkerStop = true;
        }
        fWorkerCV.notify_one();
        if (fWorker.joinable()) fWorker.join();
        delete fReady.exchange(nullptr, std::memory_order_acq_rel);
        delete fRetired.exchange(nullptr, std::memory_order_acq_rel);
    }











protected:
   /* 
    * Information */

   /**
      Get the plugin label.
      This label is a short restricted name consisting of only _, a-z, A-Z and 0-9 characters.
    */
    const char* getLabel() const override
    {
        return "SendNote";
    }

   /**
      Get an extensive comment/description about the plugin.
    */
    const char* getDescription() const override
    {
        return "Plugin that demonstrates sending notes from the editor in DPF.";
    }

   /**
      Get the plugin author/maker.
    */
    const char* getMaker() const override
    {
        return "Fuimadane";
    }

    // --------------------------------------------------------------------------------------------------------
// State (sample persistence)

// --- State readback (for host save) ---
String getState(const char* key) const override
{
    if (std::strcmp(key, "samplePath") == 0)
    {
        // ingen logging

        return String(fSamplePath.c_str());
    }

    return String();
}














void setState(const char* key, const char* value) override
{
    if (std::strcmp(key, "samplePath") == 0)
    {
        const std::string newPath = value ? value : "";

        if (newPath != fSamplePath)
        {
            fSamplePath = newPath;
            if (!fSamplePath.empty())
            {
                {
                    std::lock_guard<std::mutex> lock(fWorkerMutex);
                    fWorkerPath = fSamplePath;
                    fWorkerRequest = true;
                }
                fWorkerCV.notify_one();
            }
        }
    }
}

















   /**
      Get the plugin homepage.
    */
    const char* getHomePage() const override
    {
        return "https://linktr.ee/Fuimadane";
    }

   /**
      Get the plugin license name (a single line of text).
      For commercial plugins this should return some short copyright information.
    */
    const char* getLicense() const override
    {
        return "ISC";
    }

   /**
      Get the plugin version, in hexadecimal.
    */
    uint32_t getVersion() const override
{
    return d_version(1, 9, 0); 
}
float getParameterValue(uint32_t index) const override
{
    if (index == paramReverbSize) 
        return fReverbSize; // NYT

    if (index == paramReverbMix) 
        return fReverbMix;   // NYT
    
    if (index == paramFilter) 
        return fFilter;       // NYT

    if (index == paramResonance) 
        return fResonance; // NYT

    if (index == paramVolume)
        return fVolume;

    if (index == paramDensity)
        return fGran.getBaseDensityNorm();

    if (index == paramVelocityToDensity)
        return fGran.getVelToDensity();

    if (index == paramVelocityToGrainSize)
        return fVelocityGrainSize;

    if (index == paramPitchRate)
        return fGran.getPitchRateParam();

    if (index == paramRelease)
        return fGran.getReleaseMs();

    if (index == paramStartPosition)
        return fGran.getStartPosNorm();

    if (index == paramPositionSpread)
        return fGran.getPosSpreadNorm();

    if (index == paramSnapMs)
        return fGran.getSnapMs();

    if (index == paramScanSpeed)
        return fGran.getScanSpeed();

    if (index == paramScanMode)
        return fGran.getScanMode();

    if (index == paramScanJumpRate)
        return fGran.getScanJumpRateHz();

    if (index == paramScanJumpAmount)
        return fGran.getScanJumpAmount();

    if (index == paramScanJumpSmoothMs)
        return fGran.getScanJumpSmoothMs();

    if (index == paramSyncRate)
        return fGran.getTempoSyncRateChoice();

    if (index == paramSamplePath)
    {
        dirtyDbgLog("getParameterValue paramSamplePath -> %.9f", fSamplePing);
        return fSamplePing;
    }

    if (index == paramScanPos)
        return 0.0f;

    if (index == paramRootNote)
        return fGran.getRootNote();

    if (index == paramSampleFineTune)
        return fGran.getSampleFineTune();

    if (index == paramGrainAttack)
        return fGran.getGrainAttackMs();

    if (index == paramGrainRelease)
        return fGran.getGrainReleaseMs();

    if (index == paramSampleStart)
        return fGran.getSampleStartNorm();

    if (index == paramSampleEnd)
        return fGran.getSampleEndNorm();

    if (index == paramAutoRoot)
        return fAutoRoot ? 1.0f : 0.0f;

    if (index == paramDelayMode) return float(fDelayMode);
    if (index == paramDelayTimeLeft) return fDelayTimeLeftMs;
    if (index == paramDelayTimeRight) return fDelayTimeRightMs;
    if (index == paramDelayFeedback) return fDelayFeedback;
    if (index == paramDelayMix) return fDelayMix;
    if (index == paramDelayDamping) return fDelayDamping;
    if (index == paramTimeStretch) return fGran.getTimeStretchRatio();
    if (index == paramPlaybackMode) return fGran.getPlaybackMode();
    if (index == paramSliceMode) return float(fGran.getSliceMode());
    if (index == paramSliceCount) return float(fGran.getSliceCount());

    return 0.0f;
}





void setParameterValue(uint32_t index, float value) override
{
    switch (index)
    {
    case paramReverbSize: 
        fReverbSize = value; break; // NYT

    case paramReverbMix: 
        fReverbMix = value; break;   // NYT

    case paramFilter: fFilter = value; 
        break;       // NYT

    case paramResonance: fResonance = value; 
        break; // NYT
    
    case paramVolume:
        fVolume = value;
        break;

    case paramDensity:
        fGran.setBaseDensityNorm(value);
        break;

    case paramVelocityToDensity:
        fVelocityAmount = value;
        fGran.setVelToDensity(value);
        break;

    case paramVelocityToGrainSize:
        fVelocityGrainSize = value;
        fGran.setVelToGrainSize(value);
        break;

    case paramPitchRate:
        fGran.setPitchRateParam(value);
        break;

    case paramRelease:
        fGran.setReleaseMs(value);
        break;

    case paramStartPosition:
        fGran.setStartPosNorm(value);
        break;

    case paramPositionSpread:
        fGran.setPosSpreadNorm(value);
        break;

    case paramSnapMs:
        fGran.setSnapMs(value);
        break;

    case paramScanSpeed:
        fGran.setScanSpeed(value);
        break;

    case paramScanMode:
        fGran.setScanMode(std::round(value));
        gDrumCloudUiScanMode.store((int)std::lround(value), std::memory_order_relaxed);
        break;

    case paramScanJumpRate:
        fGran.setScanJumpRateHz(value);
        break;

    case paramScanJumpAmount:
        fGran.setScanJumpAmount(value);
        break;

    case paramScanJumpSmoothMs:
        fGran.setScanJumpSmoothMs(value);
        break;

    case paramSyncRate:
        fGran.setTempoSyncRateChoice(value);
        break;

    case paramSamplePath:
        dirtyDbgLog("setParameterValue paramSamplePath <- %.9f", value);
        fSamplePing = value;
        break;

    case paramRootNote:
        fGran.setRootNote(value);
        break;

    case paramSampleFineTune:
        fGran.setSampleFineTune(value);
        break;

    case paramGrainAttack:
        fGran.setGrainAttackMs(value);
        break;

    case paramGrainRelease:
        fGran.setGrainReleaseMs(value);
        break;

    case paramSampleStart:
        fGran.setSampleStartNorm(value);
        break;

    case paramSampleEnd:
        fGran.setSampleEndNorm(value);
        break;

    case paramAutoRoot:
        fAutoRoot = value >= 0.5f;
        break;

    case paramDelayMode:
        fDelayMode = std::clamp(int(std::lround(value)), 0, 2);
        break;

    case paramDelayTimeLeft:
        fDelayTimeLeftMs = std::clamp(value, 1.0f, 2000.0f);
        break;

    case paramDelayTimeRight:
        fDelayTimeRightMs = std::clamp(value, 1.0f, 2000.0f);
        break;

    case paramDelayFeedback:
        fDelayFeedback = std::clamp(value, 0.0f, 0.90f);
        break;

    case paramDelayMix:
        fDelayMix = std::clamp(value, 0.0f, 1.0f);
        break;

    case paramDelayDamping:
        fDelayDamping = std::clamp(value, 0.0f, 1.0f);
        break;

    case paramTimeStretch:
        fGran.setTimeStretchRatio(value);
        break;

    case paramPlaybackMode:
        fGran.setPlaybackMode(value);
        break;

    case paramSliceMode:
        fGran.setSliceMode(value);
        break;

    case paramSliceCount:
        fGran.setSliceCount(value);
        break;

    default:
        break;
    }
}






void initState(uint32_t index, State& state) override
{
    if (index != stateSamplePath)
        return;

    state.key = "samplePath";
    state.defaultValue = "";
    state.label = "Sample Path";
    state.hints = kStateIsFilenamePath; // <-- din DPF har denne
}

// --- Program / Preset Opsætning ---
void initProgramName(uint32_t index, String& programName) override
{
    if (index == 0)
        programName = "Default";
}

void loadProgram(uint32_t index) override
{
        if (index == 0)
        {
            setParameterValue(paramVolume, 1.0f);
            setParameterValue(paramDensity, 0.72f);
            setParameterValue(paramVelocityToDensity, 0.42f);
            setParameterValue(paramVelocityToGrainSize, 0.58f);
            setParameterValue(paramPitchRate, 1.0f);
            setParameterValue(paramRelease, 452.5f);
            setParameterValue(paramStartPosition, 0.0f);
            setParameterValue(paramPositionSpread, 0.0f);
            setParameterValue(paramSnapMs, 10.0f);
            setParameterValue(paramScanSpeed, 0.5f);
            setParameterValue(paramScanMode, 2.0f);
            setParameterValue(paramScanJumpRate, 27.0f);
            setParameterValue(paramScanJumpAmount, 0.69f);
            setParameterValue(paramScanJumpSmoothMs, 272.0f);
            setParameterValue(paramSyncRate, 1.0f);
            
            setParameterValue(paramFilter, 0.5f);
            setParameterValue(paramResonance, 0.0f);

            // 👇 De to nye reverb-parametre 👇
            setParameterValue(paramReverbSize, 0.8f);
            setParameterValue(paramReverbMix, 0.0f);
            setParameterValue(paramRootNote, 60.0f);
            setParameterValue(paramSampleFineTune, 0.0f);
            setParameterValue(paramGrainAttack, 10.0f);
            setParameterValue(paramGrainRelease, 80.0f);
            setParameterValue(paramSampleStart, 0.0f);
            setParameterValue(paramSampleEnd, 1.0f);
            setParameterValue(paramAutoRoot, 1.0f);
            setParameterValue(paramDelayMode, 0.0f);
            setParameterValue(paramDelayTimeLeft, 375.0f);
            setParameterValue(paramDelayTimeRight, 500.0f);
            setParameterValue(paramDelayFeedback, 0.35f);
            setParameterValue(paramDelayMix, 0.25f);
            setParameterValue(paramDelayDamping, 0.35f);
            setParameterValue(paramTimeStretch, 1.0f);
            setParameterValue(paramPlaybackMode, 0.0f);
            setParameterValue(paramSliceMode, 0.0f);
            setParameterValue(paramSliceCount, 8.0f);
        }
}

// 🔹 TRIN 3 – Parameter-definition
void initParameter(uint32_t index, Parameter& parameter) override
{
    parameter.hints = kParameterIsAutomatable;
    parameter.ranges.min = 0.0f;
    parameter.ranges.max = 1.0f;
    parameter.ranges.def = 0.0f; 

    switch (index)
    {
    case paramVolume:
        parameter.name   = "Volume";
        parameter.symbol = "volume";
        parameter.ranges.def = 1.0f;
        break;

    case paramDensity:
        parameter.name   = "Density";
        parameter.symbol = "density";
        parameter.ranges.def = 0.72f;
        break;

    case paramVelocityToDensity:
        parameter.name   = "Velocity -> Density";
        parameter.symbol = "vel_density";
        parameter.ranges.def = 0.42f;
        break;

    case paramVelocityToGrainSize:
        parameter.name   = "Velocity -> Grain Size";
        parameter.symbol = "vel_grain";
        parameter.ranges.def = 0.58f;
        break;

    case paramPitchRate:
        parameter.name   = "Pitch Rate";
        parameter.symbol = "pitch_rate";
        parameter.ranges.min = 0.5f;
        parameter.ranges.max = 2.0f;
        parameter.ranges.def = 1.0f;
        break;

    case paramRelease:
        parameter.name   = "Release";
        parameter.symbol = "release";
        parameter.unit   = "ms";
        parameter.ranges.min = 5.0f;
        parameter.ranges.max = 5000.0f;
        parameter.ranges.def = 452.5f;
        break;

    case paramStartPosition:
        parameter.name   = "Start Position";
        parameter.symbol = "start_pos";
        parameter.ranges.def = 0.0f;
        break;

    case paramPositionSpread:
        parameter.name   = "Position Spread";
        parameter.symbol = "pos_spread";
        parameter.ranges.def = 0.0f;
        break;

    case paramSnapMs:
        parameter.name   = "Start Snap (ms)";
        parameter.symbol = "snap_ms";
        parameter.unit   = "ms";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 50.0f;
        parameter.ranges.def = 10.0f;
        break;

    case paramScanSpeed:
        parameter.name   = "Scan Speed";
        parameter.symbol = "scan_speed";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 2.0f;
        parameter.ranges.def = 0.5f;
        break;

    case paramScanMode:
        parameter.name   = "Scan Mode";
        parameter.symbol = "scan_mode";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 3.0f;
        parameter.ranges.def = 2.0f;
        break;

    case paramScanJumpRate:
        parameter.name   = "Jump Rate";
        parameter.symbol = "jump_rate";
        parameter.unit   = "Hz";
        parameter.ranges.min = 0.1f;
        parameter.ranges.max = 40.0f;
        parameter.ranges.def = 27.0f;
        break;

    case paramScanJumpAmount:
        parameter.name   = "Jump Amount";
        parameter.symbol = "jump_amount";
        parameter.ranges.def = 0.69f;
        break;

    case paramScanJumpSmoothMs:
        parameter.name   = "Jump Smooth (ms)";
        parameter.symbol = "jump_smooth_ms";
        parameter.unit   = "ms";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 500.0f;
        parameter.ranges.def = 272.0f;
        break;

    case paramSyncRate:
        parameter.name   = "Sync Rate";
        parameter.symbol = "sync_rate";
        parameter.ranges.def = 1.0f;
        break;

    case paramFilter:
        parameter.name   = "Filter";
        parameter.symbol = "filter";
        parameter.ranges.def = 0.5f;
        break;

    case paramResonance:
        parameter.name   = "Resonance";
        parameter.symbol = "resonance";
        parameter.ranges.def = 0.0f;
        break;

    // 👇 NYT 👇
    case paramReverbSize:
        parameter.name   = "Reverb Size";
        parameter.symbol = "reverb_size";
        parameter.ranges.def = 0.8f;
        break;

    case paramReverbMix:
        parameter.name   = "Reverb Mix";
        parameter.symbol = "reverb_mix";
        parameter.ranges.def = 0.0f;
        break;
    // 👆 NYT 👆

    case paramSamplePath:
        parameter.name   = "[internal] Sample Sync";
        parameter.symbol = "samplepath_sync";
        parameter.hints  = kParameterIsHidden;
        parameter.ranges.def = 0.0f;
        break;

    case paramScanPos:
        parameter.name   = "Scan Pos";
        parameter.symbol = "scan_pos";
        parameter.hints  = kParameterIsHidden | kParameterIsOutput;
        parameter.ranges.def = 0.0f;
        break;

    case paramRootNote:
        parameter.name   = "Root MIDI Note";
        parameter.symbol = "root_note";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 127.0f;
        parameter.ranges.def = 60.0f;
        break;

    case paramSampleFineTune:
        parameter.name   = "Sample Fine Tune";
        parameter.symbol = "sample_fine_tune";
        parameter.unit   = "ct";
        parameter.ranges.min = -100.0f;
        parameter.ranges.max = 100.0f;
        parameter.ranges.def = 0.0f;
        break;

    case paramGrainAttack:
        parameter.name   = "Grain Attack";
        parameter.symbol = "grain_attack";
        parameter.unit   = "ms";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 500.0f;
        parameter.ranges.def = 10.0f;
        break;

    case paramGrainRelease:
        parameter.name   = "Grain Release";
        parameter.symbol = "grain_release";
        parameter.unit   = "ms";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1000.0f;
        parameter.ranges.def = 80.0f;
        break;

    case paramSampleStart:
        parameter.name   = "Sample Start";
        parameter.symbol = "sample_start";
        parameter.ranges.def = 0.0f;
        break;

    case paramSampleEnd:
        parameter.name   = "Sample End";
        parameter.symbol = "sample_end";
        parameter.ranges.def = 1.0f;
        break;

    case paramAutoRoot:
        parameter.name   = "Auto Root";
        parameter.symbol = "auto_root";
        parameter.hints  = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
        parameter.ranges.def = 1.0f;
        break;

    case paramDelayMode:
        parameter.name   = "Delay Mode";
        parameter.symbol = "delay_mode";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 2.0f;
        parameter.ranges.def = 0.0f;
        break;

    case paramDelayTimeLeft:
        parameter.name   = "Delay Time Left";
        parameter.symbol = "delay_time_l";
        parameter.unit   = "ms";
        parameter.ranges.min = 1.0f;
        parameter.ranges.max = 2000.0f;
        parameter.ranges.def = 375.0f;
        break;

    case paramDelayTimeRight:
        parameter.name   = "Delay Time Right";
        parameter.symbol = "delay_time_r";
        parameter.unit   = "ms";
        parameter.ranges.min = 1.0f;
        parameter.ranges.max = 2000.0f;
        parameter.ranges.def = 500.0f;
        break;

    case paramDelayFeedback:
        parameter.name   = "Delay Feedback";
        parameter.symbol = "delay_feedback";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 0.90f;
        parameter.ranges.def = 0.35f;
        break;

    case paramDelayMix:
        parameter.name   = "Delay Mix";
        parameter.symbol = "delay_mix";
        parameter.ranges.def = 0.25f;
        break;

    case paramDelayDamping:
        parameter.name   = "Delay Damping";
        parameter.symbol = "delay_damping";
        parameter.ranges.def = 0.35f;
        break;

    case paramTimeStretch:
        parameter.name   = "Time Stretch";
        parameter.symbol = "time_stretch";
        parameter.unit   = "x";
        parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = DrumCloudTimeStretch::kMinimumRatio;
        parameter.ranges.max = DrumCloudTimeStretch::kMaximumRatio;
        parameter.ranges.def = 1.0f;
        break;

    case paramPlaybackMode:
        parameter.name   = "Playback Mode";
        parameter.symbol = "playback_mode";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 2.0f;
        parameter.ranges.def = 0.0f;
        break;

    case paramSliceMode:
        parameter.name   = "Slicer Mode";
        parameter.symbol = "slice_mode";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 2.0f;
        parameter.ranges.def = 0.0f;
        break;

    case paramSliceCount:
        parameter.name   = "Slice Count";
        parameter.symbol = "slice_count";
        parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 2.0f;
        parameter.ranges.max = 16.0f;
        parameter.ranges.def = 8.0f;
        break;
    }
}













   /* --------------------------------------------------------------------------------------------------------
    * Init */

   /**
      Initialize the audio port @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        // treat meter audio ports as stereo
        port.groupId = kPortGroupMono;

        // everything else is as default
        Plugin::initAudioPort(input, index, port);
    }

   /* --------------------------------------------------------------------------------------------------------
    * Audio/MIDI Processing */

   /**
      Run/process function for plugins with MIDI input.
      This synthesizes the MIDI voices with a sum of sine waves.
    */
    /**
      Run/process function for plugins with MIDI input.
    */
    void run(const float**, float** outputs, uint32_t frames,
         const MidiEvent* midiEvents, uint32_t midiEventCount) override
{
    float* outL = outputs[0];
    float* outR = outputs[1];

    // --- init engine if needed ---
    if (!fGranInit || fLastSR != getSampleRate())
    {
        fGran.init(getSampleRate());
        m_reverb.init(getSampleRate()); // 👈 VIGTIGT: Starter rumklangen
        m_delay.init(float(getSampleRate()));
        fLimiterReleaseCoeff = 1.0f - std::exp(-1.0f / (0.100f * float(std::max(1.0, getSampleRate()))));
        fLastSR = getSampleRate();
        fGranInit = true;
    }

    // The audio callback only exchanges pointers and fixed-size preview data.
    // Wait for the worker to reclaim the previous sample before accepting another.
    if (fRetired.load(std::memory_order_acquire) == nullptr)
    {
        if (GranularEngine::PreparedSample* ready =
                fReady.exchange(nullptr, std::memory_order_acq_rel))
        {
            fGran.applyPreparedSample(*ready);

            const bool confident = ready->pitch.valid && ready->pitch.confidence >= 0.70f;
            gDrumCloudDetectedRoot.store(confident ? ready->pitch.midiNote : -1, std::memory_order_relaxed);
            gDrumCloudDetectedFine.store(confident ? ready->pitch.fineTuneCents : 0.0f, std::memory_order_relaxed);
            gDrumCloudDetectedConfidence.store(ready->pitch.confidence, std::memory_order_relaxed);

            if (fAutoRoot && confident)
            {
                fGran.setRootNote(float(ready->pitch.midiNote));
                fGran.setSampleFineTune(ready->pitch.fineTuneCents);
            }

            gDrumCloudDetectedPitchGeneration.fetch_add(1, std::memory_order_release);
            fRetired.store(ready, std::memory_order_release);
        }
    }

    if (fGranInit && !fTriedRestore)
    {
        fTriedRestore = true;
    }

    // --- host tempo -> engine ---
    {
        float bpm = 120.0f;
        bool playing = false;
       #if DISTRHO_PLUGIN_WANT_TIMEPOS
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid)
        {
            bpm = float(tp.bbt.beatsPerMinute);
            const double sr = std::max(1.0, double(getSampleRate()));
            const double beatsPerSecond = double(tp.bbt.beatsPerMinute) / 60.0;
            const double beatPos = (double(tp.frame) / sr) * beatsPerSecond * double(fGran.getTempoSyncRateMultiplier());
            const double beatPhase = beatPos - std::floor(beatPos);
            fGran.setTempoSyncPhase(float(beatPhase));
        }
        playing = tp.playing;
       #endif
        fGran.setTempoSyncInfo(bpm, playing);
    }

    // --- handle MIDI -> granular engine ---
    for (uint32_t i = 0; i < midiEventCount; ++i)
    {
        const MidiEvent& ev = midiEvents[i];
        if (ev.size < 3) continue;

        const uint8_t st   = ev.data[0] & 0xF0;
        const uint8_t note = ev.data[1] & 0x7F;
        const uint8_t vel  = ev.data[2] & 0x7F;

        if (st == 0x90 && vel > 0)
            fGran.trigger(note, vel);
        else if (st == 0x80 || (st == 0x90 && vel == 0))
            fGran.noteOff(note);
    }

    // --- render audio ---
    std::memset(outL, 0, sizeof(float) * frames);
    std::memset(outR, 0, sizeof(float) * frames);

    // 1. Generer lyden fra skyerne
    fGran.process(outL, outR, frames);

    // 2. Kør lyden gennem Filteret! 👇
    if (std::fabs(fFilter - 0.5f) > 0.01f) { 
        float type = (fFilter < 0.5f) ? 0.0f : 1.0f; // 0 = LP, 1 = HP
        float cutoff = 20000.0f;
        
        if (type == 0.0f) {
            float norm = fFilter * 2.0f; 
            cutoff = 20.0f * std::pow(1000.0f, norm); 
        } else {
            float norm = (fFilter - 0.5f) * 2.0f; 
            cutoff = 20.0f * std::pow(1000.0f, norm);
        }
        
        cutoff = std::clamp(cutoff, 20.0f, (float)getSampleRate() * 0.49f);
        float res = std::clamp(fResonance, 0.0f, 0.95f); 

        for(uint32_t f=0; f<frames; ++f) {
            m_svf.process(outL[f], outR[f], cutoff, res, (float)getSampleRate(), type);
        }
    }

    // 3. Kør lyden gennem Rumklangen! 👇
    if (fReverbMix > 0.001f) {
        for(uint32_t f=0; f<frames; ++f) {
            m_reverb.process(outL[f], outR[f], fReverbSize, fReverbMix);
        }
    }

    // 4. Filtered stereo / ping-pong delay.
    m_delay.setParameters(fDelayMode, fDelayTimeLeftMs, fDelayTimeRightMs,
                          fDelayFeedback, fDelayMix, fDelayDamping);
    if (fDelayMode != 0)
    {
        for (uint32_t f = 0; f < frames; ++f)
            m_delay.process(outL[f], outR[f]);
    }

    // Send data til UI
    gDrumCloudUiScanPos.store(fGran.getScanPosNorm(), std::memory_order_relaxed);
    gDrumCloudUiScanMode.store((int)std::lround(fGran.getScanMode()), std::memory_order_relaxed);
    float grainPositions[kUiGrainMarkerCount]{};
    const uint32_t grainCount = fGran.copyActiveGrainPositions(grainPositions, kUiGrainMarkerCount);
    for (uint32_t i = 0; i < grainCount; ++i)
        gDrumCloudUiGrainPos[i].store(grainPositions[i], std::memory_order_relaxed);
    gDrumCloudUiGrainCount.store(grainCount, std::memory_order_release);
    gDrumCloudUiActiveSlice.store(fGran.getActiveSlice(), std::memory_order_relaxed);
    float sliceBoundaries[kUiSliceBoundaryCount]{};
    const uint32_t sliceBoundaryCount = fGran.copySliceBoundaries(
        sliceBoundaries, kUiSliceBoundaryCount);
    for (uint32_t i = 0; i < sliceBoundaryCount; ++i)
        gDrumCloudUiSliceBoundaries[i].store(sliceBoundaries[i], std::memory_order_relaxed);
    gDrumCloudUiSliceBoundaryCount.store(sliceBoundaryCount, std::memory_order_release);

    if (fScanMeterCountdown == 0)
    {
        const uint32_t framesPerTick = std::max<uint32_t>(1u, (uint32_t)(getSampleRate() / 30.0f));
        fScanMeterCountdown = framesPerTick;
    }
    else
    {
        fScanMeterCountdown = (fScanMeterCountdown > frames) ? (fScanMeterCountdown - frames) : 0;
    }

    // --- apply output gain and linked-stereo peak protection ---
    const float vol = fVolume * fVolume;
    constexpr float kPeakCeiling = 0.98f;
    for (uint32_t f = 0; f < frames; ++f)
    {
        float left = outL[f] * vol;
        float right = outR[f] * vol;

        if (!std::isfinite(left) || !std::isfinite(right))
        {
            outL[f] = 0.0f;
            outR[f] = 0.0f;
            fLimiterGain = 0.0f;
            continue;
        }

        const float peak = std::max(std::fabs(left), std::fabs(right));
        const float target = peak > kPeakCeiling ? kPeakCeiling / peak : 1.0f;
        if (target < fLimiterGain)
            fLimiterGain = target; // immediate attack
        else
            fLimiterGain += (1.0f - fLimiterGain) * fLimiterReleaseCoeff;

        outL[f] = left * fLimiterGain;
        outR[f] = right * fLimiterGain;
    }
}







private:
    void workerLoop()
    {
        for (;;)
        {
            // Reclaim old buffers on this thread, never in the audio callback.
            delete fRetired.exchange(nullptr, std::memory_order_acq_rel);
            std::string path;
            {
                std::unique_lock<std::mutex> lock(fWorkerMutex);
                fWorkerCV.wait_for(lock, std::chrono::milliseconds(20),
                    [this]{ return fWorkerStop || fWorkerRequest ||
                        fRetired.load(std::memory_order_acquire) != nullptr; });
                if (fWorkerStop) break;
                if (!fWorkerRequest) continue;
                path.swap(fWorkerPath);
                fWorkerRequest = false;
            }
            // A newer request replaces a result that has not yet reached audio.
            delete fReady.exchange(nullptr, std::memory_order_acq_rel);
            std::unique_ptr<GranularEngine::PreparedSample> prepared;
            try {
                prepared = GranularEngine::prepareSample(path);
            } catch (...) {
                // An allocation failure must not terminate the worker or the host.
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(fWorkerMutex);
                if (fWorkerStop || fWorkerRequest || !prepared)
                    continue;
                delete fReady.exchange(prepared.release(), std::memory_order_acq_rel);
            }
        }
        delete fRetired.exchange(nullptr, std::memory_order_acq_rel);
    }

    std::mutex fWorkerMutex;
    std::condition_variable fWorkerCV;
    std::thread fWorker;
    std::string fWorkerPath;
    bool fWorkerRequest = false;
    bool fWorkerStop = false;
    std::atomic<GranularEngine::PreparedSample*> fReady{nullptr};
    std::atomic<GranularEngine::PreparedSample*> fRetired{nullptr};

    std::string fSamplePath;
    uint32_t fSampleId = 0;
    float fSamplePing = 0.0f;
    
    GranularEngine fGran;
    bool fGranInit = false;

    float fVolume = 1.0f;
    float fLimiterGain = 1.0f;
    float fLimiterReleaseCoeff = 0.001f;
    float fVelocityAmount = 0.42f;
    float fVelocityGrainSize = 0.58f;
    bool fAutoRoot = true;
    double fLastSR = 0.0;
    bool fTriedRestore = false;
    float fReleaseMs = 452.5f; 
    uint32_t fScanMeterCountdown = 0;    

    float fFilter = 0.5f;
    float fResonance = 0.0f;
    SvfStereo m_svf;

    // 👇 Her sætter du dem ind! 👇
    float fReverbSize = 0.8f;
    float fReverbMix = 0.0f;
    CloudReverb m_reverb;

    int fDelayMode = 0;
    float fDelayTimeLeftMs = 375.0f;
    float fDelayTimeRightMs = 500.0f;
    float fDelayFeedback = 0.35f;
    float fDelayMix = 0.25f;
    float fDelayDamping = 0.35f;
    FilteredStereoDelay m_delay;
}; // <-- Slutningen af din Plugin-klasse








    // -------------------------------------------------------------------------------------------------------

/* ------------------------------------------------------------------------------------------------------------
 * Plugin entry point, called by DPF to create a new plugin instance. */

Plugin* createPlugin()
{
    return new SendNoteExamplePlugin();
}

END_NAMESPACE_DISTRHO
