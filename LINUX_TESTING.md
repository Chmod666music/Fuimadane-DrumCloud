# DrumCloud Linux regression

Run the automated regression from a clean checkout:

```bash
git submodule update --init DPF
git -C DPF submodule update --init dgl/src/pugl-upstream
./tools/linux-regression.sh
```

The script tests bounded WAV/FLAC/MP3 loading, pitch detection, filtered delay,
polyphonic note ownership and time-stretch mapping. It then builds CLAP and
VST3 and checks both ELF modules for unresolved libraries, undefined symbols,
non-portable runtime paths, leaked checkout paths and expected plugin metadata.

## Host checklist

For each format and host:

1. Insert DrumCloud as an instrument and open the GUI.
2. Load WAV, FLAC and MP3 samples, including one invalid or oversized file.
3. Play single notes, held intervals and chords; release notes in a different order.
4. Move Sample Start/End and verify grains remain inside the selected region.
5. Test Hold, Scan, Jump and Sync modes plus 0.5x, 1x and 2x stretch.
6. Test Stereo and Ping Pong delay and confirm output remains controlled.
7. Save, close and reopen the project; confirm sample and parameters return.
8. Close the plugin or host immediately after requesting a sample load.

## Current matrix

| Host | CLAP | VST3 | Notes |
|---|---:|---:|---|
| Bitwig Studio | Pass | Pass | Sample loading, MIDI playback, state recall, polyphony, delay, stretch and GUI validated on Ubuntu Studio. |
| REAPER | Pass | Pass | Both formats load with the current GUI; sample loading and playback validated. |
| Ardour 9.8 | —* | —* | Host-level pass: DrumCloud loads and operates correctly, including the current GUI and sample workflow. *The exact format selected by the host was not recorded separately. |
| Renoise 3.4.4 | —* | —* | Host-level pass: DrumCloud is discovered and loads correctly. *The exact format selected by the host was not recorded separately. |

Record the DAW version, OS version, CPU architecture and exact failing step for
every new result. A successful build is not counted as a host pass.
