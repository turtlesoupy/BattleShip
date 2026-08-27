# Metal: drawable-texture over-release crashes `nextDrawable` (parallel-launch SIGSEGV)

**Date:** 2026-08-27
**Status:** FIXED (libultraship working tree; pending live-play verification)
**Files:** `libultraship/src/fast/backends/gfx_metal.cpp` (`SetupScreenFramebuffer`, `EndFrame`, `ResolveMSAAColorBuffer`)

## Symptoms

Intermittent `EXC_BAD_ACCESS` inside `-[CAMetalLayer nextDrawable]`:

```
objc_msgSend
layer_composited(_CAImageQueue*, ...)
CAImageQueueDidComposite
release_images(_CAImageQueue*, CAImageQueueReleased*)
CAImageQueueCollect_
CAMetalLayerPrivateNextDrawableLocked
-[CAMetalLayer nextDrawable]
Fast::GfxRenderingAPIMetal::SetupScreenFramebuffer
Fast::GfxRenderingAPIMetal::NewFrame
```

A sibling failure mode hangs forever in the same call (`semaphore_timedwait` under
`CAMetalLayerPrivateNextDrawableLocked`). Rare for a single instance; launching
several instances simultaneously (the eval harness's parallel capture workers,
`pipeline/eval/ab_smoothing.py`) reproduced it reliably — the harness worked
around it with ~8s launch stagger + retry.

## Root cause

Upstream `SetupScreenFramebuffer` (inherited code, `ab9935c0f`) stores the
drawable's texture and releases it the following frame:

```cpp
if (tex.texture != nullptr)
    tex.texture->release();      // over-release: we never owned this
tex.texture = mCurrentDrawable->texture();   // borrowed +0 getter
```

`CAMetalDrawable.texture` is a plain getter — a borrowed +0 reference owned by
the layer's `CAImageQueue`. Releasing it without a matching retain frees the
queue's image out from under it. The corpse is only touched when the queue's
collect pass runs during a later `nextDrawable` (`CAImageQueueCollect →
release_images → objc_msgSend` on the freed object), so the crash is
timing-dependent: it needs the compositor to still hold the image when collect
fires. Several instances booting at once load WindowServer enough to make that
window wide open — hence "reliable under parallel launch, rare alone". The
capture path's `framebufferOnly=false` (backbuffer PNG screenshots) changes the
drawable's IOSurface configuration but was not the cause; the over-release is
reachable in stock upstream too.

## Fix

1. `tex.texture->retain()` after fetching `mCurrentDrawable->texture()`,
   balancing the per-frame release (and `DestroyFramebuffer` / shutdown
   releases, which release `mTextures[...]` uniformly).
2. Nil-drawable hardening: `nextDrawable` legitimately returns nil after its
   ~1s timeout when the drawable pool is starved (exactly the parallel-boot
   scenario). Previously that was an unconditional null deref. Now: bounded
   retry, then skip the frame — keep last frame's (retained) texture/render
   pass, guard `presentDrawable` in `EndFrame` and the drawable use in
   `ResolveMSAAColorBuffer`.

## Verification

Stress test: 5 instances booted simultaneously (no stagger), 20s each.

- **Pre-fix control build:** 1–2 of 5 instances dead within 10s in 3 of 4
  rounds; fresh `.ips` reports with the exact `CAImageQueueCollect` stack.
- **Fixed build:** 8 rounds × 5 boots = 40 simultaneous boots, 5/5 alive every
  round, zero new crash reports.
- Capture regression: `capture_clip.py` vanilla pose capture and an OSB5
  injected-character capture both produced correct PNGs/clips (the
  `framebufferOnly=false` blit path is unchanged).

The eval harness's launch stagger + crash retry in `ab_smoothing.py` /
`capture_clip.py` callers can be removed once this soaks.

## Audit hook

Any metal-cpp `->release()` on a pointer obtained from a getter (not `new*` /
`alloc` / `copy` / explicit `retain()`) is an over-release. The same
`mTextures[...].texture` slot mixes owned (`newTexture`) and, before this fix,
borrowed (drawable) references — uniform release loops over such slots require
uniform ownership.

## Addendum (same day): starvation silently killed screenshot capture

The nil-drawable hardening above surfaced a second bug: the backbuffer
capture (`portMetalStageCapturePNG` fulfillment in `EndFrame`) blitted from
`mCurrentDrawable->texture()` and was gated on the drawable. Under sustained
starvation (occluded/offscreen windows during parallel eval capture) presents
are skipped for the rest of the run — and every capture was silently dropped,
yielding runs that exit cleanly with few or no screenshots (the eval harness
saw 5/10 truncated cells in one heavy round; two encoded as short clips that
passed the old success check).

Fix (`libultraship e5bbab47`): blit from the screen framebuffer's retained
texture (`mTextures[mFramebuffers[0].mTextureId].texture`) — identical to the
drawable texture on normal frames, still valid and still rendered-into on
starved frames. The nil warn is also rate-limited (1 per 300 frames).
Harness-side guard (pipeline `49c47c8`): `capture_clip` fails loudly when any
expected frame is missing and the driver retries.

Open question: WHY starvation persists for a whole run (instant nil, normal
wall-clock) — likely fully-occluded/offscreen windows getting no compositor
service. Presentation still freezes in that state; only capture is immune now.
