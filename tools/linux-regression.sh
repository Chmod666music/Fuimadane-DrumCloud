#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
reg_tmp="$(mktemp -d)"
trap 'rm -rf -- "$reg_tmp"' EXIT

cd "$repo_root"

for command_name in g++ ffmpeg file ldd readelf strings; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing regression dependency: $command_name" >&2
        exit 1
    fi
done

echo "[1/4] Running native unit tests"
ffmpeg -hide_banner -loglevel error \
    -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=1' \
    -ac 2 -c:a pcm_s16le "$reg_tmp/codec.wav"
ffmpeg -hide_banner -loglevel error -i "$reg_tmp/codec.wav" -c:a flac "$reg_tmp/codec.flac"
ffmpeg -hide_banner -loglevel error -i "$reg_tmp/codec.wav" -c:a libmp3lame "$reg_tmp/codec.mp3"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/audio_loader.cpp AudioFileLoader.cpp \
    -o "$reg_tmp/audio-loader-test"
(cd "$reg_tmp" && ./audio-loader-test)

g++ -std=gnu++17 -O2 -Wall -Wextra tests/pitch_detector.cpp PitchDetector.cpp -I. \
    -o "$reg_tmp/pitch-detector-test"
"$reg_tmp/pitch-detector-test"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/filtered_delay.cpp FilteredStereoDelay.cpp -I. \
    -o "$reg_tmp/delay-test"
"$reg_tmp/delay-test"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/polyphonic_notes.cpp -I. \
    -o "$reg_tmp/polyphony-test"
"$reg_tmp/polyphony-test"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/time_stretch.cpp -I. \
    -o "$reg_tmp/time-stretch-test"
"$reg_tmp/time-stretch-test"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/slice_mapping.cpp -I. \
    -o "$reg_tmp/slice-mapping-test"
"$reg_tmp/slice-mapping-test"

g++ -std=gnu++17 -O2 -Wall -Wextra tests/transient_detector.cpp -I. \
    -o "$reg_tmp/transient-detector-test"
"$reg_tmp/transient-detector-test"

echo "[2/4] Building Linux CLAP and VST3"
./build.sh

clap_binary="$repo_root/bin/d_drumcloud.clap"
vst3_binary="$(find "$repo_root/bin/d_drumcloud.vst3" -type f -name 'd_drumcloud.so' -print -quit)"

test -f "$clap_binary"
test -n "$vst3_binary"
test -f "$vst3_binary"
test -x "$clap_binary"
test -x "$vst3_binary"

echo "[3/4] Checking binary linkage and portability"
for binary in "$clap_binary" "$vst3_binary"; do
    file "$binary" | grep -q 'ELF'

    linkage="$reg_tmp/$(basename "$binary").ldd"
    ldd -r "$binary" >"$linkage" 2>&1
    if grep -Eq 'not found|undefined symbol' "$linkage"; then
        cat "$linkage" >&2
        exit 1
    fi

    if readelf -d "$binary" | grep -E '(RPATH|RUNPATH)' | grep -Eq '/home/|/workspace/|/tmp/'; then
        echo "Non-portable runtime search path in $binary" >&2
        readelf -d "$binary" >&2
        exit 1
    fi

    if grep -aFq "$repo_root" "$binary"; then
        echo "Local checkout path embedded in $binary" >&2
        exit 1
    fi

    grep -aFq 'Fuimadane' "$binary"
    grep -aFq 'DrumCloud' "$binary"
done

grep -aFq 'dk.fuimadane.drumcloud' "$clap_binary"

echo "[4/4] Linux regression passed"
file "$clap_binary"
file "$vst3_binary"
du -h "$clap_binary" "$vst3_binary"
