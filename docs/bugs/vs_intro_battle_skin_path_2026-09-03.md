# VS matchup card: injected meshes got the battle skinning path (skinny/disconnected arms)

**Date:** 2026-09-03  **Status:** fixed in `decomp/src/ft/ftport.c` (uncommitted pending live verification)

## Symptom
On the VS matchup card (scvsintro.c), Samus-targeted injected characters in the
raised-arm victory pose showed a pencil-thin upper arm, a dark "pants-textured"
blob at the elbow, a gap at the wrist and a thin off-hand arm. The same bundle in
battle and on the CSS card looked fine.

## Root cause
`osb5_on_menu_scene()` in ftport.c decides whether the canonical-retarget skinner
applies the *menu-pose* treatment (root-slot aim, one-child arm-segment aim along
the live joints, head up-vector aim, no leg dev-fit). That treatment exists because
CSS / results figatrees bind only part of the hierarchy, so the raw TBND rotation
deltas leave the chibi arm segments pointing away from the live joints.

The predicate listed scenes 16-20 (CSS variants) and 24 (VS results) only. The
matchup card runs under its own appended scene id `nSCKindVSIntro` (= 62), added
2026-09-02 to fix the OSB5 owner-liveness crash. It spawns fighters straight into
`nFTDemoStatusWin1..3` exactly like the results screen, but because 62 was not in
the predicate it got the *battle* path: no arm aim, so the arms deformed as if the
whole hierarchy were bound.

## Fix
Treat scene 62 as a menu scene. `SSB64_VSINTRO_BATTLE_SKIN=1` restores the old
behaviour for A/B.

## Verification (native, Metal capture)
Scratch `cap.py`: `SSB64_BOOT_BATTLE=3,0,4,1` + `SSB64_INJECT_BUNDLE=play/thomasdimson-samus.osb`
+ `SSB64_SCREENSHOT_FRAMES=50..110`. The card occupies roughly global frames 40-110
under `SSB64_MUTE=1`. Old vs new at frame 90: arms full-width and attached in the
new build. Fox- and Mario-target bundles render identically on both paths (no
regression). CSS card (scene 16, `SSB64_START_SCENE=16`) already looked correct.

## Pattern
Any new PORT scene that re-makes fighters into demo/Win statuses must be added to
`osb5_on_menu_scene()` (and reviewed against the `scd == 16 || scd == 24` debug
gate next to it). Grep `port_current_scene()` in ftport.c when adding scene ids.

## Part 2 (same day): residual notch + pants patch = leaked shoulder weights

After the scene fix the card still showed a jagged sleeve edge, a gap between
sleeve and torso, and a pants-coloured patch at mid-torso on the raised-arm side.

Diagnosis via a new engine dump: `SSB64_SKIN_DUMP=<scene>:<nth>` logs every
slot frame (`SKJ`) and every skinned vert in world space (`SKV`) on the nth skin
update of player 0 in that scene; scratch `torn.py` pairs that with the bundle's
triangles and ranks edges by live/bind length ratio. The worst edges (13x) were
all torso verts at WAIST height on the raised-arm side carrying 5-30% weight on
the SHOULDER slot next to chest+hip weight. The rest pose has the arm hanging
beside the body, so the auto-rig / diffusion leaks shoulder influence down the
torso; harmless in battle swings, but the ~150deg Win-pose shoulder rotation
turns 20% of that lever into ~20 units between neighbouring verts.

The menu-only `arm_weight >= 128 -> w*w` concentration hack was also made a
continuous ramp (exponent 1 -> 2 over arm_weight 64..192; `SSB64_ARMSQ=0/1`
for off/old) — it was a weight cliff too, though not the visible one.

Fix: at inject, `o->wdamp[i][t]` (u8) per influence. For torso-dominant verts
(arm weight sum < 128) an arm-slot influence is kept only near the ROOT end of
its own bone: radial keep 1 within 0.35 bone lengths (gone by 0.7) x along-bone
keep 1 up to t=0.35 (gone by 0.8), smoothstepped. Applied on menu scenes only
(`SSB64_ARMLEAK=0` disables). Worst edge 13x -> 3.8x (a 2-unit foot edge);
sleeve joins the torso, pants patch gone; CSS card also cleaner; Fox/Mario
cards unchanged. Data-side alternative (not done): prune the same influences in
convert_rigged.py, which needs a roster regen.
