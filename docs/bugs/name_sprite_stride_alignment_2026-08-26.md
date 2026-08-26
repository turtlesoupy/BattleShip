# Injected name sprite shear: unaligned width_img stride

**Date:** 2026-08-26
**Area:** OpenSmash roster UI injection (`decomp/src/ft/ftport.c`, `port_ui_widen`)

## Symptom

A roster character's CSS card name drew as diagonally sheared garbage
(interleaved stripe bands) — but only for some characters. OBAMA (on the
48-wide Mario name sprite) corrupted; WEIRDAL (luigi, 40) and JFLYNN
(fox, 32) were fine.

## Root cause

`port_ui_widen` grows a fighter's name sprite when injected text is wider
than the vanilla bitmap (vanilla sizes these per name: FOX 32 … DK 72).
The first version set the new `width_img` (row stride) equal to the text
content width. OBAMA's 50px name produced a 50-texel stride.

**IA8 sprite rows must keep an 8-byte-aligned stride.** Every vanilla
name sprite pads `width_img` to a multiple of 8 above the drawn `width`
(47/48, 30/32, 63/64) for the TMEM row loads. An unaligned stride shifts
every row's start by the accumulated misalignment — the classic diagonal
shear. Widths that happened to land on multiples of 8 (56) worked, which
is why the bug looked character-specific.

## Fix

Pad the widened stride: `stride = (want_w + 7) & ~7`, keep the drawn
`Sprite.width`/`Bitmap.width` at the exact content width, and put the
padding only in `width_img` — mirroring the vanilla width/width_img
split. Rows are copied old-stride → new-stride; padding columns are
zeroed.

## Rule of thumb

Any synthetic or resized sprite bitmap in the port must keep
`width_img * bytes-per-texel` a multiple of 8. Draw width may be
anything ≤ stride.
