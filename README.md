# Fuimadane DrumCloud

**Fuimadane DrumCloud** is a granular sample instrument built with the [DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF). It loads WAV, FLAC and MP3 samples, shows their waveform, and offers granular scan modes, a filter and cloud reverb.

The latest published binaries are the [Linux v1.8.1 release in the former DPF repository](https://github.com/Chmod666music/DPF/releases/tag/v1.8.1). This dedicated repository holds the imported v1.8.1 source and future development. No Windows or macOS binary, and no new beta binary, has been published here yet.

## Build from source

The DPF dependency is pinned as a Git submodule. On Linux:

```bash
git clone https://github.com/Chmod666music/Fuimadane-DrumCloud.git
cd Fuimadane-DrumCloud
git submodule update --init DPF
git -C DPF submodule update --init dgl/src/pugl-upstream
./build.sh
```

The output goes to `bin/`. See [BUILD.md](BUILD.md) before installing or testing beside an existing version with the same plugin ID. The checked-in `ArtworkData.hpp` embeds the UI artwork in every format; [tools/embed-artwork.py](tools/embed-artwork.py) regenerates it from the PNG when artwork changes.

## Sample size in the current development branch

This branch accepts WAV, FLAC and MP3 files containing at most **16 million frames** and **32 million decoded float samples**. A stereo file at 48 kHz can be about 5 minutes 33 seconds long; at 44.1 kHz, about 6 minutes 2 seconds. Higher channel counts may hit the decoded-sample limit sooner. These caps protect memory on Linux, Windows and macOS; file size on disk is not a reliable substitute for decoded length.

The waveform preview is prepared in small blocks away from the UI event thread. If a new file is too long, missing, damaged or unsupported, DrumCloud displays the reason and leaves the previously selected sample and waveform in place. A saved project stores the sample **path**, so the audio file must remain at that path for recall. A supported file may still fail DSP preparation if it is changed or removed between validation and playback; the DSP retains its previous audio in that case.

## Beta development

Future numbered beta builds will target Linux, Windows and macOS. Each download will identify its operating system, CPU architecture and format, and volunteers will be invited to report results by DAW. See [NEXT.md](NEXT.md) for the feature roadmap and tester matrix. Features from the separate [DrumCloud JS project](https://github.com/Chmod666music/DrumCloud-ReaPack) are candidates for the native instrument.

## Credits and source history

DrumCloud is developed by [Fuimadane](https://linktr.ee/Fuimadane). It uses DPF, whose authors retain credit and their own license. This source was imported from [Chmod666music/DPF, tag v1.8.1](https://github.com/Chmod666music/DPF/tree/v1.8.1/examples/DrumCloud), with the subsequent development branch adding safer sample selection and portable artwork. The old repository and release remain available as historical references.
