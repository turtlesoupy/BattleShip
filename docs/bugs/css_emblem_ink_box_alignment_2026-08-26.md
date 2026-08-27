# CSS series-emblem watermark misaligned for injected characters (2026-08-26)

**Symptom.** On the character-select screen, a roster character's card
watermark (the series emblem behind the fighter) sat ~7 px left of where the
vanilla emblem sits and grew taller than any vanilla one, so it clipped
against the card's left border and crowded the fighter. Vanilla fighters'
cards were unaffected. Reported from a WEIRDAL card (Link slot, Zelda
emblem): the accordion glyph ran off the panel edge, while PIKACHU and KIRBY
cards next to it looked correct.

**Root cause.** `port_ui_write_canvas_fit()` (`decomp/src/ft/ftport.c`) fit the
injected glyph into the sprite's whole drawn area and centered it there:

```c
ox = (dw - ow) / 2;
oy = (dh - oh) / 2;
```

That assumes the vanilla art is centered inside its own bitmap. It is not.
Every CSS series emblem is a 64x48 I4 tile whose ink occupies roughly
x[12..25]..[59..63], y[1..8]..[40..44] — centered near x=39.5, not x=32 —
because `mnPlayersVSMakeNameAndEmblem()` places the SObj at a fixed offset
(`pos.x = player * 69 + 24`, `pos.y = 143`) with no per-emblem adjustment, so
the left padding is baked into the texels. Measured from relocData file 20
(`pipeline/extract_vanilla_emblems.py`):

| emblem | ink box (x0,y0 w×h) | | emblem | ink box |
|---|---|---|---|---|
| Mario | 18,4 43×40 | | Zelda | 17,2 45×39 |
| Donkey | 14,6 49×36 | | Yoshi | 25,4 35×39 |
| Metroid | 17,1 45×44 | | FZero | 12,8 51×34 |
| Fox | 15,5 49×40 | | PMonsters | 21,3 41×41 |
| Kirby | 20,2 42×41 | | Mother | 22,4 40×40 |

Centering a glyph on the tile therefore placed it at x[2..60] instead of
x[17..61] for the Zelda slot: 8 px left and 14 px wider than the Triforce it
replaced.

**Fix.** Measure the ink box of the *vanilla* texels and fit into that box
instead of the full tile. The peak-intensity scan that already ran (to keep
the watermark's stock translucency) now collects the bbox in the same pass.
Both are read from the pristine snapshot when one exists, so a re-injection
(roster page flips, HUD re-inits, the same shared emblem sprite being hooked
once per card) measures the original art rather than the previous write —
`port_ui_write_canvas_fit()` now takes the snapshot itself instead of relying
on its callers. A blank target falls back to the whole drawn area.

No per-character or per-slot constants: the same code serves the CSS card
watermark and the in-match HUD damage-backdrop emblem, whose 27x25 sprite is
full-bleed (ink box = whole tile) and so renders exactly as before.

**Verification.** `SSB64_START_SCENE=16` + `SSB64_BOOT_BATTLE` preset with
`SSB64_INJECT_UI_SET=<char>.osbui:<fkind>`, screenshotting the P1 card:

```
before: OSBUI: injected emblem (canvas 48x48 -> 59x48 in 64x48)
after:  OSBUI: injected emblem (canvas 48x48 -> 45x37 at 17,3; vanilla ink 45x39 at 17,2 in 64x48)
```

Captured three slots (Link/Zelda = weirdalyankovic, Samus/Metroid = queen,
Mario/Mario = barackobama); the Mario run hooks the sprite twice in one scene
and reported an identical ink box both times, confirming the snapshot path.
Card crops match the vanilla emblem footprint.
