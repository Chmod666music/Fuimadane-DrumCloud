#define DRUMCLOUD_UI_DEBUG 0

#include "DistrhoUI.hpp"
#include "AudioFileLoader.hpp"
#include "DrumCloudParams.hpp"

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#ifdef __APPLE__
  #include <OpenGL/gl.h>
#else
  #include <GL/gl.h>
#endif

namespace DISTRHO {

struct DrumCloudKnobSpec
{
    uint32_t param;
    float x;
    float y;
    float radius;
    const char* label;
};

static constexpr DrumCloudKnobSpec kDrumCloudKnobs[] = {
    // Voice / grain
    { paramVolume, 70.0f, 248.0f, 19.0f, "VOL" },
    { paramDensity, 162.0f, 248.0f, 19.0f, "DENS" },
    { paramRelease, 254.0f, 248.0f, 19.0f, "REL" },
    { paramVelocityToDensity, 346.0f, 248.0f, 19.0f, "V DENS" },
    { paramVelocityToGrainSize, 70.0f, 326.0f, 19.0f, "V GSIZ" },
    { paramPitchRate, 162.0f, 326.0f, 19.0f, "PITCH" },
    { paramGrainAttack, 254.0f, 326.0f, 19.0f, "G ATK" },
    { paramGrainRelease, 346.0f, 326.0f, 19.0f, "G REL" },

    // Position / motion
    { paramStartPosition, 470.0f, 248.0f, 19.0f, "START" },
    { paramPositionSpread, 562.0f, 248.0f, 19.0f, "SPREAD" },
    { paramScanSpeed, 654.0f, 248.0f, 19.0f, "SCAN" },
    { paramSyncRate, 746.0f, 248.0f, 19.0f, "SYNC" },
    { paramScanJumpRate, 470.0f, 326.0f, 19.0f, "J RATE" },
    { paramScanJumpAmount, 562.0f, 326.0f, 19.0f, "J AMNT" },
    { paramScanJumpSmoothMs, 654.0f, 326.0f, 19.0f, "J SMTH" },
    { paramTimeStretch, 746.0f, 326.0f, 19.0f, "STRETCH" },

    // Tone / pitch
    { paramFilter, 72.0f, 454.0f, 20.0f, "FILTER" },
    { paramResonance, 166.0f, 454.0f, 20.0f, "RESO" },
    { paramRootNote, 260.0f, 454.0f, 20.0f, "ROOT" },
    { paramSampleFineTune, 354.0f, 454.0f, 20.0f, "FINE" },

    // Reverb
    { paramReverbSize, 548.0f, 454.0f, 22.0f, "SIZE" },
    { paramReverbMix, 680.0f, 454.0f, 22.0f, "MIX" },

    // Delay
    { paramDelayMode, 80.0f, 594.0f, 20.0f, "MODE" },
    { paramDelayTimeLeft, 212.0f, 594.0f, 20.0f, "TIME L" },
    { paramDelayTimeRight, 344.0f, 594.0f, 20.0f, "TIME R" },
    { paramDelayFeedback, 476.0f, 594.0f, 20.0f, "FDBK" },
    { paramDelayMix, 608.0f, 594.0f, 20.0f, "MIX" },
    { paramDelayDamping, 740.0f, 594.0f, 20.0f, "DAMP" }
};

extern std::atomic<float> gDrumCloudUiScanPos;
extern std::atomic<int>   gDrumCloudUiScanMode;
static constexpr uint32_t kUiGrainMarkerCount = 16;
extern std::atomic<uint32_t> gDrumCloudUiGrainCount;
extern std::atomic<float> gDrumCloudUiGrainPos[kUiGrainMarkerCount];
extern std::atomic<uint32_t> gDrumCloudDetectedPitchGeneration;
extern std::atomic<int> gDrumCloudDetectedRoot;
extern std::atomic<float> gDrumCloudDetectedFine;
extern std::atomic<float> gDrumCloudDetectedConfidence;

static constexpr uint32_t kMax24 = 0xFFFFFFu;

static uint32_t norm24ToId(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return kMax24;
    const float f = v * float(kMax24) - 0.5f;
    return (uint32_t)std::lround(f) & kMax24;
}

static std::string getCachePath()
{
    const char* home = std::getenv("HOME");
    if (!home)
        return "/tmp/drumcloud-sample-cache.txt";
    return std::string(home) + "/.config/drumcloud-sample-cache.txt";
}

static bool cacheRead(uint32_t id, std::string& outPath)
{
    outPath.clear();
    if (id == 0) return false;

    const std::string fn = getCachePath();
    FILE* fp = std::fopen(fn.c_str(), "r");
    if (!fp) return false;

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

class DrumCloudUI : public UI
{
public:
    DrumCloudUI()
        : UI(820, 700)
    {
        fPreviewThread = std::thread([this]{ previewLoop(); });
    }

    ~DrumCloudUI() override
    {
        {
            std::lock_guard<std::mutex> lock(fPreviewMutex);
            fPreviewStop = true;
        }
        fPreviewCV.notify_one();
        if (fPreviewThread.joinable()) fPreviewThread.join();
    }

protected:
    void parameterChanged(uint32_t index, float value) override;
    void stateChanged(const char* key, const char* value) override;
    void onDisplay() override;
    void uiIdle() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;

private:
    static constexpr int kWavePreviewSize = 1024;
    float fWaveMin[kWavePreviewSize]{};
    float fWaveMax[kWavePreviewSize]{};
    bool  fWaveValid = false;

    float fScanPosUI = 0.0f;
    int   fScanModeUi = 0;
    int   fPlaybackModeUi = 0;
    float fGrainPosUI[kUiGrainMarkerCount]{};
    uint32_t fGrainCountUI = 0;
    
    // UI Parameter values
    float fVolumeUi = 0.8f;
    float fDensityUi = 0.25f;
    float fReleaseMsUi = 452.5f; 
    float fStartPosUi = 0.0f;
    float fSpreadUi = 0.0f;
    float fScanSpeedUi = 0.12f;
    float fFilterUi = 0.5f;     
    float fResoUi = 0.0f;       
    float fReverbSizeUi = 0.8f; 
    float fReverbMixUi = 0.0f;  
    float fVelToDensityUi = 0.35f;
    float fVelToGrainUi = 0.50f;
    float fPitchRateUi = 1.00f;
    float fJumpRateUi = 1.20f;
    float fJumpAmountUi = 0.18f;
    float fJumpSmoothMsUi = 140.0f;
    float fSyncRateUi = 1.0f;
    float fRootNoteUi = 60.0f;
    float fSampleFineTuneUi = 0.0f;
    float fGrainAttackMsUi = 10.0f;
    float fGrainReleaseMsUi = 80.0f;
    float fTimeStretchUi = 1.0f;
    float fSampleStartUi = 0.0f;
    float fSampleEndUi = 1.0f;
    float fAutoRootUi = 1.0f;
    int fDetectedRootUi = -1;
    float fDetectedFineUi = 0.0f;
    float fDetectedConfidenceUi = 0.0f;
    uint32_t fDetectedPitchGenerationUi = 0;
    float fDelayModeUi = 0.0f;
    float fDelayTimeLeftUi = 375.0f;
    float fDelayTimeRightUi = 500.0f;
    float fDelayFeedbackUi = 0.35f;
    float fDelayMixUi = 0.25f;
    float fDelayDampingUi = 0.35f;

    // Interaction states
    bool  fDragKnob = false;
    uint32_t fDragKnobParam = 0xffffffffu;
    float fKnobDragStartX = 0.0f;
    float fKnobDragStartValue = 0.0f;
    bool  fDragStartPos = false;
    bool  fDragSampleStart = false;
    bool  fDragSampleEnd = false;
    
    // Hover & Double click
    uint32_t fHoverKnobParam = 0xffffffffu;
    uint32_t fLastClickParam = 0xffffffffu;
    std::chrono::steady_clock::time_point fLastClickTime;

    float fSamplePing = 0.0f;
    std::string fSamplePath;
    bool fRestoringFromParam = false;
    bool fChoosingSample = false;

    struct PreviewResult {
        AudioPreview preview;
        std::string path;
        std::string error;
        uint64_t generation = 0;
        bool chosen = false;
        bool success = false;
    };
    void previewLoop();
    std::mutex fPreviewMutex;
    std::condition_variable fPreviewCV;
    std::thread fPreviewThread;
    std::unique_ptr<PreviewResult> fPreviewResult;
    std::string fPreviewPath;
    uint64_t fPreviewGeneration = 0;
    bool fPreviewPending = false;
    bool fPreviewChosen = false;
    bool fPreviewStop = false;
    std::string fLoadStatus;
    bool fLoadError = false;
    float getParamMin(uint32_t param) const;
    float getParamMax(uint32_t param) const;
    float getParamDef(uint32_t param) const;
    float getParamUiValue(uint32_t param) const;
    void  setParamUiValue(uint32_t param, float value);
    bool  hitKnob(float mx, float my, float cx, float cy, float r) const;
    uint32_t knobAt(float mx, float my) const;
    float normaliseParamValue(uint32_t param, float value) const;
    void formatParamValue(uint32_t param, float value, char* text, std::size_t size) const;
    void drawParameterKnob(const DrumCloudKnobSpec& spec) const;
    void drawPanel(float x, float y, float width, float height, const char* title) const;
    void  drawStrokeChar(char c, float x, float y, float s) const;
    void  drawStrokeText(const char* txt, float x, float y, float s) const;
    void  drawPixelGlyph(char c, float x, float y, float scale) const;
    void  drawPixelText(const char* txt, float x, float y, float scale) const;
    void  drawModernKnob(float cx, float cy, float r, float value, const char* label, bool isHovered) const;
};

float DrumCloudUI::getParamMin(uint32_t param) const
{
    switch (param) {
        case paramVolume: return 0.0f;
        case paramDensity: return 0.0f;
        case paramRelease: return 5.0f;
        case paramStartPosition: return 0.0f;
        case paramPositionSpread: return 0.0f;
        case paramScanSpeed: return 0.0f;
        case paramFilter: return 0.0f;
        case paramResonance: return 0.0f;
        case paramReverbSize: return 0.0f;
        case paramReverbMix: return 0.0f;
        case paramVelocityToDensity: return 0.0f;
        case paramVelocityToGrainSize: return 0.0f;
        case paramPitchRate: return 0.5f;
        case paramScanJumpRate: return 0.1f;
        case paramScanJumpAmount: return 0.0f;
        case paramScanJumpSmoothMs: return 0.0f;
        case paramSyncRate: return 0.0f;
        case paramRootNote: return 0.0f;
        case paramSampleFineTune: return -100.0f;
        case paramGrainAttack: return 0.0f;
        case paramGrainRelease: return 0.0f;
        case paramSampleStart: return 0.0f;
        case paramSampleEnd: return 0.0f;
        case paramAutoRoot: return 0.0f;
        case paramDelayMode: return 0.0f;
        case paramDelayTimeLeft: return 1.0f;
        case paramDelayTimeRight: return 1.0f;
        case paramDelayFeedback: return 0.0f;
        case paramDelayMix: return 0.0f;
        case paramDelayDamping: return 0.0f;
        case paramTimeStretch: return 0.25f;
        default: return 0.0f;
    }
}

float DrumCloudUI::getParamMax(uint32_t param) const
{
    switch (param) {
        case paramVolume: return 1.0f;
        case paramDensity: return 1.0f;
        case paramRelease: return 5000.0f;
        case paramStartPosition: return 1.0f;
        case paramPositionSpread: return 1.0f;
        case paramScanSpeed: return 2.0f;
        case paramFilter: return 1.0f;
        case paramResonance: return 1.0f;
        case paramReverbSize: return 1.0f;
        case paramReverbMix: return 1.0f;
        case paramVelocityToDensity: return 1.0f;
        case paramVelocityToGrainSize: return 1.0f;
        case paramPitchRate: return 2.0f;
        case paramScanJumpRate: return 40.0f;
        case paramScanJumpAmount: return 1.0f;
        case paramScanJumpSmoothMs: return 500.0f;
        case paramSyncRate: return 1.0f;
        case paramRootNote: return 127.0f;
        case paramSampleFineTune: return 100.0f;
        case paramGrainAttack: return 500.0f;
        case paramGrainRelease: return 1000.0f;
        case paramSampleStart: return 1.0f;
        case paramSampleEnd: return 1.0f;
        case paramAutoRoot: return 1.0f;
        case paramDelayMode: return 2.0f;
        case paramDelayTimeLeft: return 2000.0f;
        case paramDelayTimeRight: return 2000.0f;
        case paramDelayFeedback: return 0.90f;
        case paramDelayMix: return 1.0f;
        case paramDelayDamping: return 1.0f;
        case paramTimeStretch: return 4.0f;
        default: return 1.0f;
    }
}

float DrumCloudUI::getParamDef(uint32_t param) const
{
    switch (param) {
        case paramVolume: return 1.0f;
        case paramDensity: return 0.72f;
        case paramRelease: return 452.5f;
        case paramStartPosition: return 0.0f;
        case paramPositionSpread: return 0.0f;
        case paramScanSpeed: return 0.5f;
        case paramFilter: return 0.5f;
        case paramResonance: return 0.0f;
        case paramReverbSize: return 0.8f;
        case paramReverbMix: return 0.0f;
        case paramVelocityToDensity: return 0.42f;
        case paramVelocityToGrainSize: return 0.58f;
        case paramPitchRate: return 1.0f;
        case paramScanJumpRate: return 27.0f;
        case paramScanJumpAmount: return 0.69f;
        case paramScanJumpSmoothMs: return 272.0f;
        case paramSyncRate: return 1.0f;
        case paramRootNote: return 60.0f;
        case paramSampleFineTune: return 0.0f;
        case paramGrainAttack: return 10.0f;
        case paramGrainRelease: return 80.0f;
        case paramSampleStart: return 0.0f;
        case paramSampleEnd: return 1.0f;
        case paramAutoRoot: return 1.0f;
        case paramDelayMode: return 0.0f;
        case paramDelayTimeLeft: return 375.0f;
        case paramDelayTimeRight: return 500.0f;
        case paramDelayFeedback: return 0.35f;
        case paramDelayMix: return 0.25f;
        case paramDelayDamping: return 0.35f;
        case paramTimeStretch: return 1.0f;
        default: return 0.0f;
    }
}

float DrumCloudUI::getParamUiValue(uint32_t param) const
{
    switch (param) {
        case paramVolume: return fVolumeUi;
        case paramDensity: return fDensityUi;
        case paramRelease: return fReleaseMsUi;
        case paramStartPosition: return fStartPosUi;
        case paramPositionSpread: return fSpreadUi;
        case paramScanSpeed: return fScanSpeedUi;
        case paramFilter: return fFilterUi;
        case paramResonance: return fResoUi;
        case paramReverbSize: return fReverbSizeUi;
        case paramReverbMix: return fReverbMixUi;
        case paramVelocityToDensity: return fVelToDensityUi;
        case paramVelocityToGrainSize: return fVelToGrainUi;
        case paramPitchRate: return fPitchRateUi;
        case paramScanJumpRate: return fJumpRateUi;
        case paramScanJumpAmount: return fJumpAmountUi;
        case paramScanJumpSmoothMs: return fJumpSmoothMsUi;
        case paramSyncRate: return fSyncRateUi;
        case paramRootNote: return fRootNoteUi;
        case paramSampleFineTune: return fSampleFineTuneUi;
        case paramGrainAttack: return fGrainAttackMsUi;
        case paramGrainRelease: return fGrainReleaseMsUi;
        case paramSampleStart: return fSampleStartUi;
        case paramSampleEnd: return fSampleEndUi;
        case paramAutoRoot: return fAutoRootUi;
        case paramDelayMode: return fDelayModeUi;
        case paramDelayTimeLeft: return fDelayTimeLeftUi;
        case paramDelayTimeRight: return fDelayTimeRightUi;
        case paramDelayFeedback: return fDelayFeedbackUi;
        case paramDelayMix: return fDelayMixUi;
        case paramDelayDamping: return fDelayDampingUi;
        case paramTimeStretch: return fTimeStretchUi;
        default: return 0.0f;
    }
}

void DrumCloudUI::setParamUiValue(uint32_t param, float value)
{
    switch (param) {
        case paramVolume: fVolumeUi = value; break;
        case paramDensity: fDensityUi = value; break;
        case paramRelease: fReleaseMsUi = value; break;
        case paramStartPosition: fStartPosUi = value; break;
        case paramPositionSpread: fSpreadUi = value; break;
        case paramScanSpeed: fScanSpeedUi = value; break;
        case paramFilter: fFilterUi = value; break;
        case paramResonance: fResoUi = value; break;
        case paramReverbSize: fReverbSizeUi = value; break;
        case paramReverbMix: fReverbMixUi = value; break;
        case paramVelocityToDensity: fVelToDensityUi = value; break;
        case paramVelocityToGrainSize: fVelToGrainUi = value; break;
        case paramPitchRate: fPitchRateUi = value; break;
        case paramScanJumpRate: fJumpRateUi = value; break;
        case paramScanJumpAmount: fJumpAmountUi = value; break;
        case paramScanJumpSmoothMs: fJumpSmoothMsUi = value; break;
        case paramSyncRate: fSyncRateUi = value; break;
        case paramRootNote: fRootNoteUi = std::round(value); break;
        case paramSampleFineTune: fSampleFineTuneUi = value; break;
        case paramGrainAttack: fGrainAttackMsUi = value; break;
        case paramGrainRelease: fGrainReleaseMsUi = value; break;
        case paramSampleStart: fSampleStartUi = value; break;
        case paramSampleEnd: fSampleEndUi = value; break;
        case paramAutoRoot: fAutoRootUi = value; break;
        case paramDelayMode: fDelayModeUi = std::round(value); break;
        case paramDelayTimeLeft: fDelayTimeLeftUi = value; break;
        case paramDelayTimeRight: fDelayTimeRightUi = value; break;
        case paramDelayFeedback: fDelayFeedbackUi = value; break;
        case paramDelayMix: fDelayMixUi = value; break;
        case paramDelayDamping: fDelayDampingUi = value; break;
        case paramTimeStretch: fTimeStretchUi = value; break;
        default: break;
    }
}

bool DrumCloudUI::hitKnob(float mx, float my, float cx, float cy, float r) const
{
    const float dx = mx - cx;
    const float dy = my - cy;
    return (dx*dx + dy*dy) <= (r*r);
}

uint32_t DrumCloudUI::knobAt(float mx, float my) const
{
    for (const DrumCloudKnobSpec& spec : kDrumCloudKnobs)
        if (hitKnob(mx, my, spec.x, spec.y, spec.radius + 3.0f))
            return spec.param;
    return 0xffffffffu;
}

float DrumCloudUI::normaliseParamValue(uint32_t param, float value) const
{
    if (param == paramTimeStretch)
        return std::clamp((std::log2(std::max(0.25f, value)) + 2.0f) * 0.25f, 0.0f, 1.0f);

    const float vmin = getParamMin(param);
    const float vmax = getParamMax(param);
    return vmax > vmin ? std::clamp((value - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
}

void DrumCloudUI::formatParamValue(uint32_t param, float value, char* text, std::size_t size) const
{
    if (text == nullptr || size == 0) return;

    switch (param)
    {
    case paramRelease:
    case paramGrainAttack:
    case paramGrainRelease:
    case paramScanJumpSmoothMs:
    case paramDelayTimeLeft:
    case paramDelayTimeRight:
        std::snprintf(text, size, "%.0f MS", value);
        return;
    case paramScanJumpRate:
        std::snprintf(text, size, "%.1f HZ", value);
        return;
    case paramPitchRate:
    case paramTimeStretch:
        std::snprintf(text, size, "%.2f X", value);
        return;
    case paramRootNote:
        std::snprintf(text, size, "MIDI %.0f", value);
        return;
    case paramSampleFineTune:
        std::snprintf(text, size, "%.0f CT", value);
        return;
    case paramSyncRate:
        std::snprintf(text, size, "%s", value < 0.25f ? "0.5 X" : (value < 0.75f ? "1 X" : "2 X"));
        return;
    case paramDelayMode:
        std::snprintf(text, size, "%s", value < 0.5f ? "OFF" : (value < 1.5f ? "STEREO" : "PING PONG"));
        return;
    default:
        std::snprintf(text, size, "%.0f %%", value * 100.0f);
        return;
    }
}

void DrumCloudUI::drawParameterKnob(const DrumCloudKnobSpec& spec) const
{
    const float value = getParamUiValue(spec.param);
    drawModernKnob(spec.x, spec.y, spec.radius,
                   normaliseParamValue(spec.param, value), spec.label,
                   fHoverKnobParam == spec.param);

    char valueText[24]{};
    formatParamValue(spec.param, value, valueText, sizeof(valueText));
    glColor4f(0.74f, 0.76f, 0.80f, 0.96f);
    drawPixelText(valueText,
                  spec.x - float(std::strlen(valueText)) * 3.0f,
                  spec.y + spec.radius + 23.0f, 1.0f);
}

void DrumCloudUI::drawPanel(float x, float y, float width, float height, const char* title) const
{
    glColor4f(0.045f, 0.048f, 0.058f, 0.98f);
    glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + width, y);
        glVertex2f(x + width, y + height); glVertex2f(x, y + height);
    glEnd();

    glLineWidth(1.0f);
    glColor4f(0.22f, 0.23f, 0.27f, 1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x, y); glVertex2f(x + width, y);
        glVertex2f(x + width, y + height); glVertex2f(x, y + height);
    glEnd();

    glColor4f(0.94f, 0.68f, 0.19f, 0.98f);
    drawPixelText(title, x + 12.0f, y + 10.0f, 1.15f);

    glColor4f(0.18f, 0.19f, 0.22f, 1.0f);
    glBegin(GL_LINES);
        glVertex2f(x + 12.0f, y + 27.0f);
        glVertex2f(x + width - 12.0f, y + 27.0f);
    glEnd();
}

static const uint8_t* getPixelGlyphRows(char c)
{
    static const uint8_t SPACE[7] = {0,0,0,0,0,0,0};
    static const uint8_t DOT[7]   = {0,0,0,0,0,0x0C,0x0C};
    static const uint8_t DASH[7]  = {0,0,0,0x1E,0,0,0};
    static const uint8_t COLON[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0};
    static const uint8_t SLASH[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    static const uint8_t PERCENT[7] = {0x19,0x1A,0x04,0x04,0x08,0x0B,0x13};
    static const uint8_t A[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t B[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    static const uint8_t C[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
    static const uint8_t D[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    static const uint8_t E[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    static const uint8_t F[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    static const uint8_t G[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E};
    static const uint8_t H[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t I[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
    static const uint8_t J[7] = {0x07,0x02,0x02,0x02,0x12,0x12,0x0C};
    static const uint8_t K[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    static const uint8_t L[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    static const uint8_t M[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    static const uint8_t N[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    static const uint8_t O[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t P[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    static const uint8_t Q[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    static const uint8_t R[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    static const uint8_t S[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    static const uint8_t T[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    static const uint8_t U[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t V[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
    static const uint8_t W[7] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
    static const uint8_t X[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
    static const uint8_t Y[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
    static const uint8_t Z[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
    static const uint8_t N0[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    static const uint8_t N1[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    static const uint8_t N2[7]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    static const uint8_t N3[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    static const uint8_t N4[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    static const uint8_t N5[7]={0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    static const uint8_t N6[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    static const uint8_t N7[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    static const uint8_t N8[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    static const uint8_t N9[7]={0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
    switch (std::toupper(static_cast<unsigned char>(c)))
    {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
    case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
    case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
    case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
    case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
    case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
    case 'Y': return Y; case 'Z': return Z;
    case '0': return N0; case '1': return N1; case '2': return N2; case '3': return N3;
    case '4': return N4; case '5': return N5; case '6': return N6; case '7': return N7;
    case '8': return N8; case '9': return N9;
    case '.': return DOT; case '-': return DASH; case ':': return COLON;
    case '/': return SLASH; case '%': return PERCENT; case ' ': return SPACE;
    default: return SPACE;
    }
}

void DrumCloudUI::drawPixelGlyph(char c, float x, float y, float scale) const
{
    const uint8_t* rows = getPixelGlyphRows(c);
    const float px = std::max(1.0f, scale);
    const float py = std::max(1.0f, scale);
    glBegin(GL_QUADS);
    for (int r = 0; r < 7; ++r)
    {
        for (int col = 0; col < 5; ++col)
        {
            if (rows[r] & (1 << (4 - col)))
            {
                const float x0 = x + col * px;
                const float y0 = y + r * py;
                glVertex2f(x0, y0);
                glVertex2f(x0 + px, y0);
                glVertex2f(x0 + px, y0 + py);
                glVertex2f(x0, y0 + py);
            }
        }
    }
    glEnd();
}

void DrumCloudUI::drawPixelText(const char* txt, float x, float y, float scale) const
{
    float pen = x;
    for (const char* p = txt; *p; ++p)
    {
        drawPixelGlyph(*p, pen, y, scale);
        pen += 6.0f * scale;
    }
}

void DrumCloudUI::drawModernKnob(float cx, float cy, float r, float value, const char* label, bool isHovered) const
{
    const float angleStart = -135.0f * (float)M_PI / 180.0f;
    const float angleEnd   =  135.0f * (float)M_PI / 180.0f;
    const float angleVal   = angleStart + (angleEnd - angleStart) * value;

    if (isHovered) glColor4f(0.18f, 0.19f, 0.22f, 1.0f);
    else glColor4f(0.12f, 0.13f, 0.16f, 1.0f);

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for(int i = 0; i <= 32; ++i) {
        float a = angleStart + (angleEnd - angleStart) * (i / 32.0f);
        glVertex2f(cx + std::cos(a) * r, cy + std::sin(a) * r);
    }
    glEnd();

    if (isHovered) glColor4f(1.00f, 0.84f, 0.34f, 1.0f);
    else glColor4f(0.82f, 0.58f, 0.16f, 1.0f); 

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for(int i = 0; i <= 32; ++i) {
        float t = i / 32.0f;
        float a = angleStart + (angleVal - angleStart) * t;
        glVertex2f(cx + std::cos(a) * r, cy + std::sin(a) * r);
    }
    glEnd();

    glColor4f(0.06f, 0.06f, 0.07f, 1.0f); 
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for(int i = 0; i <= 32; ++i) {
        float a = i * ((float)M_PI * 2.0f) / 32.0f;
        glVertex2f(cx + std::cos(a) * (r - 4.5f), cy + std::sin(a) * (r - 4.5f));
    }
    glEnd();

    glColor4f(0.9f, 0.9f, 0.95f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(cx + std::cos(angleVal) * (r - 6.0f), cy + std::sin(angleVal) * (r - 6.0f));
    glVertex2f(cx + std::cos(angleVal) * r, cy + std::sin(angleVal) * r);
    glEnd();

    if (isHovered) glColor4f(1.0f, 0.92f, 0.62f, 1.0f);
    else glColor4f(0.86f, 0.72f, 0.42f, 0.98f);
    drawPixelText(label, cx - (float)std::strlen(label) * 3.3f, cy + r + 10.0f, 1.20f);
}

void DrumCloudUI::previewLoop()
{
    for (;;)
    {
        std::string path;
        uint64_t generation = 0;
        bool chosen = false;
        {
            std::unique_lock<std::mutex> lock(fPreviewMutex);
            fPreviewCV.wait(lock, [this]{ return fPreviewStop || fPreviewPending; });
            if (fPreviewStop) return;
            path.swap(fPreviewPath);
            generation = fPreviewGeneration;
            chosen = fPreviewChosen;
            fPreviewPending = false;
        }

        std::unique_ptr<PreviewResult> result(new PreviewResult);
        result->path = std::move(path);
        result->generation = generation;
        result->chosen = chosen;
        try {
            result->success = loadAudioFilePreview(result->path.c_str(),
                result->preview, &result->error);
        } catch (...) {
            result->error = "not enough memory to load sample";
            result->success = false;
        }
        {
            std::lock_guard<std::mutex> lock(fPreviewMutex);
            if (fPreviewStop || generation != fPreviewGeneration) continue;
            fPreviewResult = std::move(result);
        }
    }
}

void DrumCloudUI::onDisplay()
{
    const float W = (float)getWidth();
    const float waveTop = 52.0f;
    const float waveBottom = 176.0f;
    const float mid = 0.5f * (waveTop + waveBottom);

    glDisable(GL_TEXTURE_2D);
    glClearColor(0.018f, 0.019f, 0.024f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Fuimadane identity and instrument hierarchy.
    glColor4f(0.95f, 0.70f, 0.22f, 1.0f);
    drawPixelText("DRUMCLOUD", 18.0f, 15.0f, 2.0f);
    glColor4f(0.62f, 0.64f, 0.69f, 0.96f);
    drawPixelText("FUIMADANE GRANULAR INSTRUMENT", 150.0f, 20.0f, 1.05f);
    glColor4f(0.82f, 0.60f, 0.19f, 0.94f);
    drawPixelText("V1.9 BETA", W - 78.0f, 20.0f, 1.0f);

    // Waveform frame.
    glColor4f(0.035f, 0.038f, 0.047f, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(18.0f, waveTop); glVertex2f(W - 18.0f, waveTop);
        glVertex2f(W - 18.0f, waveBottom); glVertex2f(18.0f, waveBottom);
    glEnd();
    glLineWidth(1.0f);
    glColor4f(0.25f, 0.26f, 0.30f, 1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(18.0f, waveTop); glVertex2f(W - 18.0f, waveTop);
        glVertex2f(W - 18.0f, waveBottom); glVertex2f(18.0f, waveBottom);
    glEnd();

    drawPanel(18.0f, 192.0f, 384.0f, 188.0f, "VOICE / GRAIN");
    drawPanel(418.0f, 192.0f, 384.0f, 188.0f, "POSITION / MOTION");
    drawPanel(18.0f, 396.0f, 384.0f, 124.0f, "TONE / PITCH");
    drawPanel(418.0f, 396.0f, 384.0f, 124.0f, "REVERB");
    drawPanel(18.0f, 536.0f, 784.0f, 146.0f, "FILTERED DELAY");

    glLineWidth(1.0f);
    glColor4f(0.25f, 0.25f, 0.28f, 1.0f);
    glBegin(GL_LINES);
        glVertex2f(18.0f, mid);
        glVertex2f(W - 18.0f, mid);
    glEnd();

    if (fWaveValid)
    {
        const float x0 = 18.0f;
        const float x1 = W - 18.0f;
        const float y0 = waveTop;
        const float y1 = waveBottom;
        const float scanPos = std::clamp(fScanPosUI, 0.0f, 1.0f);
        const float scanX = x0 + scanPos * (x1 - x0);
        const float ampY = 0.5f * (y1 - y0);

        glLineWidth(1.0f);
        glColor4f(0.86f, 0.65f, 0.26f, 1.0f);
        glBegin(GL_LINES);
        for (int i = 0; i < kWavePreviewSize; ++i)
        {
            const float t = (kWavePreviewSize > 1) ? (float)i / (float)(kWavePreviewSize - 1) : 0.0f;
            const float x = x0 + t * (x1 - x0);
            glVertex2f(x, mid + std::clamp(fWaveMin[i], -1.0f, 1.0f) * ampY);
            glVertex2f(x, mid + std::clamp(fWaveMax[i], -1.0f, 1.0f) * ampY);
        }
        glEnd();

        const float regionStartX = x0 + std::clamp(fSampleStartUi, 0.0f, 1.0f) * (x1 - x0);
        const float regionEndX = x0 + std::clamp(fSampleEndUi, 0.0f, 1.0f) * (x1 - x0);

        // Dim audio outside the playable region and draw draggable gold handles.
        glColor4f(0.015f, 0.012f, 0.010f, 0.72f);
        glBegin(GL_QUADS);
            glVertex2f(x0, y0); glVertex2f(regionStartX, y0);
            glVertex2f(regionStartX, y1); glVertex2f(x0, y1);
            glVertex2f(regionEndX, y0); glVertex2f(x1, y0);
            glVertex2f(x1, y1); glVertex2f(regionEndX, y1);
        glEnd();

        glLineWidth(2.0f);
        glColor4f(0.96f, 0.68f, 0.18f, 0.98f);
        glBegin(GL_LINES);
            glVertex2f(regionStartX, y0); glVertex2f(regionStartX, y1);
            glVertex2f(regionEndX, y0); glVertex2f(regionEndX, y1);
        glEnd();

        const float startX = regionStartX + fStartPosUi * (regionEndX - regionStartX);
        const float halfW = 0.5f * fSpreadUi * (regionEndX - regionStartX);
        const float sx0 = std::max(regionStartX, startX - halfW);
        const float sx1 = std::min(regionEndX, startX + halfW);

        if (fSpreadUi > 0.0001f)
        {
            glBegin(GL_QUADS);
                glColor4f(0.32f, 0.34f, 0.48f, 0.08f);
                glVertex2f(sx0, y0);
                glVertex2f(sx1, y0);
                glColor4f(0.36f, 0.40f, 0.56f, 0.18f);
                glVertex2f(sx1, y1);
                glVertex2f(sx0, y1);
            glEnd();

            glLineWidth(1.0f);
            glColor4f(0.55f, 0.62f, 0.88f, 0.35f);
            glBegin(GL_LINES);
                glVertex2f(sx0, y0); glVertex2f(sx0, y1);
                glVertex2f(sx1, y0); glVertex2f(sx1, y1);
            glEnd();
        }

        const bool showStartMarker = (fScanModeUi <= 0 || fDragStartPos);
        if (showStartMarker)
        {
            glLineWidth(6.0f);
            glColor4f(1.0f, 0.72f, 0.18f, 0.12f);
            glBegin(GL_LINES);
                glVertex2f(startX, y0);
                glVertex2f(startX, y1);
            glEnd();

            glLineWidth(2.0f);
            glColor4f(1.0f, 0.76f, 0.22f, 0.92f);
            glBegin(GL_LINES);
                glVertex2f(startX, y0);
                glVertex2f(startX, y1);
            glEnd();

            glColor4f(1.0f, 0.80f, 0.28f, 0.90f);
            glBegin(GL_TRIANGLES);
                glVertex2f(startX, y0 - 1.0f);
                glVertex2f(startX - 4.0f, y0 - 8.0f);
                glVertex2f(startX + 4.0f, y0 - 8.0f);
            glEnd();
        }

        // Individual active grains: warm coral, deliberately distinct from cyan scan.
        const uint32_t visibleGrains = std::min<uint32_t>(fGrainCountUI, kUiGrainMarkerCount);
        glLineWidth(1.0f);
        glColor4f(1.0f, 0.32f, 0.12f, 0.78f);
        glBegin(GL_LINES);
        for (uint32_t i = 0; i < visibleGrains; ++i)
        {
            const float gx = x0 + std::clamp(fGrainPosUI[i], 0.0f, 1.0f) * (x1 - x0);
            glVertex2f(gx, y0 + 5.0f);
            glVertex2f(gx, y1 - 5.0f);
        }
        glEnd();

        glLineWidth(6.0f);
        glColor4f(1.0f, 1.0f, 1.0f, 0.18f);
        glBegin(GL_LINES);
            glVertex2f(scanX, y0);
            glVertex2f(scanX, y1);
        glEnd();

        glLineWidth(2.0f);
        glColor4f(0.96f, 0.97f, 1.0f, 0.98f);
        glBegin(GL_LINES);
            glVertex2f(scanX, y0);
            glVertex2f(scanX, y1);
        glEnd();
    }

    if (!fLoadStatus.empty())
    {
        if (fLoadError) glColor4f(1.0f, 0.44f, 0.34f, 1.0f);
        else glColor4f(0.88f, 0.91f, 0.97f, 0.98f);
        drawPixelText(fLoadStatus.c_str(), 26.0f, 158.0f, 1.05f);
    }

    for (const DrumCloudKnobSpec& spec : kDrumCloudKnobs)
        drawParameterKnob(spec);

    // Automatic pitch analysis controls and result.
    {
        const float bx0 = W - 258.0f;
        const float by0 = 60.0f;
        const float bw = 120.0f;
        const float bh = 22.0f;
        glColor4f(0.10f, 0.11f, 0.15f, 0.94f);
        glBegin(GL_QUADS);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(fAutoRootUi >= 0.5f ? 0.86f : 0.30f,
                  fAutoRootUi >= 0.5f ? 0.65f : 0.34f,
                  fAutoRootUi >= 0.5f ? 0.24f : 0.44f, 0.95f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.88f, 0.91f, 0.97f, 0.98f);
        drawPixelText(fAutoRootUi >= 0.5f ? "AUTO ROOT ON" : "AUTO ROOT OFF",
                      bx0 + 9.0f, by0 + 6.0f, 1.15f);
    }

    {
        char pitchBuf[28];
        if (fDetectedRootUi >= 0)
            std::snprintf(pitchBuf, sizeof(pitchBuf), "R%d F%.0f C%.0f",
                          fDetectedRootUi, fDetectedFineUi,
                          fDetectedConfidenceUi * 100.0f);
        else
            std::snprintf(pitchBuf, sizeof(pitchBuf), "ROOT LOW CONF");

        const float bx0 = W - 258.0f;
        const float by0 = 88.0f;
        const float bw = 120.0f;
        const float bh = 22.0f;
        glColor4f(0.10f, 0.11f, 0.15f, 0.94f);
        glBegin(GL_QUADS);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.30f, 0.34f, 0.44f, 0.9f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(fDetectedRootUi >= 0 ? 0.88f : 1.0f,
                  fDetectedRootUi >= 0 ? 0.91f : 0.55f,
                  fDetectedRootUi >= 0 ? 0.97f : 0.30f, 0.98f);
        drawPixelText(pitchBuf, bx0 + 7.0f, by0 + 6.0f, 1.05f);
    }

    // Data Boxes
    {
        char scanBuf[24];
        std::snprintf(scanBuf, sizeof(scanBuf), "SCAN %.2f", fScanPosUI);
        const float bx0 = W - 118.0f;
        const float by0 = 88.0f;
        const float bw = 90.0f;
        const float bh = 22.0f;
        glColor4f(0.10f, 0.11f, 0.15f, 0.92f);
        glBegin(GL_QUADS);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.30f, 0.34f, 0.44f, 0.9f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.88f, 0.91f, 0.97f, 0.98f);
        drawPixelText(scanBuf, bx0 + 8.0f, by0 + 6.0f, 1.35f);
    }

    {
        const char* modeName = "HOLD";
        switch (fScanModeUi)
        {
        case 1: modeName = "SCAN"; break;
        case 2: modeName = "JUMP"; break;
        case 3: modeName = "SYNC"; break;
        default: break;
        }
        char modeBuf[20];
        std::snprintf(modeBuf, sizeof(modeBuf), "%s %d", modeName, fScanModeUi);
        const float bx0 = W - 118.0f;
        const float by0 = 60.0f;
        const float bw = 90.0f;
        const float bh = 22.0f;
        glColor4f(0.10f, 0.11f, 0.15f, 0.92f);
        glBegin(GL_QUADS);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.30f, 0.34f, 0.44f, 0.9f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.88f, 0.91f, 0.97f, 0.98f);
        drawPixelText(modeBuf, bx0 + 9.0f, by0 + 6.0f, 1.35f);
    }

    // Playback boundary behaviour for SCAN mode.
    {
        const char* playbackName = "PLAY LOOP";
        switch (fPlaybackModeUi)
        {
        case 1: playbackName = "PLAY ONE SHOT"; break;
        case 2: playbackName = "PLAY PING PONG"; break;
        default: break;
        }

        const float bx0 = W - 258.0f;
        const float by0 = 116.0f;
        const float bw = 230.0f;
        const float bh = 22.0f;
        glColor4f(0.10f, 0.11f, 0.15f, 0.94f);
        glBegin(GL_QUADS);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.86f, 0.65f, 0.24f, 0.95f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(bx0, by0); glVertex2f(bx0 + bw, by0);
            glVertex2f(bx0 + bw, by0 + bh); glVertex2f(bx0, by0 + bh);
        glEnd();
        glColor4f(0.88f, 0.91f, 0.97f, 0.98f);
        drawPixelText(playbackName, bx0 + 9.0f, by0 + 6.0f, 1.15f);
    }
}

bool DrumCloudUI::onMotion(const MotionEvent& ev)
{
    const float mx = (float)ev.pos.getX();
    const float my = (float)ev.pos.getY();

    const float wx0 = 18.0f;
    const float wx1 = (float)getWidth() - 18.0f;

    if (fDragSampleStart)
    {
        float norm = (wx1 > wx0) ? (mx - wx0) / (wx1 - wx0) : 0.0f;
        norm = std::clamp(norm, 0.0f, fSampleEndUi - 0.001f);
        fSampleStartUi = norm;
        setParameterValue(paramSampleStart, norm);
        repaint();
        return true;
    }

    if (fDragSampleEnd)
    {
        float norm = (wx1 > wx0) ? (mx - wx0) / (wx1 - wx0) : 1.0f;
        norm = std::clamp(norm, fSampleStartUi + 0.001f, 1.0f);
        fSampleEndUi = norm;
        setParameterValue(paramSampleEnd, norm);
        repaint();
        return true;
    }

    if (fDragStartPos)
    {
        const float regionX0 = wx0 + fSampleStartUi * (wx1 - wx0);
        const float regionX1 = wx0 + fSampleEndUi * (wx1 - wx0);
        float norm = (regionX1 > regionX0) ? (mx - regionX0) / (regionX1 - regionX0) : 0.0f;
        norm = std::clamp(norm, 0.0f, 1.0f);
        if (norm < 0.005f) norm = 0.0f;
        fStartPosUi = norm;
        setParameterValue(paramStartPosition, norm);
        repaint();
        return true;
    }

    if (fDragKnob)
    {
        const float minV = getParamMin(fDragKnobParam);
        const float maxV = getParamMax(fDragKnobParam);
        const float range = maxV - minV;
        float newValue;
        if (fDragKnobParam == paramTimeStretch)
        {
            const float startOctaves = std::log2(std::clamp(fKnobDragStartValue, 0.25f, 4.0f));
            newValue = std::exp2(std::clamp(startOctaves + ((mx - fKnobDragStartX) / 260.0f) * 4.0f,
                                            -2.0f, 2.0f));
        }
        else
        {
            newValue = fKnobDragStartValue + ((mx - fKnobDragStartX) / 260.0f) * range;
        }
        newValue = std::clamp(newValue, minV, maxV);
        setParamUiValue(fDragKnobParam, newValue);
        setParameterValue(fDragKnobParam, newValue);
        repaint();
        return true;
    }

    const uint32_t hoverNow = (!fDragKnob && !fDragStartPos && !fDragSampleStart && !fDragSampleEnd)
        ? knobAt(mx, my)
        : 0xffffffffu;

    if (fHoverKnobParam != hoverNow)
    {
        fHoverKnobParam = hoverNow;
        repaint();
    }

    return false;
}

void DrumCloudUI::uiIdle()
{
    std::unique_ptr<PreviewResult> result;
    {
        std::lock_guard<std::mutex> lock(fPreviewMutex);
        result.swap(fPreviewResult);
    }
    if (result && result->generation == fPreviewGeneration)
    {
        if (result->success)
        {
            fSamplePath = result->path;
            std::copy_n(result->preview.min, kWavePreviewSize, fWaveMin);
            std::copy_n(result->preview.max, kWavePreviewSize, fWaveMax);
            fWaveValid = true;
            fLoadError = false;
            fLoadStatus = result->chosen ? "FILE ACCEPTED" : "SAMPLE PREVIEW READY";
            if (result->chosen) setState("samplePath", fSamplePath.c_str());
        }
        else
        {
            // Leave the previous sample and waveform intact on failure.
            fLoadError = true;
            if (result->error.find("size limit") != std::string::npos)
                fLoadStatus = "FILE TOO LONG - LIMIT 16M FRAMES";
            else if (result->error == "unsupported type")
                fLoadStatus = "FORMAT NOT SUPPORTED";
            else if (result->error == "not enough memory to load sample")
                fLoadStatus = "NOT ENOUGH MEMORY";
            else
                fLoadStatus = "FILE MISSING OR INVALID";
        }
        repaint();
    }
    const float scan = std::clamp(gDrumCloudUiScanPos.load(std::memory_order_relaxed), 0.0f, 1.0f);

    const uint32_t detectedGeneration =
        gDrumCloudDetectedPitchGeneration.load(std::memory_order_acquire);
    if (detectedGeneration != fDetectedPitchGenerationUi)
    {
        fDetectedPitchGenerationUi = detectedGeneration;
        fDetectedRootUi = gDrumCloudDetectedRoot.load(std::memory_order_relaxed);
        fDetectedFineUi = gDrumCloudDetectedFine.load(std::memory_order_relaxed);
        fDetectedConfidenceUi = gDrumCloudDetectedConfidence.load(std::memory_order_relaxed);

        if (fAutoRootUi >= 0.5f && fDetectedRootUi >= 0 && fDetectedConfidenceUi >= 0.70f)
        {
            fRootNoteUi = float(fDetectedRootUi);
            fSampleFineTuneUi = fDetectedFineUi;
            setParameterValue(paramRootNote, fRootNoteUi);
            setParameterValue(paramSampleFineTune, fSampleFineTuneUi);
        }
        repaint();
    }

    bool grainChanged = false;
    const uint32_t grainCount = std::min<uint32_t>(
        gDrumCloudUiGrainCount.load(std::memory_order_acquire), kUiGrainMarkerCount);
    if (grainCount != fGrainCountUI)
    {
        fGrainCountUI = grainCount;
        grainChanged = true;
    }
    for (uint32_t i = 0; i < grainCount; ++i)
    {
        const float pos = std::clamp(gDrumCloudUiGrainPos[i].load(std::memory_order_relaxed), 0.0f, 1.0f);
        if (std::fabs(pos - fGrainPosUI[i]) > 0.0005f)
        {
            fGrainPosUI[i] = pos;
            grainChanged = true;
        }
    }

    if (std::fabs(scan - fScanPosUI) > 0.0005f)
    {
        fScanPosUI = scan;
        repaint();
    }
    else if (grainChanged)
    {
        repaint();
    }
}

bool DrumCloudUI::onMouse(const MouseEvent& ev)
{
    const float mx = (float)ev.pos.getX();
    const float my = (float)ev.pos.getY();
    const float wx0 = 18.0f;
    const float wx1 = (float)getWidth() - 18.0f;
    const float wy0 = 52.0f;
    const float wy1 = 176.0f;
    const bool hitWave = (mx >= wx0 && mx <= wx1 && my >= wy0 && my <= wy1);
    const bool hitStartPosZone = hitWave && (my >= (wy1 - 16.0f) && my <= wy1);
    const float regionStartX = wx0 + fSampleStartUi * (wx1 - wx0);
    const float regionEndX = wx0 + fSampleEndUi * (wx1 - wx0);
    const bool hitSampleStart = hitWave && std::fabs(mx - regionStartX) <= 7.0f;
    const bool hitSampleEnd = hitWave && std::fabs(mx - regionEndX) <= 7.0f;

    if (ev.button == 1 && ev.press)
    {
        const float autoBx0 = (float)getWidth() - 258.0f;
        const float autoBy0 = 60.0f;
        const float autoBw = 120.0f;
        const float autoBh = 22.0f;
        if (mx >= autoBx0 && mx <= autoBx0 + autoBw &&
            my >= autoBy0 && my <= autoBy0 + autoBh)
        {
            fAutoRootUi = fAutoRootUi >= 0.5f ? 0.0f : 1.0f;
            editParameter(paramAutoRoot, true);
            setParameterValue(paramAutoRoot, fAutoRootUi);
            editParameter(paramAutoRoot, false);

            if (fAutoRootUi >= 0.5f && fDetectedRootUi >= 0 && fDetectedConfidenceUi >= 0.70f)
            {
                setParameterValue(paramRootNote, float(fDetectedRootUi));
                setParameterValue(paramSampleFineTune, fDetectedFineUi);
                fRootNoteUi = float(fDetectedRootUi);
                fSampleFineTuneUi = fDetectedFineUi;
            }
            repaint();
            return true;
        }

        const float playbackBx0 = (float)getWidth() - 258.0f;
        const float playbackBy0 = 116.0f;
        const float playbackBw = 230.0f;
        const float playbackBh = 22.0f;
        if (mx >= playbackBx0 && mx <= playbackBx0 + playbackBw &&
            my >= playbackBy0 && my <= playbackBy0 + playbackBh)
        {
            const int newMode = (fPlaybackModeUi + 1) % 3;
            fPlaybackModeUi = newMode;
            editParameter(paramPlaybackMode, true);
            setParameterValue(paramPlaybackMode, float(newMode));
            editParameter(paramPlaybackMode, false);

            // These boundary modes describe forward traversal, so selecting
            // one from the UI also makes SCAN the active movement mode.
            if (fScanModeUi != 1)
            {
                fScanModeUi = 1;
                editParameter(paramScanMode, true);
                setParameterValue(paramScanMode, 1.0f);
                editParameter(paramScanMode, false);
            }
            repaint();
            return true;
        }

        const float modeBx0 = (float)getWidth() - 118.0f;
        const float modeBy0 = 60.0f;
        const float modeBw  = 90.0f;
        const float modeBh  = 22.0f;

        if (mx >= modeBx0 && mx <= modeBx0 + modeBw &&
            my >= modeBy0 && my <= modeBy0 + modeBh)
        {
            const int newMode = (fScanModeUi + 1) % 4;
            fScanModeUi = newMode;
            editParameter(paramScanMode, true);
            setParameterValue(paramScanMode, (float)newMode);
            editParameter(paramScanMode, false);
            repaint();
            return true;
        }
    
        if (hitSampleStart || hitSampleEnd)
        {
            const bool chooseStart = hitSampleStart &&
                (!hitSampleEnd || std::fabs(mx - regionStartX) <= std::fabs(mx - regionEndX));
            fDragSampleStart = chooseStart;
            fDragSampleEnd = !chooseStart;
            editParameter(chooseStart ? paramSampleStart : paramSampleEnd, true);
            return true;
        }

        if (hitStartPosZone)
        {
            fDragStartPos = true;
            float norm = (regionEndX > regionStartX) ? (mx - regionStartX) / (regionEndX - regionStartX) : 0.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            if (norm < 0.005f) norm = 0.0f;
            fStartPosUi = norm;
            editParameter(paramStartPosition, true);
            setParameterValue(paramStartPosition, norm);
            repaint();
            return true;
        }

        const uint32_t knobParam = knobAt(mx, my);
        if (knobParam != 0xffffffffu)
        {
            const auto now = std::chrono::steady_clock::now();
            const bool isDoubleClick = (fLastClickParam == knobParam) &&
                (std::chrono::duration_cast<std::chrono::milliseconds>(now - fLastClickTime).count() < 300);
            fLastClickTime = now;
            fLastClickParam = knobParam;

            if (isDoubleClick)
            {
                const float defVal = getParamDef(knobParam);
                editParameter(knobParam, true);
                setParamUiValue(knobParam, defVal);
                setParameterValue(knobParam, defVal);
                editParameter(knobParam, false);
                repaint();
            }
            else
            {
                fDragKnob = true;
                fDragKnobParam = knobParam;
                fKnobDragStartX = mx;
                fKnobDragStartValue = getParamUiValue(knobParam);
                editParameter(knobParam, true);
            }
            return true;
        }

        if (hitWave)
        {
            fChoosingSample = true;
            requestStateFile("samplePath");
            return true;
        }
        return false;
    }

    if (ev.button == 1 && !ev.press)
    {
        if (fDragKnob)
        {
            editParameter(fDragKnobParam, false);
            fDragKnob = false;
            fDragKnobParam = 0xffffffffu;
            return true;
        }
        if (fDragStartPos)
        {
            editParameter(paramStartPosition, false);
            fDragStartPos = false;
            return true;
        }
        if (fDragSampleStart)
        {
            editParameter(paramSampleStart, false);
            fDragSampleStart = false;
            return true;
        }
        if (fDragSampleEnd)
        {
            editParameter(paramSampleEnd, false);
            fDragSampleEnd = false;
            return true;
        }
    }

    return false;
}

void DrumCloudUI::stateChanged(const char* key, const char* value)
{
    if (std::strcmp(key, "samplePath") == 0)
    {
        const std::string newPath = value ? value : "";
        if (newPath.empty() || newPath == fSamplePath)
        {
            fChoosingSample = false;
            return;
        }

        const bool chosen = fChoosingSample && !fRestoringFromParam;
        fChoosingSample = false;
        {
            std::lock_guard<std::mutex> lock(fPreviewMutex);
            fPreviewPath = newPath;
            fPreviewChosen = chosen;
            ++fPreviewGeneration;
            fPreviewPending = true;
            fPreviewResult.reset();
        }
        fLoadError = false;
        fLoadStatus = "CHECKING SAMPLE";
        fPreviewCV.notify_one();
        repaint();
        return;
    }

    UI::stateChanged(key, value);
}

void DrumCloudUI::parameterChanged(uint32_t index, float value)
{
    if (index == paramVolume) { fVolumeUi = value; repaint(); return; }
    if (index == paramDensity) { fDensityUi = value; repaint(); return; }
    if (index == paramRelease) { fReleaseMsUi = value; repaint(); return; }
    if (index == paramStartPosition) { fStartPosUi = value; repaint(); return; }
    if (index == paramPositionSpread) { fSpreadUi = value; repaint(); return; }
    if (index == paramScanSpeed) { fScanSpeedUi = value; repaint(); return; }
    if (index == paramFilter) { fFilterUi = value; repaint(); return; }
    if (index == paramResonance) { fResoUi = value; repaint(); return; }
    if (index == paramReverbSize) { fReverbSizeUi = value; repaint(); return; }
    if (index == paramReverbMix) { fReverbMixUi = value; repaint(); return; }
    if (index == paramVelocityToDensity) { fVelToDensityUi = value; repaint(); return; }
    if (index == paramVelocityToGrainSize) { fVelToGrainUi = value; repaint(); return; }
    if (index == paramPitchRate) { fPitchRateUi = value; repaint(); return; }
    if (index == paramScanJumpRate) { fJumpRateUi = value; repaint(); return; }
    if (index == paramScanJumpAmount) { fJumpAmountUi = value; repaint(); return; }
    if (index == paramScanJumpSmoothMs) { fJumpSmoothMsUi = value; repaint(); return; }
    if (index == paramSyncRate) { fSyncRateUi = value; repaint(); return; }
    if (index == paramRootNote) { fRootNoteUi = std::round(value); repaint(); return; }
    if (index == paramSampleFineTune) { fSampleFineTuneUi = value; repaint(); return; }
    if (index == paramGrainAttack) { fGrainAttackMsUi = value; repaint(); return; }
    if (index == paramGrainRelease) { fGrainReleaseMsUi = value; repaint(); return; }
    if (index == paramSampleStart) { fSampleStartUi = value; repaint(); return; }
    if (index == paramSampleEnd) { fSampleEndUi = value; repaint(); return; }
    if (index == paramAutoRoot) { fAutoRootUi = value; repaint(); return; }
    if (index == paramDelayMode) { fDelayModeUi = std::round(value); repaint(); return; }
    if (index == paramDelayTimeLeft) { fDelayTimeLeftUi = value; repaint(); return; }
    if (index == paramDelayTimeRight) { fDelayTimeRightUi = value; repaint(); return; }
    if (index == paramDelayFeedback) { fDelayFeedbackUi = value; repaint(); return; }
    if (index == paramDelayMix) { fDelayMixUi = value; repaint(); return; }
    if (index == paramDelayDamping) { fDelayDampingUi = value; repaint(); return; }
    if (index == paramTimeStretch) { fTimeStretchUi = value; repaint(); return; }
    if (index == paramPlaybackMode) { fPlaybackModeUi = (int)std::lround(value); repaint(); return; }
    if (index == paramScanMode) { fScanModeUi = (int)std::lround(value); repaint(); return; }
    if (index == paramScanPos) { repaint(); return; }
}

UI* createUI()
{
    return new DrumCloudUI();
}

} // namespace DISTRHO
