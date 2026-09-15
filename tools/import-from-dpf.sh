#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
if [[ "$(pwd -P)" != "$REPO_ROOT" ]]; then
  echo "Run from the Fuimadane-DrumCloud repository root."
  exit 1
fi
if [[ "$(basename "$REPO_ROOT")" != "Fuimadane-DrumCloud" ]]; then
  echo "Wrong checkout: $REPO_ROOT"
  exit 1
fi
if [[ -n "$(git status --porcelain)" ]]; then
  echo "The checkout has changes. Commit or move them before importing."
  exit 1
fi
if [[ -e "$REPO_ROOT/Makefile" || -e "$REPO_ROOT/DPF" ]]; then
  echo "The import appears to have run already; refusing to overwrite it."
  exit 1
fi

TEMP_SOURCE="$(mktemp -d)"
trap 'rm -rf "$TEMP_SOURCE"' EXIT
git clone --branch drumcloud-v181-next --single-branch \
  https://github.com/Chmod666music/DPF.git "$TEMP_SOURCE/old"

SOURCE_DIR="$TEMP_SOURCE/old/examples/DrumCloud"
if [[ ! -f "$SOURCE_DIR/SendNoteExamplePlugin.cpp" || ! -f "$SOURCE_DIR/UI/fuimadane_wave_bg.png" ]]; then
  echo "The selected source branch is incomplete."
  exit 1
fi

(
  cd "$SOURCE_DIR"
  tar --exclude='./DPF' --exclude='./README.md' --exclude='*.zip' -cf - .
) | tar -xf - -C "$REPO_ROOT"

# Pin the old DPF snapshot first; moving to upstream DPF is a separate tested change.
git submodule add https://github.com/Chmod666music/DPF.git DPF
git -C DPF checkout v1.8.1

cat > Makefile <<'MAKEFILE'
NAME = d_drumcloud
FILES_DSP = SendNoteExamplePlugin.cpp AudioFileLoader.cpp
FILES_UI = DrumCloudUI.cpp AudioFileLoader.cpp
UI_TYPE = opengl
CXXFLAGS += -std=gnu++17
DPF_BUILD_DIR = $(CURDIR)/build/d_drumcloud
DPF_TARGET_DIR = $(CURDIR)/bin
TARGETS ?= clap vst3

include DPF/Makefile.plugins.mk

all: $(TARGETS)
MAKEFILE

cat > build.sh <<'BUILD'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -f DPF/Makefile.plugins.mk ]]; then
  echo "DPF submodule missing. Run: git submodule update --init --recursive"
  exit 1
fi
make CONFIG=Release
BUILD
chmod +x build.sh

cat > .gitignore <<'IGNORE'
/bin/
/build/
*.o
*.d
IGNORE

echo "Source, GUI image and decoder headers imported into: $REPO_ROOT"
echo "DPF is pinned as a submodule at: $(git -C DPF rev-parse --short HEAD)"
echo "Review: git status --short"
echo "Build on Linux: ./build.sh"
echo "No commit, push or binary release was made by this script."
