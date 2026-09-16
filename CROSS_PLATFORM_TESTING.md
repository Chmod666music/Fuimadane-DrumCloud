# DrumCloud Windows and macOS beta testing

The Windows and macOS files produced by GitHub Actions are **unsigned beta
builds**. A green build proves that the source compiled and was packaged for
the target platform. It does not prove that a DAW can discover, open or run the
plugin correctly.

## Download a beta artifact

1. Open the relevant commit or pull request on GitHub and select **Checks**.
2. Open **Windows and macOS beta builds**.
3. Download the artifact for `win64` or `macos-universal` from the run summary.
4. Keep the artifact name and commit SHA in the beta report.

GitHub requires a signed-in account to download workflow artifacts. Test a
beta from a custom or user-local plugin folder where possible; do not replace a
known working installation until the beta has passed.

## Windows 10/11 x86-64

The Windows artifact contains CLAP and VST3 builds. Copy the complete plugin
file or bundle to one of these standard locations, then force a plugin rescan:

- CLAP: `C:\Program Files\Common Files\CLAP\`
- VST3: `C:\Program Files\Common Files\VST3\`

Windows may show a SmartScreen warning because beta artifacts are not code
signed. Do not disable system-wide security controls. Only continue when the
artifact came directly from this repository's GitHub Actions run.

## macOS Intel and Apple Silicon

The `macos-universal` artifact contains an unsigned installer package with
universal CLAP, VST3 and AU bundles. It targets both Intel and Apple Silicon.
The installer uses the system-wide plugin folders:

- CLAP: `/Library/Audio/Plug-Ins/CLAP/`
- VST3: `/Library/Audio/Plug-Ins/VST3/`
- AU: `/Library/Audio/Plug-Ins/Components/`

Gatekeeper may refuse an unsigned beta. Do not weaken Gatekeeper globally. A
tester who understands and accepts the risk can use Finder's **Open** action or
approve the blocked package in **System Settings > Privacy & Security**. Public
releases must be Developer ID signed and notarized before we call them ready.

After installing an AU build, restart the DAW or its Audio Unit scanner. Record
whether the DAW ran natively or through Rosetta.

## Required host test

For each OS, CPU, DAW and format:

1. Confirm the plugin is discovered as an instrument and opens without a crash.
2. Confirm the dark-and-gold GUI draws correctly and can be reopened.
3. Load WAV, FLAC and MP3 files, then play single notes and chords over MIDI.
4. Verify root detection, fine tune, sample start/end and visible grain heads.
5. Exercise Hold, Scan, Jump, Sync and 0.5x, 1x and 2x time stretch.
6. Exercise Stereo and Ping Pong delay and confirm peak protection remains stable.
7. Save, close and reopen the project; confirm sample and parameters return.
8. Request a sample load and immediately close the plugin or DAW.

Submit both passing and failing results with the **DrumCloud beta report** issue
template. Include OS version, CPU architecture, DAW version, exact format,
artifact name and commit SHA. Do not share private samples or credentials.

## Initial beta matrix

| Platform | Architecture | Formats built | Build | Host validation |
|---|---|---|---|---|
| Windows 10/11 | x86-64 | CLAP, VST3 | Pending CI | Pending testers |
| macOS | Universal: Intel + Apple Silicon | CLAP, VST3, AU | Pending CI | Pending testers |

Signing, notarization and an end-user installer/release archive are separate
release-engineering tasks and are not implied by this beta workflow.
