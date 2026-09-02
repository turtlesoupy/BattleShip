# Generated Item MVP

## Decision

The first generated-item feature is a **cosmetic Home-Run Bat override**, not
a new item kind. A generated low-poly GLB is converted to one opaque OSB2
vertex-color display list or one textured-lit OSB4 display list and substituted
for the bat's vanilla DObj geometry at spawn.
The item remains `nITKindBat` (`8`) everywhere else.

This boundary proves the valuable part of the idea—generated objects in a
match—without adding item behavior, combat balance, AI rules, item-switch UI,
spawn weights, sound design, or network-visible item IDs.

## Behavior contract

The override may change only rendering data. These remain vanilla bat data:

- pickup, hold, swing, throw, drop, fall, spin, rebound, and despawn states;
- fighter bat-swing animations and animation-script hitboxes;
- damage, knockback, collision bounds, sounds, AI recognition, and item kind;
- item-switch and random-spawn behavior.

If the override is missing or invalid, the vanilla bat renders normally. The
runtime loads and validates the override once, then caches its process-lifetime
display list for subsequent bat spawns.

While the bat is held, the port mirrors generated child geometry around the
attachment origin when the fighter changes facing. The stock bat does not make
this engine detail obvious, but without the correction an asymmetric prop would
present a different end to the fighter's hand in each direction.

## Asset contract

Input is an embedded GLB containing exactly one mesh with one triangle-list
primitive. The MVP rejects sparse accessors, external textures, multi-mesh
files, and models over 2,000 triangles. Convert it with:

```sh
python3 pipeline/pipeline/convert_item.py generated.glb generated-bat.osb \
  --axis y --length 420 --hold-point auto
```

`--axis` identifies the source model's long axis. `--target-axis` chooses the
item-local axis it maps onto; bat-like props should use `--target-axis x`
because the stock bat's shaft runs along local X. Use `--flip` when its tip
points the wrong way.

The default `--hold-point auto` compares robust cross-sections at both ends,
chooses the narrower end as the handle, finds that end's full 3D center, and
insets the grip slightly so the glove wraps around it. This matters for clubs,
hammers, and axes: centering the other two axes from the complete bounding box
lets a bulky head pull the shaft away from the hand. Use `--hold-inset` to tune
how far the glove sits from the cap.

The runtime then maps that zero point onto the bat archetype's calibrated hand
anchor. It preserves the stock drawable transform and adds the small visual
center correction shared by every generated bat-skin; this engine-side offset
must not be compensated for in each asset.

Geometry cannot reliably identify every semantic handle. For ambiguous props,
author the grip directly with `--hold-position X Y Z`, expressed in the source
GLB's coordinates. `--hold-point base|center|tip` is available for simple
manual placement, and the old `--pivot` spelling remains compatible. The
`--offset-x`, `--offset-y`, and `--offset-z` options are final game-space nudges,
not a substitute for an asset-local 3D grip point.

The existing provider front-end can produce the GLB; keep the request near the
runtime budget so conversion does not have to discard geometry:

```sh
python3 pipeline/pipeline/gen.py text3d \
  "low-poly toy mallet, single object, clean silhouette, game prop" \
  --polycount 1200
python3 pipeline/pipeline/gen.py status <task-id> --kind text
python3 pipeline/pipeline/gen.py download <task-id> generated.glb --kind text
```

Image-to-3D and Tripo outputs work too when exported as a single embedded GLB.
Provider calls are intentionally separate from conversion so submitting a paid
generation job remains an explicit action.

The default output is a one-part OSB2 bundle. Texture/base colors are sampled
into vertex colors, which is appropriate only for flat or untextured props.
For provider-textured assets use `--shading textured-lit`: the converter emits
OSB4, guided-flattens baked provider lighting, dilates UV gutters before making
a small RGBA16 dithered atlas, recomputes smooth normals across UV seams, and
uses neutral ambient/key lighting in game. `--texture-size 64` is the default
and is generally enough for an N64-style item.

## Running it

Native builds:

```sh
SSB64_INJECT_ITEM_BAT=/absolute/path/generated-bat.osb \
SSB64_FORCE_ITEM_KIND=8 \
./BattleShip
```

Web builds stage the bundle into MEMFS:

```text
?inject_item_bat=bundles/generated-bat.osb&SSB64_FORCE_ITEM_KIND=8
```

`SSB64_FORCE_ITEM_KIND=8` is only a test convenience. Without it, every bat
that naturally spawns uses the override.

## Fast iteration lab

Use the item lab instead of waiting for a random spawn. It converts the GLB,
boots a deterministic match, creates exactly one replacement bat, and writes
a screenshot in roughly ten seconds:

```sh
python3 pipeline/pipeline/preview_item.py generated.glb artifacts/item-preview \
  --axis x --target-axis x --roll 0 --length 420 --hold-point auto --mode hold \
  --shading textured-lit --texture-size 64
```

`--mode hold` starts the item attached to P1, which makes grip, orientation,
and clipping errors obvious. `--mode ground` drops it beside P1 to inspect its
free-standing pose. Change `--axis`, `--roll`, `--flip`, `--length`, the
`--hold-*` values, or any `--offset-*` value and rerun the same command;
asset-only iterations do not require rebuilding the game.

Add `--action attack --frame 505` to tap A just before capture and verify that
the generated mesh follows the real held-item swing animation.

The underlying runtime hook is `SSB64_ITEM_PREVIEW=hold|ground`. It works even
when random items are disabled and has no effect when unset.

## MVP acceptance criteria

1. With no override configured, the vanilla bat is unchanged.
2. A valid one-part OSB2 or OSB4 bundle replaces the bat model on native and web.
3. The replacement can be picked up, swung, thrown, dropped, and respawned.
4. Damage, knockback, fighter animation, and AI behavior remain bat behavior.
5. A missing, malformed, dense, or unsupported asset fails safely and logs a
   concise `ITEMOSB` message.
6. An asymmetric override keeps the same authored grip against the fighter's
   hand when the fighter turns left or right while holding it.

## Expansion path

The next useful abstraction is an item-skin registry mapping a cosmetic slug
to a vanilla behavior archetype (`bat`, `sword`, `fan`, `container`, or
`heal`). It should keep gameplay IDs vanilla while allowing several cosmetic
models to be selected before a match. Only after that proves limiting should
we allocate new item kinds.

Later format work can build on the shared OSB4 textured-lighting treatment and
embed authoring metadata when an editor needs to move the grip without
reconverting geometry. Projectiles, bombs, guns, shells, Pokémon, and custom
effects remain a separate behavior-authoring tier rather than part of the
cosmetic pipeline.
