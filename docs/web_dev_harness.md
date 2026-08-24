# WASM Dev Harness (2026-08-24)

Browser-based iteration loop for the OpenSmash pipeline — same env-hook
contract as the native build, driven by URL instead of shell env.

## Serve

`.claude/launch.json` (repo root) has `battleship-wasm`: runs
`scripts/web_dev_server.py 8600 web-dist` — static serving of `web-dist/`
plus `POST /upload` (data-URL or raw body -> `web-dist/captures/<X-Capture-Name>.png|webm`).

Rebuild + repackage after C changes:

    ninja -C build-wasm BattleShip.js -j 4       # target is BattleShip.js, not BattleShip
    ./scripts/package_web.sh build-wasm web-dist
    cp ../pipeline/play/*.osb web-dist/bundles/

## URL contract (web/index.html)

- Any `SSB64_*` query param becomes an environment variable before main().
- `?inject=bundles/queen.osb[&fkind=0][&player=0]` fetches the bundle into
  MEMFS and sets `SSB64_INJECT_BUNDLE` (+FKIND/PLAYER).
- Example: `http://localhost:8600/index.html?inject=bundles/queen.osb&SSB64_BOOT_BATTLE=0,8,4`

JS helpers on the page: `captureFrame()` (PNG data-URL of the canvas — works
in hidden tabs), `startClip(ms)` (MediaRecorder WebM — needs a visible tab),
`pressKey(code, ms)` (synthetic key hold; SDL listens on the canvas).

## Aspect ratio / sizing (proper N64 4:3)

- The SDL window is NOT resizable on Emscripten (gfx_sdl2.cpp): SDL's browser
  driver otherwise resizes the canvas to the browser window on every resize —
  including 0x0 for hidden tabs — stretching the game and killing headless
  rendering. The render buffer is fixed at the configured size.
- `web/files-extra/BattleShip.cfg.json` ships `Window: 1280x960` (4:3); the
  shell CSS letterboxes the displayed element at 4:3. MEMFS discards the
  game-written config each load, so this file IS the config.
- Belt-and-braces in gfx_sdl2.cpp SIZE_CHANGED (fires on fullscreen): drawable
  sizes are clamped to the largest 4:3 fit and 0-size events are ignored.

## Gotchas

- The shell's async preRun MUST call `addRunDependency` before its first
  `await`, or Emscripten boots the game with an empty FS (first-run ROM
  wizard appears). Bug shipped and fixed 2026-08-24.
- `Module.webglContextAttributes` does not reach SDL's EGL context —
  `preserveDrawingBuffer` stays false. `toDataURL` still works because the
  buffer is only cleared on composite; in hidden tabs there is no composite,
  and in visible tabs capture right after a frame is close enough in practice.
- First-run asset extraction needs no ROM in the browser: `BattleShip.o2r` is
  baked at build time and `package_web.sh` writes the recipe sidecar.
