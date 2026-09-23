# DrumCloud v1.9.0-beta.3

This milestone turns DrumCloud into a compact granular slicer instrument while
making its dark-and-gold interface easier to use across different displays.

## New in beta.3

- **SLICE EQ** divides the selected Sample Start/End region into 2–16 equal,
  non-overlapping slices
- **SLICE TR** detects real transient onsets throughout the complete sample
- MIDI note 36 and upward trigger consecutive slices at the sample's original pitch
- Polyphonic slice playback with independent, safe grain boundaries per voice
- **SENS** changes transient sensitivity immediately without reloading the sample
- Strongest suitably spaced attacks are selected across the region, preventing a
  dense early cluster from consuming all available slices
- Improved onset refinement places boundaries just before broad and low-frequency
  attacks instead of inside their waveform peaks
- Waveform slice boundaries and active-slice highlighting
- Resizable, HiDPI-aware interface with correctly scaled drawing and pointer input
- Playback and slicer controls moved above the waveform for a clearer overview
- Compact gold Sample Start/End grab handles with wider hit areas, reducing
  accidental sample-dialog openings
- Slicer defaults to off, preserving the normal chromatic instrument workflow

## Downloads

- **Linux x86-64:** CLAP and VST3
- **Windows 10/11 x86-64:** CLAP and VST3
- **macOS 10.15 or newer:** universal Intel/Apple Silicon CLAP, VST3 and AU

## Validation status

The Linux CLAP and VST3 builds passed the complete native regression suite and
manual testing in REAPER. Automated Linux, Windows and macOS builds pass. The
Windows and macOS packages remain beta builds that need testing in real hosts.

## Beta warning

This is an unsigned pre-release intended for testing. The macOS package is not
notarized. Do not disable system-wide security protections. Only install files
downloaded directly from this GitHub release and follow
`CROSS_PLATFORM_TESTING.md`.

Please report successes and failures using the repository's **DrumCloud beta
report** issue template. Include operating system/version, CPU architecture,
DAW/version, plugin format, result and reproduction steps.
