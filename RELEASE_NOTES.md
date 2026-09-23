# DrumCloud v1.9.0-beta.5

This release supersedes beta.4 and completes the first transient-slicer
milestone. Its slice placement and sample-browser workflow have been manually
verified on Linux in REAPER.

## Fixed and improved in beta.5

- Fixed incorrect transient slices caused by marker positions and their
  strength values becoming misaligned while duplicate markers were removed
- Transient positions and strengths now remain paired through sorting and
  compaction, so the strongest musical attacks select the correct boundaries
- Slice candidates are ranked using both onset novelty and the actual event
  peak, preventing tiny sharp clicks from outranking stronger hits
- Attack positions are refined backwards from the selected event peak for
  cleaner placement before the audible transient
- Restored the host/native sample chooser in capable DAWs, with DPF's browser
  retained as a fallback
- Last-folder persistence remains active after selecting a sample

## Slicer and interface milestone

- Equal and transient-detected MIDI slicing with 2–16 slices
- Adjustable transient sensitivity and refined onset placement
- MIDI note 36 and upward trigger consecutive slices at original sample pitch
- Polyphonic slice playback with safe, independent grain boundaries
- Waveform slice markers and active-slice highlighting
- Resizable, HiDPI-aware interface with scaled drawing and pointer input
- Clearer waveform layout plus easy-to-grab Sample Start/End handles

## Downloads

- **Linux x86-64:** CLAP and VST3
- **Windows 10/11 x86-64:** CLAP and VST3
- **macOS 10.15 or newer:** universal Intel/Apple Silicon CLAP, VST3 and AU

## Validation status

Manual Linux CLAP and VST3 testing passed, including sample loading, remembered
folder behavior, transient placement and MIDI slice playback. Automated Linux,
Windows and macOS builds pass. Windows and macOS packages remain beta builds
that need testing in real hosts.

## Beta warning

This is an unsigned pre-release intended for testing. The macOS package is not
notarized. Do not disable system-wide security protections. Only install files
downloaded directly from this GitHub release and follow
`CROSS_PLATFORM_TESTING.md`.

Please report successes and failures using the repository's **DrumCloud beta
report** issue template. Include operating system/version, CPU architecture,
DAW/version, plugin format, result and reproduction steps.
