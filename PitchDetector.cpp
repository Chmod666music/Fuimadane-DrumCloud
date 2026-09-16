#include "PitchDetector.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Candidate
{
    float midi = 0.0f;
    float frequency = 0.0f;
    float confidence = 0.0f;
};

Candidate analyzeWindow(const float* left, const float* right,
                        uint32_t start, uint32_t frames,
                        uint32_t step, float analysisRate)
{
    const uint32_t available = frames > start ? frames - start : 0;
    const uint32_t count = std::min<uint32_t>(4096, available / step);
    if (count < 1024) return {};

    std::vector<float> x(count);
    double mean = 0.0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t at = start + i * step;
        const float mono = right ? 0.5f * (left[at] + right[at]) : left[at];
        x[i] = mono;
        mean += mono;
    }
    mean /= double(count);

    double energy = 0.0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const float window = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * float(i) / float(count - 1));
        x[i] = float(double(x[i]) - mean) * window;
        energy += double(x[i]) * double(x[i]);
    }
    if (std::sqrt(energy / double(count)) < 1.0e-4) return {};

    const uint32_t minTau = std::max<uint32_t>(2, uint32_t(analysisRate / 1200.0f));
    const uint32_t maxTau = std::min<uint32_t>(count / 2, uint32_t(analysisRate / 35.0f));
    if (maxTau <= minTau + 2) return {};

    std::vector<float> cmnd(maxTau + 1, 1.0f);
    double cumulative = 0.0;
    for (uint32_t tau = 1; tau <= maxTau; ++tau)
    {
        double difference = 0.0;
        const uint32_t limit = count - maxTau;
        for (uint32_t i = 0; i < limit; ++i)
        {
            const double d = double(x[i]) - double(x[i + tau]);
            difference += d * d;
        }
        cumulative += difference;
        cmnd[tau] = cumulative > 0.0 ? float(difference * double(tau) / cumulative) : 1.0f;
    }

    uint32_t bestTau = 0;
    for (uint32_t tau = minTau + 1; tau < maxTau; ++tau)
    {
        if (cmnd[tau] < 0.18f && cmnd[tau] <= cmnd[tau - 1] && cmnd[tau] < cmnd[tau + 1])
        {
            bestTau = tau;
            break;
        }
    }
    if (bestTau == 0)
    {
        bestTau = minTau;
        for (uint32_t tau = minTau + 1; tau <= maxTau; ++tau)
            if (cmnd[tau] < cmnd[bestTau]) bestTau = tau;
    }

    const float confidence = std::clamp(1.0f - cmnd[bestTau], 0.0f, 1.0f);
    if (confidence < 0.55f) return {};

    float refinedTau = float(bestTau);
    if (bestTau > 1 && bestTau < maxTau)
    {
        const float a = cmnd[bestTau - 1];
        const float b = cmnd[bestTau];
        const float c = cmnd[bestTau + 1];
        const float denominator = a - 2.0f * b + c;
        if (std::fabs(denominator) > 1.0e-8f)
            refinedTau += 0.5f * (a - c) / denominator;
    }

    const float frequency = analysisRate / refinedTau;
    if (!(frequency >= 35.0f && frequency <= 1200.0f)) return {};
    const float midi = 69.0f + 12.0f * std::log2(frequency / 440.0f);
    return {midi, frequency, confidence};
}

} // namespace

PitchDetectionResult detectSamplePitch(const float* left, const float* right,
                                       uint32_t frames, uint32_t sampleRate)
{
    PitchDetectionResult result;
    if (!left || frames < 1024 || sampleRate < 8000) return result;

    const uint32_t step = std::max<uint32_t>(1, uint32_t(std::lround(float(sampleRate) / 12000.0f)));
    const float analysisRate = float(sampleRate) / float(step);
    const uint32_t sourceWindow = 4096u * step;
    if (frames < 1024u * step) return result;

    std::vector<Candidate> candidates;
    constexpr uint32_t kWindows = 7;
    const uint32_t maxStart = frames > sourceWindow ? frames - sourceWindow : 0;
    for (uint32_t i = 0; i < kWindows; ++i)
    {
        const uint32_t start = kWindows > 1 ? uint32_t((uint64_t(maxStart) * i) / (kWindows - 1)) : 0;
        Candidate candidate = analyzeWindow(left, right, start, frames, step, analysisRate);
        if (candidate.confidence > 0.0f) candidates.push_back(candidate);
    }
    if (candidates.empty()) return result;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.midi < b.midi;
    });

    // A confidence-weighted median resists attacks, silence and occasional octave errors.
    float totalWeight = 0.0f;
    for (const Candidate& c : candidates) totalWeight += c.confidence * c.confidence;
    float accumulated = 0.0f;
    Candidate chosen = candidates.front();
    for (const Candidate& c : candidates)
    {
        accumulated += c.confidence * c.confidence;
        chosen = c;
        if (accumulated >= 0.5f * totalWeight) break;
    }

    float agreementWeight = 0.0f;
    float agreeingConfidence = 0.0f;
    for (const Candidate& c : candidates)
    {
        const float distance = std::fabs(c.midi - chosen.midi);
        if (distance < 0.55f)
        {
            const float weight = c.confidence * c.confidence;
            agreementWeight += weight;
            agreeingConfidence += weight * c.confidence;
        }
    }
    const float agreement = totalWeight > 0.0f ? agreementWeight / totalWeight : 0.0f;
    const float confidence = agreementWeight > 0.0f
        ? (agreeingConfidence / agreementWeight) * agreement
        : 0.0f;
    if (confidence < 0.60f) return result;

    const int root = std::clamp(int(std::lround(chosen.midi)), 0, 127);
    result.valid = true;
    result.midiNote = root;
    // Correction required to bring the measured pitch onto the equal-tempered root.
    result.fineTuneCents = std::clamp((float(root) - chosen.midi) * 100.0f, -100.0f, 100.0f);
    result.frequencyHz = chosen.frequency;
    result.confidence = std::clamp(confidence, 0.0f, 1.0f);
    return result;
}
