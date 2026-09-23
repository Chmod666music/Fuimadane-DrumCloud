# DrumCloud v1.9.0-beta.4

This hotfix supersedes beta.3. It keeps the complete scalable-UI and MIDI-slicer
milestone while restoring a faster, more familiar sample-loading workflow.

## Fixed in beta.4

- The native sample browser remembers the last used folder across browser
  closes, plugin instances, projects and application restarts
- If a sample is already loaded, the browser opens in that sample's folder
- A saved last folder is used when no sample is loaded, with Home as a safe
  fallback
- The existing asynchronous validation and sample-loading path remains intact

## Included from beta.3

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

Manual Linux CLAP and VST3 testing passed, including loading a sample, closing
the chooser and reopening it in the remembered folder. Automated Linux, Windows
and macOS builds pass. Windows and macOS packages remain beta builds that need
testing in real hosts.

## Beta warning

This is an unsigned pre-release intended for testing. The macOS package is not
notarized. Do not disable system-wide security protections. Only install files
downloaded directly from this GitHub release and follow
`CROSS_PLATFORM_TESTING.md`.

Please report successes and failures using the repository's **DrumCloud beta
report** issue template. Include operating system/version, CPU architecture,
DAW/version, plugin format, result and reproduction steps.
