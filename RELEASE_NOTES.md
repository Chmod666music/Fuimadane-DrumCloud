# DrumCloud v1.9.0-beta.8

This beta fixes Ardour stereo output and the jumping or blinking waveform markers when a project contains more than one DrumCloud instance.

## Fixed in beta.8

- Both left and right outputs now reach Ardour VST3. The plugin uses DPF's normal stereo port mapping.
- The FILTERED DELAY MODE control changes between OFF, STEREO and PING PONG with a click.
- Each plugin instance now keeps its own scan playhead, grain markers, slice boundaries, active slice and detected pitch. Multiple DrumCloud instances can no longer overwrite one another's waveform display.

## Compatibility

- Linux x86-64: CLAP and VST3
- Windows 10/11 x86-64: CLAP and VST3
- macOS 10.15+: universal Intel and Apple Silicon CLAP, VST3 and AU

Linux CLAP/VST3 regression passed. Ardour VST3 was manually tested on Ubuntu Studio with two plugin instances: audio plays on both channels, markers stay stable in slice mode, and the sample and settings return after saving and reopening the project. Windows and macOS builds compile successfully but still need real DAW tests.

The Linux desktop portal for sample selection and system browser link from beta.7 remain included. These are unsigned beta packages; the macOS installer is not notarized. Only install packages downloaded from this GitHub release. Please include OS, CPU, DAW/version, plugin format and reproduction steps in [beta reports](https://github.com/Chmod666music/Fuimadane-DrumCloud/issues/new/choose).
