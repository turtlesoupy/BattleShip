#!/bin/bash
# Package the Emscripten build + runtime files into web-dist/ for serving.
# Usage: scripts/package_web.sh [build-dir] [out-dir]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build-wasm}"
OUT_DIR="${2:-web-dist}"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

cp web/index.html "$OUT_DIR/"
cp "$BUILD_DIR/BattleShip.js" "$BUILD_DIR/BattleShip.wasm" "$OUT_DIR/"

# Runtime files the game reads from its working directory. Mirrored into
# files/ and listed in manifest.json; the shell stages them into MEMFS.
mkdir -p "$OUT_DIR/files"
for f in BattleShip.o2r BattleShip.o2r.recipe f3d.o2r gamecontrollerdb.txt config.yml; do
  if [ -f "$BUILD_DIR/$f" ]; then
    cp "$BUILD_DIR/$f" "$OUT_DIR/files/"
  else
    echo "warning: $BUILD_DIR/$f missing, skipped" >&2
  fi
done
if [ -d "$BUILD_DIR/assets" ]; then
  cp -R "$BUILD_DIR/assets" "$OUT_DIR/files/assets"
fi

# Recipe sidecar: on native the game writes this after first-run extraction;
# in the browser assets are baked at build time, so pre-write it to keep
# first_run from thinking the archive is stale. Hash comes from the build's
# compile flags (SSB64_ASSET_RECIPE_HASH).
if [ ! -f "$OUT_DIR/files/BattleShip.o2r.recipe" ]; then
  RECIPE_HASH=$(awk 'match($0, /SSB64_ASSET_RECIPE_HASH=\\"[a-f0-9]+/) { s = substr($0, RSTART, RLENGTH); sub(/.*"/, "", s); print s; exit }' "$BUILD_DIR/build.ninja")
  if [ -n "$RECIPE_HASH" ]; then
    printf '%s' "$RECIPE_HASH" > "$OUT_DIR/files/BattleShip.o2r.recipe"
  fi
fi

# Manifest: MEMFS path -> URL
(
  cd "$OUT_DIR/files"
  echo '{ "files": ['
  first=1
  find . -type f | sed 's|^\./||' | LC_ALL=C sort | while read -r p; do
    [ $first -eq 1 ] && first=0 || echo ','
    printf '  { "path": "/%s", "url": "files/%s" }' "$p" "$p"
  done
  echo ''
  echo ']}'
) > "$OUT_DIR/manifest.json"

echo "Packaged into $OUT_DIR:"
du -sh "$OUT_DIR"/* | sed 's|'"$OUT_DIR"'/||'
