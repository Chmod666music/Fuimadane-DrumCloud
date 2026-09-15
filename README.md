# Fuimadane DrumCloud

DrumCloud is Fuimadane's native granular sample instrument. The last published version is **v1.8.1** for Linux. The next development cycle targets Linux, Windows and macOS beta builds, with each platform released only after local build and host tests.

The source currently lives in [Chmod666music/DPF](https://github.com/Chmod666music/DPF/tree/v1.8.1/examples/DrumCloud). This dedicated repository is being prepared to hold the plugin source, releases and beta tester feedback. DrumCloud is built with [DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF); DPF remains credited and is not authored by Fuimadane.

The migration script is in `tools/import-from-dpf.sh`. It copies the plugin's source and artwork from the v1.8.1 development branch and pins the DPF dependency to the v1.8.1 release commit. The imported code must be built and tested before any beta binary is published.
