# DrumCloud v1.9.0-beta.1

The first public cross-platform beta of Fuimadane DrumCloud is here.

DrumCloud is a granular sample instrument for turning WAV, FLAC and MP3 files
into playable, moving clouds of sound. Version 1.9 expands the original Linux
instrument into a much more expressive instrument with a redesigned dark and
gold interface.

## Highlights

- Automatic sample pitch analysis with Root MIDI Note and Sample Fine Tune
- Manual sample start/end region and visible active grain heads
- Independent granular time stretch from 0.25x to 4x
- True polyphonic MIDI note scheduling
- Grain attack and release controls
- Hold, Scan, Jump and Sync movement modes
- Filtered Stereo and Ping Pong delay
- Output peak protection
- Safer bounded WAV, FLAC and MP3 loading outside the audio callback
- Project state recall for the selected sample and parameters
- Dark-and-gold Fuimadane interface

## Downloads

- **Linux x86-64:** CLAP and VST3
- **Windows 10/11 x86-64:** CLAP and VST3
- **macOS 10.15 or newer:** universal Intel/Apple Silicon CLAP, VST3 and AU

## Beta warning

This is a pre-release intended for testing. The Linux builds have passed host
tests in Bitwig Studio, REAPER, Ardour and Renoise. Windows and macOS packages
compile successfully but still need real DAW testing.

The Windows and macOS packages are not code signed, and the macOS package is
not notarized. Do not disable system-wide security protections. Only install
downloads obtained directly from this GitHub release, and follow
`CROSS_PLATFORM_TESTING.md` for platform-specific guidance.

Please report both successes and failures through the repository's
**DrumCloud beta report** issue template. Include the operating system, CPU,
DAW version and exact plugin format tested.
