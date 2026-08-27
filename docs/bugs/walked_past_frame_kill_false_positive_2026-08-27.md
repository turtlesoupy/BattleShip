# Walked-past frame-kill false positive (heap-layout-dependent missing meshes)

**Date:** 2026-08-27
**Status:** FIXED (browser-verified on the wasm attract intro; native shares the path)
**Files:** `port/port_dl_ranges.cpp`, `libultraship/src/fast/interpreter.{h,cpp}`, plus two sibling fixes: wasm unresolved-pointer guards (`interpreter.cpp`), OSB5 slot eviction (`decomp/src/ft/ftport.c`)

## Symptom

Intermittently, some meshes simply did not render — no crash, no repro. Most
visibly in the opening movie: usually at least one character per boot (Mario's
arms only, or Kirby / Pikachu / DK entirely absent), with the missing set
varying between boots but staying consistent within one session. Stage select
occasionally lost most of the screen while a few meshes still drew.

## Root cause

`port_dl_check_addr` classified **any** address that was (a) not inside any
registered DL range and (b) within the 64KB `kWalkPastWindow` past the end of
**any** registered range as `PORT_DL_WALKED_PAST`. `gfx_step` answers that
verdict with `g_exec_stack.stop()` — deliberately killing the whole frame's
remaining walk, because a genuine runaway may have corrupted parent frames.

But the registry (reloc file copies, scene arena, mod ranges) shares the game
heap with ordinary allocations, and two families of perfectly valid DLs are
plain unregistered mallocs:

- widened packed-DL copies built by `portNormalizeDisplayListPointer`
  (`std::vector` storage), heavily used by menu/movie scenes, and
- OSB5 injected-mesh DLs (`osb5_load`, per-part builders in `ftport.c`).

Whenever malloc happened to place one of these inside some unrelated range's
64KB shadow, every walk through it was condemned as a runaway and the frame
died at that exact draw — every frame, for as long as the allocation lived
(the widening cache pins copies until range eviction). Draw order decided the
victims: everything submitted after the condemned DL vanished. Per-boot heap
layout decided which DL was cursed → "different characters missing each boot,
same ones all session."

The `GFX STALE-DL DIAG` dump (PR #133 instrumentation) fired at ~frame 1550 of
the attract intro on consecutive boots, with the exec-stack top inside an
unclassified heap address (a widened copy) called from `scene_arena` — and the
`walked past` WARN self-caps at 10 lines, so long sessions showed nothing.

## Fix

Two layers:

1. **Correct semantics** (`port_dl_check_addr`): "walked past" now requires
   that the thread's *last in-range hit* — the range this walk was actually
   inside, already tracked by the hot-path `HitCache` — ends just before the
   address. The shadow of unrelated ranges no longer condemns anything. A
   genuine missing-terminator walk-off (variant 5) still steps from its range
   into that same range's shadow, so real detection is preserved.
2. **Register widened copies** (`Fast::RegisterDLRangeHooks`, new): the
   interpreter now reports each widened copy's exact extent to the game's
   registry ("widened-dl"), and unregisters on cache reset / range eviction.
   Besides eliminating the false positive at the source, in-range hits let the
   walker's single-entry cache answer per-command checks without the locked
   linear scan every widened-DL command previously paid.

## Sibling fixes landed together

- **wasm unresolved-pointer guards**: `gfx_vtx_addr_is_unresolved` and the
  SETTIMG guard treated any pointer < 0x10000000 as an unresolved segment
  token on 32-bit targets; wasm's 512MB linear memory straddles that line, so
  legitimately low-malloc'd Vtx/texture buffers were silently dropped. The
  `__EMSCRIPTEN__` arm now rejects only null (no SIGSEGV risk on wasm).
- **OSB5 fail-open slot eviction**: screens spawning several fighters under
  one player index (1P intro uses `desc.player = 0` for the whole lineup) let
  a second injected fighter re-claim the per-player mesh slot while the first
  still lived, stranding it blanked + `GOBJ_FLAG_HIDDEN` forever. Blanking now
  saves the engine's per-joint draw pointers; `osb5_release_owner` restores
  the displaced fighter to its vanilla mesh and reveals it before re-claim.

## Verification

Pre-fix: two consecutive wasm boots dumped the STALE-DL diag at ~frame 1550
with intro characters missing (user-observed on separate Chrome sessions:
Mario/Kirby, then Mario/DK, then Mario-arms/Kirby/Pikachu). Post-fix: a full
attract intro with a console hook recorded zero diag/walked-past/unresolved
lines and every vignette rendered (Pikachu, all five Yoshis). Heap layout
varies per boot, so continued soak on stage select and injected-character
matches is the ongoing gate.
