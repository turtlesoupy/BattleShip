#!/bin/bash
# Package the Emscripten build + runtime files into web-dist/ for serving.
# Usage: scripts/package_web.sh [build-dir] [out-dir]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build-wasm}"
OUT_DIR="${2:-web-dist}"

# Preserve staged character bundles across repackages: bundles/ is populated
# by the pipeline, not the build, and wiping it here silently reverts the
# playable characters to whatever gets ad-hoc re-copied afterwards (that
# exact mixup shipped stale pre-BLNK bundles once — see
# docs/bugs/osb5_stale_owner_css_previews_2026-08-25.md's sibling incident).
if [ -d "$OUT_DIR/bundles" ]; then
  BUNDLE_STASH=$(mktemp -d)
  mv "$OUT_DIR/bundles" "$BUNDLE_STASH/bundles"
fi
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
if [ -n "${BUNDLE_STASH:-}" ]; then
  mv "$BUNDLE_STASH/bundles" "$OUT_DIR/bundles"
  rmdir "$BUNDLE_STASH"
fi

# Stamp a content-derived version into the shell so a redeploy always busts
# the browser cache for the JS glue (a stale glue + fresh wasm pair hangs).
BUILD_VERSION=$(shasum "$BUILD_DIR/BattleShip.wasm" | cut -c1-12)
sed "s/BUILD_VERSION/$BUILD_VERSION/g" web/index.html > "$OUT_DIR/index.html"
# The glue fetches "BattleShip.wasm" by name — version that URL too so the
# js/wasm pair always cache-busts together.
sed "s|BattleShip\.wasm|BattleShip.wasm?v=$BUILD_VERSION|g" "$BUILD_DIR/BattleShip.js" > "$OUT_DIR/BattleShip.js"
cp "$BUILD_DIR/BattleShip.wasm" "$OUT_DIR/"

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

# Hi-res texture packs are staged into MEMFS at /mods as ZIPs (HiResPack
# reads zips in place). Zip-only on wasm: the pack grammar's '#' characters
# in loose-PNG filenames hang the boot-time file scan on MEMFS, and '#'
# breaks URLs anyway — inside a zip both problems vanish.
# Extra defaults staged verbatim into MEMFS (e.g. BattleShip.cfg.json with
# the 4:3 render-buffer size — the browser has no persisted config).
if [ -d web/files-extra ]; then
  cp web/files-extra/* "$OUT_DIR/files/"
fi

if [ -d web/mods ]; then
  mkdir -p "$OUT_DIR/files/mods"
  cp web/mods/*.zip "$OUT_DIR/files/mods/" 2>/dev/null || true
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
    # URL-encode characters that break URLs (notably '#' in HiResPack names —
    # an unencoded '#' starts the fragment and the server sees a truncated
    # path). The MEMFS path stays raw.
    enc=$(printf '%s' "$p" | sed -e 's/%/%25/g' -e 's/#/%23/g' -e 's/ /%20/g' -e 's/?/%3F/g')
    printf '  { "path": "/%s", "url": "files/%s" }' "$p" "$enc"
  done
  echo ''
  echo ']}'
) > "$OUT_DIR/manifest.json"

echo "Packaged into $OUT_DIR:"
du -sh "$OUT_DIR"/* | sed 's|'"$OUT_DIR"'/||'
