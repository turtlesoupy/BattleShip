# OSB5 stale owner shreds vanilla CSS previews (2026-08-25)

## Symptom

With a custom-character bundle injected, the character-select screen's 3D
card previews for OTHER (vanilla) fighters rendered shredded — DK reduced
to his tie and hands, Fox to a boot fragment. In-game fighters were fine;
only preview/demo fighters spawned after the injected fighter despawned
were affected.

## Root cause

`sOsb5.owner` identified the injected fighter by bare `GObj *`. GObjs are
pool-allocated: when the owner despawns (match end, CSS chip moved off the
slot), the next fighter spawned — e.g. the DK preview you hover next — can
reuse the same address. The pointer comparison in `port_osb5_skin_update`
(per-tick self-heal re-blanking) and `port_osb5_joint_replaced` (modelpart
guard) then matched, and the queen's blank list was applied to a vanilla
DK: mapped joints blanked, no replacement mesh attached (attach only fires
for the injected fkind), leaving just the unmapped accessory joints (tie,
tail) drawing.

Reproduced deterministically on the VS CSS: drop the 1P chip on the
injected slot (attach), then move it to DK — DK's preview shreds.

## Fix

Ownership also records the fighter kind (`sOsb5.owner_fkind`, set at
attach). Every consumer of `owner` additionally requires
`fp->fkind == owner_fkind`. A pool-reused address with a different fkind
no longer matches; a reused address with the SAME fkind is refreshed by
the attach that fires for every spawn of the injected kind.

`decomp/src/ft/ftport.c`: `OSB5State.owner_fkind`, checks in
`port_osb5_skin_update` and `port_osb5_joint_replaced`.
