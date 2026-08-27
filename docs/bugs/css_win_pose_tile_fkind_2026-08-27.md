# CSS selection Win pose keyed by tile fkind freezes base-retargeted previews mid-animation

**Date:** 2026-08-27
**Status:** RESOLVED
**Files:** `decomp/src/mn/mnplayers/mnplayersvs.c`

## Symptom

Dropping the token on a base-retargeted roster character left the card fighter
frozen mid-animation (dangling airborne pose) instead of finishing in the
selection victory stance. Hover was fine; only actual selection triggered it.
Repro: all-Mario `?base=` roster URL, drop the pin on MORITZ (Samus tile,
Mario base).

## Root cause

On selection, `mnPlayersVSFighterProcUpdate` sets a per-fighter victory status:

```c
scSubsysFighterSetStatus(..., mnPlayersVSGetStatusSelected(sMNPlayersVSSlots[player].fkind));
```

`mnPlayersVSGetStatusSelected` maps fighter kind -> Win variant (Fox/Samus=Win4,
Mario/Kirby=Win3, ...), and `sMNPlayersVSSlots[].fkind` is the TILE fkind. A
base-retargeted preview spawns as the BASE fighter, so the fighter was commanded
another kind's Win variant — Moritz (Samus tile, Mario base) got Samus's Win4,
which Mario's motion data never completes, freezing the pose. Only visible when
tile and base map to different Win variants (Queen: Luigi tile/Luigi-mapped
Win1 -> unaffected; Moritz: Win4 vs Win3 -> frozen).

## Fix

Compute `sel_fkind = port_roster_tile_spawn_fkind(slot.fkind)` once in
`mnPlayersVSFighterProcUpdate` and pass it to both `mnPlayersVSGetStatusSelected`
calls — the Win pose now matches the fighter actually standing on the card.
Vanilla tiles pass through unchanged; both call sites (player picks and preset
CPUs) funnel through this one function.

## Verification

Rebuilt wasm; drove the repro (drop pin on MORITZ under the all-Mario base URL).
At +10s and +16s the card fighter is grounded in a settled victory stance with
the animation progressing between frames — previously frozen airborne.
Same family as css_preview_shared_base_stale_mesh_2026-08-27: CSS code that keys
per-fighter data by tile fkind breaks under base retargeting.
