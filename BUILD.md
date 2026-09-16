# Build DrumCloud from this repository

The imported source is based on DrumCloud v1.8.1. Linux CLAP and VST3 compile from this checkout. No new beta binary has been released.

Clone with submodules, or after cloning run:

```bash
git submodule update --init DPF
git -C DPF submodule update --init dgl/src/pugl-upstream
./build.sh
```

The v1.9 interface is drawn directly with portable DPF/OpenGL primitives and does not require external GUI artwork at build or run time.

Run `./tools/linux-regression.sh` to execute the native tests, build both Linux formats and inspect the resulting binaries for unresolved dependencies and non-portable paths.

The outputs are in `bin/d_drumcloud.clap` and `bin/d_drumcloud.vst3`. Test the new plugin from a custom plugin path before replacing an installed version with the same plugin ID. The old `install.sh` and `package.sh` are removed because they target the former nested DPF layout and v1.8.1 release packages.

Windows and macOS build instructions will be written after platform builds and host tests pass. Do not publish binaries from this import alone.
