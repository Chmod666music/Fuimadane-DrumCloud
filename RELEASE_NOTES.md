# DrumCloud v1.9.0-beta.2

This beta adds dedicated playback-boundary control to Fuimadane DrumCloud's
polyphonic granular scanner.

## New in beta.2

- **PLAY LOOP**: scan from the selected start position to Sample End, then wrap
- **PLAY ONE SHOT**: scan once to Sample End and stop creating new grains
- **PLAY PING PONG**: alternate continuously between the sample-region boundaries
- Independent playback position, direction and one-shot completion for every
  active MIDI note, preventing polyphonic voices from moving each other's playhead
- New notes retrigger from the selected start position
- Existing grains finish through their natural release when ONE SHOT reaches the end
- Playback Mode is a persistent automatable parameter and is recalled with projects
- Dark-and-gold waveform control for selecting the playback behaviour
- Corrected Linux regression checks for reliable binary portability validation

## Downloads

- **Linux x86-64:** CLAP and VST3
- **Windows 10/11 x86-64:** CLAP and VST3
- **macOS 10.15 or newer:** universal Intel/Apple Silicon CLAP, VST3 and AU

## Validation status

The Linux CLAP and VST3 builds passed the complete native regression suite and
manual playback testing on Ubuntu Studio. Windows and macOS packages are
cross-compiled automatically and still need real host testing.

## Beta warning

This is an unsigned pre-release intended for testing. The macOS package is not
notarized. Do not disable system-wide security protections. Only install files
downloaded directly from this GitHub release and follow
`CROSS_PLATFORM_TESTING.md`.

Please report successes and failures using the repository's **DrumCloud beta
report** issue template. Include operating system/version, CPU architecture,
DAW/version, plugin format, result and reproduction steps.
