# SSB64_BOOT_BATTLE Zeroed All Knockback (2026-08-26) - FIXED

## Symptom

In any match started via `SSB64_BOOT_BATTLE` — the direct-boot path *and* the
`SSB64_START_SCENE` CSS-preset demo path — fighters accumulated damage normally
but were never launched. Percentages climbed to the 999 cap and stayed there;
players could only die by falling off, not by being knocked off. Attacks
connected, dealt damage, and produced hit effects, so nothing looked broken in
a screenshot.

Matches started through the real menus were unaffected, which is why this
survived weeks of harness runs without being isolated.

## Root Cause

`scmanager.c`'s boot-battle block set:

```c
gSCManagerTransferBattleState.damage_ratio = 2;   /* 100% */
```

`damage_ratio` is a **literal percent**, not a menu index. `sctypes.h` documents
the domain as "From 50% to 200%"; the VS options menu wraps the value from 50 to
200 in steps of 1 and assigns it straight through (`mnvsoptions.c`), and every
other writer in the tree uses `100` (`sc1pmanager.c`, `netpeer.c`, and
`dSCManagerDefaultBattleState`).

It feeds `ftParamGetCommonKnockback` as a direct multiplier:

```c
knockback = ( ... ) * ( gSCManagerBattleState->damage_ratio * 0.01F ) * handicaps;
```

So `2` meant **every hit in the game landed at 2% knockback**.

This is not a "weak moves stop killing" bug — it is total. Launch (damage level
3, the tumble state) requires hitstun >= 32, i.e. knockback >= 60. The strongest
hit in the game against the lightest character produces raw knockback around
2400 at the 999% cap; at x0.02 that is ~48. **No move could reach the launch
threshold at any percent.** For scale, Mario's jab 1 at 999% computes to 157
correct, 3.1 as shipped.

The 2500 knockback cap in `ftParamGetCommonKnockback` is applied *after* the
ratio multiply, so it never bound and never masked the scale error.

Introduced 2026-08-07 in `31d204fe2` ("pipeline: SSB64_BOOT_BATTLE direct-boot
+ skeleton dump + VS handoff diagnostics"). Port code only; nothing upstream.

## Deeper Cause

`SSB64_BOOT_BATTLE` was a **second implementation** of battle-state
construction, hand-writing thirteen fields that the menu path already derives.
Duplicated derivation drifts and duplicated units get mistranslated; both
happened. A second, quieter instance of the same drift: `is_single_stockicon`
was never set at all, so a stock match ran with the time-mode default from
`dSCManagerDefaultBattleState` (the real path computes it from the TIME rule bit
at `mnplayersvs.c:5010`).

## Fix

Two layers.

**1. Single owner for derivation.** The boot block now expresses the preset as
character-select and VS-options *inputs* (`sMNPlayersVSSlots[]`,
`sMNPlayersVSStockValue`, `sMNVSOptionsDamage`, ...) and commits through the same
two functions the menus use:

- `mnPlayersVSSetSceneData()` — rules, stocks, and every derived per-player
  field: `player`, `color`, `tag`, `is_single_stockicon`, `level`, `handicap`,
  plus `pl_count`/`cp_count`.
- `mnVSOptionsSetAllSettings()` — handicap mode, team attack, stage select,
  `damage_ratio`, and the per-player handicap normalization to
  `FTCOMMON_HANDICAP_DEFAULT` when handicap is Off.

Order matters: `SetAllSettings()` must run after `SetSceneData()` so the
handicap normalization lands on rows that already exist.

Nothing in the block derives a battle-state field by hand any more. Fields the
two commit functions do not own (`game_type`, `gkind`, `item_toggles`,
`item_appearance_rate`, `stock_count`) follow as an explicit, commented delta
list, as do the two intentional demo deviations — per-port HUD `color` for CPUs
(vanilla gives every CPU the shared grey CP color, which makes a four-way
injection demo unreadable) and the forced CP `tag` in two-human eval mode.

**2. Domain validation for whatever is still assigned directly.**
`portValidateBattleState()` runs at VSBattle entry — after the boot preset, the
CSS handoff, and the competitive-ruleset override have all had their say — and
logs loudly on any out-of-domain field: `damage_ratio` 50..200, `stocks` 0..4,
`handicap`, `item_appearance_rate`, per-player `level` 1..9 and `tag`.

Per-player `handicap` is the one case that is **clamped** rather than only
reported: `ftParamGetCommonKnockback` indexes
`dFTCommonDataHandicapTable[handicap - 1]` with no bound check, so `0` is an
out-of-bounds read, not merely a wrong multiplier. The table's row count is
region-dependent and its extern declaration has an unspecified bound, so
`dFTCommonDataHandicapTableCount` is now exported from the definition site in
`ftcommondata.c`.

## Audit Hook

Structural reuse of a single writer prevents *derivation* drift but cannot catch
a *unit* mistake in a field still assigned directly. Both defenses are needed.

The class to watch for: a field whose type admits the wrong value silently
(`u8 damage_ratio = 2` is perfectly legal) and that feeds a **continuous
multiplier into physics** rather than an enum, count, or flag. Enums and counts
fail loudly — wrong stage, wrong stock count, match ends early — and get caught
in one run. Multipliers just change how the game feels, and nobody diffs feel.
In `SCBattleState` there are exactly two such fields: `damage_ratio` and
per-player `handicap`.

A latent sibling found in the same audit and fixed alongside it: the
`port_get_comp_ruleset()` block set `item_appearance_rate = 4; // None`, but `4`
is `nSCBattleItemSwitchHigh` — `nSCBattleItemSwitchNone` is `0`. It was inert,
because the same block sets `item_toggles = 0` and leaves nothing to spawn.
That masking is the point: it would have survived untouched until someone
enabled items alongside the comp ruleset and got High spawn rates out of a line
reading "None". Now uses the named constant.

## Verification

- WASM build clean.
- Runtime log now reports `BOOT_BATTLE override -> ... damage_ratio=100`.
- `portValidateBattleState` produced no warnings across a full boot.
- Live play confirmed by the user: knockback and KOs behave normally again.

## Files

- `decomp/src/sc/scmanager.c` — boot-battle block rewritten to commit through
  the menu writers; `portValidateBattleState()` added and called at VSBattle
  entry.
- `decomp/src/ft/ftcommondata.c` / `.h` — export
  `dFTCommonDataHandicapTableCount` for the handicap bound check.
