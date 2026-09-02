# Browser controller ports (gamepads + keyboard per N64 port)

Added 2026-09-01. The browser build routes each logical N64 port to a
device chosen by the web shell instead of libultraship's SDL auto-assign.

## Why

The game's own multiplayer logic keys everything off "is a controller
plugged into port N" (`osContGetQuery` errno / `OSContPad.errno`):
`mnPlayersVSUpdateGate` spawns and ejects the per-port hand cursor on the
VS character select every frame, `mnPlayersVSInitPlayer` closes a human
slot whose port has no pad, menus accept input from any connected port,
and `scmanager.c` sends you to the no-controller screen when none are.
The port previously hard-wired all four ports as connected
(`port/stubs/n64_stubs.c`), so the select screen always showed four
idle cursors and only port 1 (keyboard) ever moved. libultraship also
pins keyboard mappings to port 1, so a keyboard player could never be P2.

## Pieces

| Layer | File | Role |
|-------|------|------|
| Shell | `web/index.html` (`window.controllerPorts`) | Owns the port map. Reads `?ports=<json>` (the site's plan: per port `{kind:'gamepad',id,index}`, `{kind:'keyboard'}`, `{kind:'none'}` = explicitly closed, or `null` = free); without it auto-plans (gamepads by index, keyboard only when no gamepad). Handles `gamepadconnected`/`disconnected` (sticky: a lost pad leaves its port unplugged and takes it back on return; a new pad takes the lowest free port, never a closed one). `Module.readPorts(ptr)` fills 4 x `{kind, button, stick_x, stick_y}` int32 per game tick. `apply(plan)` swaps in a new plan live from the site's Advanced panel. |
| Port | `port/web_input.cpp` | `port_input_apply_pads()` runs right after `osContGetReadData` in `syControllerReadDeviceData`: kind NONE → zeroed pad + `CONT_NO_RESPONSE_ERROR`; KEYBOARD → copy of LUS port-1 pad (keyboard lives there); GAMEPAD → shell sample. Also parks every SDL gamepad in every port's ignore set so SDL and the shell never both drive a port. `port_input_port_connected()` feeds `osContGetQuery`. No-op on native, when the shell is not engaged, and under `SSB64_REPLAY_PLAY`. |
| Game | `decomp/src/sc/scmanager.c` | `SSB64_BOOT_HUMANS=<1..4>`: ports below the count are human slots in the `SSB64_BOOT_BATTLE` preset (fkind -1 = live pick). Also disables the eval-only deviations (no items, CP tags) that `p2kind==0` used to imply. |
| Site | `pipeline/web-prototype` (`shared/controller-ports.js`, `src/gamepads.js`, `launch-options.js`, Advanced panel) | Detects pads, shows the header badge and the Input section at the top of Advanced: one dropdown per port (P1..P4) offering the keyboard and each connected controller, each device on one port at most, with unfillable ports disabled ("Connect a controller to enable"). Choices persist per port as `auto`/`none`/`keyboard`/`gamepad:<index>`; defaults put controllers first and the keyboard on P1 only when there is no controller. Emits the `ports` plan to the shell. **Double select**: with two or more human ports the roster grid collects one pick per port (tiles are stamped 1P/2P, a prompt says who is up, clicking a stamped tile takes it back), then boots straight into the match with `SSB64_BOOT_HUMANS=n`, humans on ports 1..n and CPUs filling the rest; extra humans' custom fighters ride the per-player `inject_player` rows. A launch without picks (VS Character Select boot mode) opens the in-game select with `SSB64_START_SCENE=16` instead. The How-to-play step accepts gamepad input and shows the pad's button names; the keyboard legend is the engine's real map (X C Z E R + WASD). |

## Gamepad → N64 mapping (standard layout)

A=A, B=B, X/Y=C-up (jump), LB=L, RB or RT=R, LT=Z, Start=Start,
D-pad=D-pad, right stick=C buttons (0.5 threshold), left stick=stick
(radial deadzone 0.15, scaled to ±80).

## Testing without hardware

Fake pads in the page or the engine iframe:

```js
navigator.getGamepads = () => pads;              // [{index,id,connected:true,mapping:'standard',buttons:[...17],axes:[0,0,0,0]}]
const e = new Event('gamepadconnected'); Object.defineProperty(e, 'gamepad', { value: pads[0] }); window.dispatchEvent(e);
```

`window.controllerPorts.snapshot()` in the iframe shows the live map;
`port_log` prints `web input owns routing (kinds a,b,c,d)` once engaged.
