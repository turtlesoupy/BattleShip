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

## Part 3 (same day): generalized — no longer menu-only, plus shoulder lift

Measured over the Samus battle eval tour (eval/streams/fk3.json): the upper arm
is above horizontal in ~6% of frames and reaches vertical (up-tilt/up-smash), so
the leak tear was a battle bug too, just never frozen on. And the weights are the
canonical Mario build shared by every target, so it was never Samus-specific.

Changes (decomp ftport.c, pipeline convert_rigged.py), all env-escapable:
- `wdamp` leak damping now runs in ALL scenes (`SSB64_ARMLEAK=0` off).
- Shoulder lift, all scenes: an arm-root slot whose bone is raised past ~60deg
  from hanging moves its origin up 0.28 / inward 0.10 bone lengths at vertical
  (smoothstep 60..150deg); children inherit; torso verts follow through an
  inject-time `shprox` proximity table x (1 - arm weight). `SSB64_SHLIFT=<scale>`,
  0 disables. Fixes the "arm flies up, nothing around it moves" read.
- Converter: `arm-leak prune` pass after post-claim smoothing, same geometric
  rule (torso-dominant verts keep arm influence only near the root end of its
  bone). `--no-armprune` disables. Re-cut thomasdimson-samus: engine reports
  "0 leaked arm influences damped", tear metric identical to the damped path.
  Shipped roster not regenerated — the engine damping covers it.
- Canonical-only: Mario-base bundles are not canonical retargets, so neither
  damping nor lift applies there (pixel-identical before/after on the fk0 tour).

Verified: VS card (lift on/off), CSS, battle tour frames 982/990/1210/1218/545
(up-tilt, up-smash, run) samus target old vs new — no tears, shoulder follows the
cannon arm, run unchanged.

## Part 4 (same day): "arm glitching up and down" on the card = Win1-3 loop snap

User saw the raised arm jump every ~quarter second on the card. Frame-to-frame
pixel diffs of a native every-frame capture showed 2-frame spikes at frames 83,
99, 115 (period 16) — present with the lift and damping OFF, on the OLD battle
skin path, and on VANILLA Samus, so not a skinning bug. Hooking
`port_dump_frame` into scVSIntroFighterProcUpdate (after
ftParamsUpdateFighterPartsTransformAll, or the cached part matrices read
constant) showed the cannon joint easing over 16 tics then snapping ~80 units
back: the generic Win1..3 results poses loop a tail segment with a
discontinuous restart.

Fix: the card now uses `mnPlayersVSGetStatusSelected(fkind)` — the per-kind
"selected" pose the CSS card uses (Samus/Fox Win4, Mario/Kirby Win3, ...),
authored to hold. Verified every frame 60-130 on samus/fox/mario targets: no
periodic spikes, only continuous animation. Side effect: the card pose is now
deterministic and matches the CSS card.

Tooling kept: SSB64_DUMP_FRAMES now also works on the card scene.

## Part 5 (same day): "left arm smaller than right" on the held Samus pose

Measured from SKIN_DUMP world verts: upper-arm radius about the shoulder->elbow
axis is symmetric (18.1 vs 18.3). What differs is the FREE arm's forearm+hand
(bent to the chin in Win4): 23.9 with the menu arm-weight squaring ramp vs 17.8
with it off, cannon side ~17 either way. The ramp (originally "concentrate the
broad source arm weights in sharply bent CSS poses") fattened the bent free
forearm ~35% and the raised cannon arm read small by contrast. Note also the
cannon covers the hand, so that arm always reads shorter — same as vanilla.

Now that leaked weights are pruned the ramp has no job left: default
SSB64_ARMSQ=0 (2 = ramp, 1 = old hard cutoff). CSS samus/fox/luigi and the
card checked ramp on vs off: no rubber-hose regression, slimmer bent forearms.

Measurement gotcha: measure limb radius about that limb's OWN bone axis
(shoulder->elbow); a shoulder->hand axis inflates the number whenever the elbow
is bent and produced a false 2x asymmetry first.

## Blind A/B (user-rated, 2026-09-03, 45 pairs)
15 cells (thomasdimson/barackobama/elonmusk x samus/fox/luigi/link/mario-base),
configs from ONE binary via env: before (ARMLEAK=0 SHLIFT=0 ARMSQ=2), after
(defaults), dqs (after + SSB64_DQS=1). Clips = VS card + CSS + 29 tour poses,
per-frame union crop shared across configs. Scratch: vs/sweep/{sweep,blind,sheets}.py.

  after vs before: after 3 (all samus), tie 10, before 2 (luigi — clips differ
                   by <0.1% of pixels there; noise)
  after vs dqs:    after 8, dqs 4 (fox 2-1, luigi 2-1 for dqs; samus 3-0 after)
  before vs dqs:   before 6, dqs 5, tie 4

Decision: ship "after" defaults; DQS stays opt-in (SSB64_DQS=1) — it fixes the
pinched sleeve caps but its bent-elbow bulge loses overall. Mario-base pairs
tied as expected (control).
