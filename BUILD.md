# Build DrumCloud from this repository

The imported source is based on DrumCloud v1.8.1. Linux CLAP and VST3 compile from this checkout. No new beta binary has been released.

Clone with submodules, or after cloning run:

```bash
git submodule update --init DPF
git -C DPF submodule update --init dgl/src/pugl-upstream
./build.sh
```

The build script generates `ArtworkData.hpp` from the checked-in PNG if it is absent. Commit the generated header before Windows/macOS beta builds so all platforms embed the same artwork without depending on a developer machine path or a Python installation at build time. Regenerate it with `python3 tools/embed-artwork.py` whenever the PNG changes.

The outputs are in `bin/d_drumcloud.clap` and `bin/d_drumcloud.vst3`. Test the new plugin from a custom plugin path before replacing an installed version with the same plugin ID. The old `install.sh` and `package.sh` are removed because they target the former nested DPF layout and v1.8.1 release packages.

Windows and macOS build instructions will be written after platform builds and host tests pass. Do not publish binaries from this import alone.
