#include "FilteredStereoDelay.hpp"

#include <algorithm>
#include <cmath>

void FilteredStereoDelay::init(float sampleRate, float maxDelayMs)
{
    fSampleRate = std::max(8000.0f, sampleRate);
    const uint32_t size = std::max<uint32_t>(4, uint32_t(std::ceil(fSampleRate * maxDelayMs * 0.001f)) + 4u);
    fBufferLeft.assign(size, 0.0f);
    fBufferRight.assign(size, 0.0f);
    fWrite = 0;
    fTimesInitialized = false;
    fLowpassLeft = fLowpassRight = 0.0f;
}

void FilteredStereoDelay::reset() noexcept
{
    std::fill(fBufferLeft.begin(), fBufferLeft.end(), 0.0f);
    std::fill(fBufferRight.begin(), fBufferRight.end(), 0.0f);
    fWrite = 0;
    fLowpassLeft = fLowpassRight = 0.0f;
}

void FilteredStereoDelay::setParameters(int mode, float timeLeftMs, float timeRightMs,
                                        float feedback, float mix, float damping) noexcept
{
    const int nextMode = std::clamp(mode, 0, 2);
    const float maxSamples = fBufferLeft.empty() ? 2.0f : float(fBufferLeft.size() - 2u);
    fTargetDelayLeft = std::clamp(timeLeftMs * 0.001f * fSampleRate, 1.0f, maxSamples);
    fTargetDelayRight = std::clamp(timeRightMs * 0.001f * fSampleRate, 1.0f, maxSamples);
    fFeedback = std::clamp(feedback, 0.0f, 0.90f);
    fMix = std::clamp(mix, 0.0f, 1.0f);
    fDamping = std::clamp(damping, 0.0f, 1.0f);

    if (!fTimesInitialized || nextMode != fMode)
    {
        fDelayLeft = fTargetDelayLeft;
        fDelayRight = fTargetDelayRight;
        fTimesInitialized = true;
    }
    fMode = nextMode;
}

float FilteredStereoDelay::readInterpolated(const std::vector<float>& buffer, float delaySamples) const noexcept
{
    if (buffer.empty()) return 0.0f;
    float position = float(fWrite) - delaySamples;
    const float size = float(buffer.size());
    while (position < 0.0f) position += size;
    while (position >= size) position -= size;
    const uint32_t i0 = uint32_t(position);
    const uint32_t i1 = (i0 + 1u) % uint32_t(buffer.size());
    const float fraction = position - float(i0);
    return buffer[i0] + (buffer[i1] - buffer[i0]) * fraction;
}

void FilteredStereoDelay::process(float& left, float& right) noexcept
{
    if (fMode == 0 || fBufferLeft.empty()) return;

    // About 10 ms of time smoothing at 48 kHz; block-independent and click resistant.
    const float timeSlew = 1.0f - std::exp(-1.0f / (0.010f * fSampleRate));
    fDelayLeft += (fTargetDelayLeft - fDelayLeft) * timeSlew;
    fDelayRight += (fTargetDelayRight - fDelayRight) * timeSlew;

    const float tapLeft = readInterpolated(fBufferLeft, fDelayLeft);
    const float tapRight = readInterpolated(fBufferRight, fDelayRight);
    const float lowpassAmount = std::max(0.01f, 1.0f - 0.96f * fDamping);
    fLowpassLeft += (tapLeft - fLowpassLeft) * lowpassAmount;
    fLowpassRight += (tapRight - fLowpassRight) * lowpassAmount;

    const float inputLeft = left;
    const float inputRight = right;
    const float feedbackLeft = fMode == 2 ? fLowpassRight : fLowpassLeft;
    const float feedbackRight = fMode == 2 ? fLowpassLeft : fLowpassRight;
    fBufferLeft[fWrite] = std::tanh(inputLeft + feedbackLeft * fFeedback);
    fBufferRight[fWrite] = std::tanh(inputRight + feedbackRight * fFeedback);
    if (++fWrite >= fBufferLeft.size()) fWrite = 0;

    const float dryGain = std::cos(fMix * 1.57079632679489661923f);
    const float wetGain = std::sin(fMix * 1.57079632679489661923f);
    left = inputLeft * dryGain + tapLeft * wetGain;
    right = inputRight * dryGain + tapRight * wetGain;
}
