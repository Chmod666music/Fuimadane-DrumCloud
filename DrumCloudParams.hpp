#pragma once

enum DrumCloudParams
{
    paramVolume = 0,
    paramDensity,
    paramVelocityToDensity,
    paramVelocityToGrainSize,
    paramPitchRate,
    paramRelease,
    paramStartPosition,
    paramPositionSpread,
    paramSnapMs,
    paramScanSpeed,
    paramScanMode,
    paramScanJumpRate,
    paramScanJumpAmount,
    paramScanJumpSmoothMs,
    paramSyncRate,
    paramFilter,
    paramResonance,
    paramReverbSize, // 👈 Her er de nye!
    paramReverbMix,  // 👈 Her er de nye!
    paramSamplePath,
    paramScanPos,

    // v1.9 additions: append only, preserving all v1.8.x parameter IDs.
    paramRootNote,
    paramSampleFineTune,
    paramGrainAttack,
    paramGrainRelease,
    paramSampleStart,
    paramSampleEnd,
    paramAutoRoot,
    paramCount
};