# DrumCloud native: development after v1.8.1

The native C++/DPF instrument and DrumCloud JS are separate plugins. Use the native v1.8.1 release tag as the baseline; preserve existing parameter indices and plugin IDs so saved host projects still reopen. Target Linux, Windows and macOS from one DSP codebase; build and package separately for each platform.

## Completed for v1.9.0-beta.2: sample playback modes

The `PLAY MODE` control is implemented for the main sample/scanner playhead between Sample Start and Sample End:

- `LOOP ↻`: move from Start to End and wrap to Start, preserving the current looping behavior.
- `ONE SHOT →`: move once from Start to End and stop at End. A new MIDI note retriggers from Start; active grains should finish through their normal grain release instead of being cut abruptly.
- `PING PONG ↔`: alternate continuously between Start and End while the voice is active.
- Keep playback position and direction per voice so polyphonic notes do not interfere with each other.
- Make all modes cooperate with Sample Start/End, Scan Rate, Time Stretch, Position Spread/Jitter, note-off behavior and project state recall.
- Show the selected mode clearly in the dark-and-gold UI. If practical, show current direction with a small white arrow.
- Preserve the existing behavior as the default for old sessions and append any new parameter indices without renumbering existing parameters.
- Linux CLAP/VST3 regression and manual playback tests passed. Cross-platform host testing continues through the numbered beta.

Possible follow-up: `REVERSE ←` one-shot and a user choice between retriggering and continuing from the current position.

## In development: MIDI slicer

- `SLICE ON` divides the current Sample Start/End region into equal, non-overlapping slices.
- Choose 2, 4, 8 or 16 slices from the UI; the automatable parameter itself accepts every integer from 2–16.
- MIDI note 36 triggers slice 1 and consecutive notes trigger consecutive slices.
- Slice voices retain DrumCloud's granular density, envelopes, filter, reverb, delay, scan and playback modes, but play at the sample's original pitch instead of transposing each slice by its trigger key.
- Slice bounds are stored per grain so overlapping/polyphonic slices cannot move one another's playback region.
- The waveform shows every boundary and highlights the last triggered slice. Slicer mode defaults to off, preserving old sessions and the normal chromatic instrument mode.
- `SLICE TR` reuses the transient analysis performed during sample loading. Real detected onsets become slice starts; fallback snap-grid markers are deliberately excluded. The selected count acts as a maximum, and the UI reports detected/maximum slices.

## DrumCloud JS feature reference

The current file in [DrumCloud-ReaPack](https://github.com/Chmod666music/DrumCloud-ReaPack/blob/main/Effects/DrumCloud/DrumCloud_JS.jsfx) declares v0.27. Its source exposes sample start/end, five position modes, per-grain direction, pitch/density/stereo spread, grain attack/release, eight MIDI voices, automatic/manual root handling, delay, and room/hall/shimmer reverb.

## Platform targets

- Linux: CLAP and VST3 first; keep LV2 support and evaluate VST2 maintenance separately.
- Windows: VST3 first, then CLAP where host support warrants it. Test native file selection, paths, UI rendering, installation and project recall in Windows DAWs.
- macOS: VST3 and AU first, then CLAP where host support warrants it. Build and test Apple Silicon and Intel variants, native dialogs, UI rendering, sample paths and host recall. Investigate signing, notarization and bundle integrity before distribution; source compatibility in v1.8.1 does not establish working binaries.
- Run builds and smoke tests on the target operating system. Keep format IDs and state behavior stable, and package the correct UI resources with each binary.

## Beta testing and releases

- Publish numbered beta builds separately for Linux, Windows and macOS as each target becomes buildable. State exactly which architecture and plugin format each download supports; an unbuilt or untested platform remains marked pending.
- Begin with a private/internal smoke test of sample loading, state recall, MIDI, GUI resizing and host stability. Then offer public beta packages and invite volunteers without assuming testers are available.
- Provide a short test checklist and a GitHub issue template requesting operating system/version, CPU architecture, plugin format, DAW/version, steps, expected/actual behavior, screenshots or logs, and whether the old project still opens. Ask testers not to include personal sample files unless they choose to share them.
- Keep a per-host matrix for Linux (Bitwig, REAPER, Ardour), Windows (at least two VST3 DAWs), and macOS (VST3 and AU in at least one host each). Track confirmed results, failures and untested cells separately. Do not equate one host passing with all hosts passing.
- Iterate beta releases after fixes. Promote a stable cross-platform release only after confirmed installation, sample load, project restore and playback tests on each advertised operating system. If a platform lags, release tested platforms and identify the pending platform clearly.

## Repository identity

This dedicated repository contains the imported native source. DPF is pinned as a submodule at the v1.8.1 release commit; the former `Chmod666music/DPF` repository and release remain historical references. The Linux CLAP and VST3 builds and Bitwig smoke tests passed from the standalone checkout. Before beta packaging, confirm a clean clone, another Linux host, and portable paths. Keep future releases and tester reports here, and continue to credit DPF upstream.

## Native order of work

1. Move file decoding and waveform preparation out of the audio callback. Build a bounded decoded buffer, then transfer it at a safe point without blocking, allocating, or freeing in the audio callback. Keep the old sample playing on a failed load. Verify rapid sample changes and project restore in Bitwig and REAPER, then in representative Windows and macOS hosts.
2. Add bounded sample start/end controls. Restrict grain start, snap/zero-cross seeking, playback, and all scan modes to the selected region. Handle short regions without reading out of bounds. Show the range and grains on the waveform.
3. Add selectable grain direction (forward/backward/alternate/random), then pitch spread and stereo spread. Keep defaults matching v1.8.1 so existing sessions sound the same.
4. Extend scan to backward, ping-pong, and random walk. Show scan mode and movement clearly in the GUI.
5. Add root note/fine tune, with optional detection performed outside the audio callback and a confidence display. Manual root must remain intact when detection is uncertain.
6. Add envelope, delay and reverb choices only after the sample and grain path is stable. Design a resizable UI with grouped sample, grain, movement, tone and space controls.

For each stage build CLAP and VST3 on Linux, Windows VST3, and macOS VST3/AU as applicable; confirm state recall and test long/invalid files plus rapid MIDI. Add platform build automation once reproducible local builds exist. Release binaries only after DAW smoke tests on the corresponding operating system.
