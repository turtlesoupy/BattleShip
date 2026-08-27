# CSS preview keeps the previous character's mesh when two roster picks share a base fighter

**Date:** 2026-08-27
**Status:** RESOLVED
**Files:** `decomp/src/mn/mnplayers/mnplayersvs.c`, `decomp/src/ft/ftport.c`

## Symptom

On the roster character select, moving a token from one custom character to
another updated the card NAME and emblem but **not the 3D preview mesh** — the
first-picked character's mesh stayed on the card. The stale mesh then rode into
the match: selecting JFLYNN spawned Moritz's mesh in gameplay.

Only reproduced when two roster characters resolve to the **same base fighter**,
i.e. under base retargeting:

```
?roster=1&SSB64_START_SCENE=16&SSB64_ROSTER_PAGE=1&base=moritzbaierlentz:mario,joeyflynn:mario,...
```

With no `base=` (each character on its own tile fkind) it never appeared, which
is why normal roster use never hit it.

## Root cause

`mnPlayersVSSetupFighter`'s reuse gate decided whether to reuse the live preview
GObj or destroy-and-remake it by comparing **spawn fkinds only**:

```c
if (fighter_gobj != NULL &&
    ftGetStruct(fighter_gobj)->fkind == port_roster_tile_spawn_fkind(slot.fkind))
    { /* reuse: costume + position only */ }
else
    mnPlayersVSMakeFighter(...);   /* the ONLY path that re-runs injection */
```

`port_inject_bundle()` runs from `ftManagerMakeFighter`, so the reuse branch
never re-injects. Two characters that declare the same base fighter spawn as the
same fkind, so the gate saw "no change", reused, and left the previous
character's OSB5 mesh attached. The mesh slot stayed owned by the outgoing
character, so the match inherited it.

The fkind comparison was itself a prior fix (comparing the raw tile made the
preview re-make every frame). It was right about fkind and incomplete about
identity: preview identity is *base fkind AND which character*, not base fkind
alone.

## Fix

`OSB5State` gains `owner_char` — the registry index the slot is wearing —
recorded where the slot is claimed in `osb5_load()`, carried from
`port_inject_bundle()`'s `port_char_for_player()` resolution via
`sInjectCharIdx`. New predicate `port_roster_preview_char_matches()` answers
whether a player's preview GObj already wears the character the tile resolves
to, and the menu gate requires it in addition to the fkind match.

The predicate deliberately reads the **tile** binding (`sTileChar[]`), not
`sPlayerChar[]`: the gate runs before `mnPlayersVSMakeFighter` calls
`port_roster_bind_player`, so the player binding still holds the OUTGOING pick
at gate time.

Also fixes vanilla<->custom swaps on a single fkind (page-0 Mario vs a page-1
character sitting on the Mario tile), which had the same hole.

## Verification

Rebuilt wasm, drove the CSS with synthetic input and captured frames via
`window.captureFrame()` -> POST `/upload`:

- Place 1P on MORITZ, move one tile, place on JFLYNN: card reads JFLYNN and the
  mesh is Joey (glasses, beard, white tee, jeans) on the Mario skeleton.
  Before the fix the same frame showed Moritz.
- Start the match from that pick: 1P spawns as Joey, not Moritz.
- Control: `base=moritzbaierlentz:mario` alone (bases differ) switched correctly
  both before and after, confirming the shared-base trigger.
