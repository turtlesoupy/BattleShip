# In-browser asset extraction (web build)

The browser package no longer ships `BattleShip.o2r`. The archive is built
inside the player's browser from the ROM they upload, so no ROM-derived
bytes are ever hosted, cached at the edge, or served to anyone who did not
supply a ROM.

## Pieces

| Piece | Where | Role |
|---|---|---|
| Torch wasm module | `torch/build-wasm/torch.{js,wasm}` → `web-dist/torch/` | Torch compiled with Emscripten. `torch/src/lib/web.cpp` exports `torch_extract_o2r(rom, len, srcDir, dstDir)`; the CLI is compiled out. |
| Recipe tree | `web-dist/torch/{config.yml,yamls/us/*.yml,recipe.json}` | Same inputs standalone Torch reads with `-s`. Port metadata only. |
| Worker | `web/torch-worker.js` | Stages the recipe tree + ROM bytes into the module's MEMFS, runs the export, posts the archive back. |
| Stager | `web/rom-extract.js` | Called from the shell's `preRun` when `/BattleShip.o2r` is absent. Looks up the ROM in IndexedDB, reuses a cached archive for this recipe, or runs the worker and caches the result. |
| Site hook | `pipeline/web-prototype/shared/rom-store.js` | After the server validates the ROM hash, the site stores the canonical ROM bytes in IndexedDB (same origin as the engine iframe). |

IndexedDB `opensmash-rom` v1: `roms` (by SHA-1), `archives` (by
`<recipe-hash>:<sha1>`), `meta.current`. The recipe hash is the one CMake
stamps into `BattleShip.o2r.recipe`, so a deploy that changes the extraction
recipe rebuilds the archive on next launch without a re-upload.

## Building Torch for the browser

```bash
cd torch
emcmake cmake -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUSE_STANDALONE=ON -DBUILD_SSB64=ON -DBUILD_STORMLIB=OFF \
  -DBUILD_SM64=OFF -DBUILD_MK64=OFF -DBUILD_SF64=OFF -DBUILD_PM64=OFF \
  -DBUILD_FZERO=OFF -DBUILD_MARIO_ARTIST=OFF -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
cmake --build build-wasm -j
```

`scripts/package_web.sh` picks the module up from `torch/build-wasm` (override
with `TORCH_WASM_DIR`) and refuses to package without it unless
`PACKAGE_O2R=1`, which restores the old behaviour of shipping the archive in
the manifest for local harness runs that have no site to stage a ROM.

Gotchas found while porting:

- Torch relies on exceptions for control flow. Emscripten strips `catch`
  blocks unless `-sNO_DISABLE_EXCEPTION_CATCHING` is present at **compile**
  time; the upstream CMake only passed it at link time.
- `BUILD_SSB64` defaults off, and without it every reloc node fails with
  "No factory by the name 'SSB64:RELOC'".
- Output is deterministic in content: entry CRCs match the native build
  exactly; only zip timestamps differ.

## Port-stage select-screen art

Final Destination, Metal Cavern and Battlefield have no stage-select icon in
the ROM (they are 1P arenas there), so the port derives a wallpaper and a
48x36 thumbnail from each stage's own background sprite. Those PNGs are
ROM-derived, so they are no longer packaged either: the worker calls
`torch_derive_stage_assets` (`port/css_icons/stage_assets_derive.cpp`,
compiled into the Torch module by `scripts/build_torch_wasm.sh`) right after
the archive export, and the results ride along in the IndexedDB record as
`extras` and are written to `/assets/css_icons/` by `writeArchive`. Output is
pixel-identical to `tools/derive_stage_assets.py` and takes ~50 ms. Only the
script-rendered nameplates (`*_name.png`, plain text) still ship in the
package; the FD emblem PNG is dropped because `mnMapsMakeEmblem` does not
render it yet.

## Scheduling (why the archive is built on the site, not in the engine)

Torch itself is not the bottleneck; where it runs is. Measured in Chrome
with the US ROM:

| Situation | Extraction |
|---|---|
| Worker in a visible tab, nothing else running | 3.1–3.5 s |
| Same, while Chrome tier-compiles the 7.5 MB engine wasm | 12–43 s |
| Same, in a hidden/background tab (renderer priority lowered) | 17–51 s |

Two rules follow:

1. **The site prewarms.** `shared/rom-store.js#prewarmEngineArchive` runs
   right after `/api/validate-rom` succeeds, during the console/controller
   launch animation, so the engine finds the archive in IndexedDB and boots
   from cache.
2. **The engine never overlaps extraction with its own compile.** The
   shell resolves `window.__archivePromise` (cache lookup, or a build if the
   site did not prewarm) *before* inserting the `BattleShip.js` script tag.
   Serial is far cheaper than contended.

`archives` records carry a `timings` object (worker phases plus a coarse
log-line timeline, visibility and focus at build time) for diagnosing slow
builds in the field.

## Numbers (US ROM, 2026-09-01)

| | |
|---|---|
| Native Torch | 5.0 s |
| Torch wasm under Node (V8) | 4.3 s |
| Archive | 12.1 MB, 2159 entries |
| Torch wasm module | 1.65 MB |
