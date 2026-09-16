#include "../PitchDetector.hpp"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static std::vector<float> sine(float hz, float sr, float seconds)
{
    std::vector<float> out(size_t(sr * seconds));
    for (size_t i=0;i<out.size();++i) out[i]=0.7f*std::sin(2.0*3.141592653589793*hz*double(i)/sr);
    return out;
}

int main()
{
    const uint32_t sr=48000;
    auto a=sine(440.0f,sr,1.0f);
    auto ra=detectSamplePitch(a.data(),nullptr,a.size(),sr);
    std::printf("A4 valid=%d note=%d fine=%.2f conf=%.3f hz=%.2f\n",ra.valid,ra.midiNote,ra.fineTuneCents,ra.confidence,ra.frequencyHz);
    if(!ra.valid||ra.midiNote!=69||std::fabs(ra.fineTuneCents)>2.0f) return 1;

    const float c4=261.625565f*std::pow(2.0f,25.0f/1200.0f);
    auto c=sine(c4,sr,1.0f);
    auto rc=detectSamplePitch(c.data(),nullptr,c.size(),sr);
    std::printf("C4+25 valid=%d note=%d fine=%.2f conf=%.3f hz=%.2f\n",rc.valid,rc.midiNote,rc.fineTuneCents,rc.confidence,rc.frequencyHz);
    if(!rc.valid||rc.midiNote!=60||std::fabs(rc.fineTuneCents+25.0f)>3.0f) return 2;

    std::mt19937 gen(1234); std::uniform_real_distribution<float> dist(-1.0f,1.0f);
    std::vector<float> noise(sr); for(float& v:noise) v=dist(gen);
    auto rn=detectSamplePitch(noise.data(),nullptr,noise.size(),sr);
    std::printf("noise valid=%d conf=%.3f\n",rn.valid,rn.confidence);
    if(rn.valid && rn.confidence>=0.60f) return 3;
    return 0;
}
