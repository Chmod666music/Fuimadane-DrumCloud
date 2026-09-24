# DrumCloud v1.9.0-beta.7

This release fixes the missing Linux desktop-portal support in packaged beta
artifacts.

## Fixed in beta.7

- Linux release builds now install the DBus development dependency required by
  DPF's XDG Desktop Portal backend
- REAPER VST3 sample selection now opens the desktop portal, matching the
  verified local build instead of falling back to DPF's embedded file browser
- Release validation inspects both CLAP and VST3 binaries for the portal backend
  and their DBus linkage; a package without portal support can no longer publish

## Included from beta.6

- The new **FUIMADANE - OPEN WEBSITE** control opens the user's normal system
  browser instead of delegating the link to a DAW's embedded browser
- Linux launches website links with `xdg-open` and falls back to `gio open`
- Windows uses the system URL handler and macOS uses the system `open` service
- Sample selection continues to request the host/native file portal first;
  DPF's bundled browser remains available only as a compatibility fallback
- The release regression now verifies both the portal-first sample workflow and
  the external Linux browser launchers inside CLAP and VST3 artifacts

## Included from beta.5

- Improved transient slicing with correctly paired marker strengths
- Stronger musical attacks are preferred over tiny sharp clicks
- Refined transient placement before the audible attack
- Remembered sample folders and portable WAV, FLAC and MP3 loading
- Resizable, HiDPI-aware interface and polyphonic MIDI slicing

## Downloads

- **Linux x86-64:** CLAP and VST3
- **Windows 10/11 x86-64:** CLAP and VST3
- **macOS 10.15 or newer:** universal Intel/Apple Silicon CLAP, VST3 and AU

## Validation status

The Linux CLAP and VST3 builds passed automated regression and were manually
verified on Ubuntu Studio: sample selection opened the desktop portal, while
the Fuimadane website opened in the normal system browser. Windows and macOS
packages remain unsigned beta builds and need testing in real hosts.

## Beta warning

This is an unsigned pre-release intended for testing. The macOS package is not
notarized. Do not disable system-wide security protections. Only install files
downloaded directly from this GitHub release and follow
`CROSS_PLATFORM_TESTING.md`.

Please report successes and failures using the repository's **DrumCloud beta
report** issue template. Include operating system/version, CPU architecture,
DAW/version, plugin format, result and reproduction steps.
