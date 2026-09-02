#!/bin/bash
# Build Torch as a browser module (web-dist/torch/) for in-browser asset
# extraction. See docs/web_rom_extraction.md. Requires an activated emsdk
# (emcmake on PATH).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BUILD_DIR="${1:-torch/build-wasm}"
emcmake cmake -S torch -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_STANDALONE=ON -DBUILD_SSB64=ON -DBUILD_STORMLIB=OFF \
  -DBUILD_SM64=OFF -DBUILD_MK64=OFF -DBUILD_SF64=OFF -DBUILD_PM64=OFF \
  -DBUILD_FZERO=OFF -DBUILD_MARIO_ARTIST=OFF -DBUILD_NAUDIO=ON \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DTORCH_WEB_EXTRA_SOURCES="$ROOT/port/css_icons/stage_assets_derive.cpp" \
  -DTORCH_WEB_EXTRA_INCLUDES="$ROOT/port/css_icons" \
  -DTORCH_WEB_EXTRA_DEFINES="TORCH_WEB_STAGE_ASSETS"
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ls -la "$BUILD_DIR/torch.js" "$BUILD_DIR/torch.wasm"
