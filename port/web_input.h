#pragma once

/**
 * web_input.h — logical N64 port -> browser input device routing.
 *
 * The browser shell (web/index.html) owns the map "port N is driven by
 * gamepad X / the keyboard / nothing" and re-plans it live on hot-plug.
 * The engine pulls one sample per port per game tick through
 * Module.readPorts() and overlays it on the pad data libultraship produced,
 * so the game's own "is a controller plugged into port N" logic (cursor
 * spawn on the character select, HMN/CPU/NOT defaults, no-controller
 * screen) follows the browser map instead of the all-four-connected stub.
 *
 * On native builds nothing is engaged and every call is a no-op: SDL
 * gamepads keep flowing through libultraship's own port routing.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror of the game's OSContPad (decomp/include/PR/os.h). */
typedef struct PortInputPad {
	uint16_t button;
	int8_t stick_x;
	int8_t stick_y;
	uint8_t err;
} PortInputPad;

/* Overlay the browser port map on the four pads just read from
 * libultraship (game OSContPad layout). Called from
 * syControllerReadDeviceData right after osContGetReadData. */
void port_input_apply_pads(PortInputPad *pads);

/* 1 if port N (0..3) should report a plugged-in controller to the game. */
int port_input_port_connected(int port);

#ifdef __cplusplus
}
#endif
