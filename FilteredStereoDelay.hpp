#pragma once

#include <cstdint>
#include <vector>

class FilteredStereoDelay
{
public:
    void init(float sampleRate, float maxDelayMs = 2000.0f);
    void reset() noexcept;
    void setParameters(int mode, float timeLeftMs, float timeRightMs,
                       float feedback, float mix, float damping) noexcept;
    void process(float& left, float& right) noexcept;

private:
    float readInterpolated(const std::vector<float>& buffer, float delaySamples) const noexcept;

    std::vector<float> fBufferLeft;
    std::vector<float> fBufferRight;
    uint32_t fWrite = 0;
    float fSampleRate = 48000.0f;
    int fMode = 0;
    bool fTimesInitialized = false;
    float fDelayLeft = 18000.0f;
    float fDelayRight = 24000.0f;
    float fTargetDelayLeft = 18000.0f;
    float fTargetDelayRight = 24000.0f;
    float fFeedback = 0.35f;
    float fMix = 0.25f;
    float fDamping = 0.35f;
    float fLowpassLeft = 0.0f;
    float fLowpassRight = 0.0f;
};
