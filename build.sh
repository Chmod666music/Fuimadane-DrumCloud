#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -f DPF/Makefile.plugins.mk ]]; then
  echo "DPF submodule missing. Run: git submodule update --init --recursive"
  exit 1
fi
if [[ ! -f ArtworkData.hpp ]]; then
  python3 tools/embed-artwork.py
fi
make CONFIG=Release
