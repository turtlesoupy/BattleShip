/* Browser port map — see web_input.h for the contract. */
#include "web_input.h"
#include "port_log.h"

#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#endif

/* Values the shell writes into PortSample.kind. Anything but AUTO on any
 * port means the shell owns routing for this session. */
enum {
	PORT_KIND_AUTO = 0,     /* shell not engaged: keep libultraship's pad as-is */
	PORT_KIND_NONE = 1,     /* nothing plugged in: report CONT_NO_RESPONSE_ERROR */
	PORT_KIND_KEYBOARD = 2, /* the keyboard (libultraship keys it to port 1) */
	PORT_KIND_GAMEPAD = 3,  /* a browser Gamepad; button/stick come from the shell */
};

#define PORT_INPUT_NO_RESPONSE 0x8 /* CONT_NO_RESPONSE_ERROR */
#define PORT_INPUT_PORTS 4

/* One row per port, filled by Module.readPorts(ptr) as 4 x int32. */
typedef struct PortSample {
	int32_t kind;
	int32_t button;
	int32_t stick_x;
	int32_t stick_y;
} PortSample;

static PortSample sSamples[PORT_INPUT_PORTS];
static int sConnected[PORT_INPUT_PORTS] = { 1, 1, 1, 1 };
static int sEngaged = 0;
static int sReplayChecked = 0;
static int sReplayActive = 0;

#ifdef __EMSCRIPTEN__
static void port_input_sample_from_shell(void)
{
	for (int i = 0; i < PORT_INPUT_PORTS; i++) {
		sSamples[i].kind = PORT_KIND_AUTO;
		sSamples[i].button = 0;
		sSamples[i].stick_x = 0;
		sSamples[i].stick_y = 0;
	}
	/* The shell fills HEAP32[ptr/4 + port*4 + {0:kind,1:button,2:x,3:y}]. */
	EM_ASM({
		if (Module.readPorts) Module.readPorts($0);
	}, sSamples);
}

/* Once the shell owns routing, SDL must not also drive the ports: the
 * browser Gamepad the shell samples for port N is the same physical device
 * SDL's emscripten backend would auto-assign to port N (or worse, a
 * different one). Park every SDL pad in every port's ignore set; the
 * manager auto-assigns only pads it has never seen, so this sticks. */
static void port_input_quarantine_sdl_gamepads(void)
{
	auto context = Ship::Context::GetInstance();
	if (!context) return;
	auto deck = context->GetControlDeck();
	if (!deck) return;
	auto devices = deck->GetConnectedPhysicalDeviceManager();
	if (!devices) return;

	for (const auto& [instanceId, name] : devices->GetConnectedSDLGamepadNames()) {
		for (uint8_t port = 0; port < PORT_INPUT_PORTS; port++) {
			if (!devices->PortIsIgnoringInstanceId(port, instanceId)) {
				devices->IgnoreInstanceIdForPort(port, instanceId);
				port_log("SSB64: web input owns routing; SDL gamepad '%s' (instance %d) detached from port %d\n",
				         name.c_str(), (int)instanceId, (int)port + 1);
			}
		}
	}
}

static int8_t port_input_clamp_stick(int32_t v)
{
	if (v > 80) return 80;
	if (v < -80) return -80;
	return (int8_t)v;
}
#endif

void port_input_apply_pads(PortInputPad *pads)
{
#ifdef __EMSCRIPTEN__
	if (!sReplayChecked) {
		/* Scripted replays drive both fighters from a file; the ports must
		 * keep reporting as they always have or the harness baselines move. */
		sReplayActive = getenv("SSB64_REPLAY_PLAY") != NULL;
		sReplayChecked = 1;
	}
	if (sReplayActive || pads == NULL) return;

	port_input_sample_from_shell();

	int engaged = 0;
	for (int i = 0; i < PORT_INPUT_PORTS; i++) {
		if (sSamples[i].kind != PORT_KIND_AUTO) engaged = 1;
	}
	if (!engaged) {
		if (sEngaged) port_log("SSB64: web input released routing\n");
		sEngaged = 0;
		for (int i = 0; i < PORT_INPUT_PORTS; i++) sConnected[i] = 1;
		return;
	}
	if (!sEngaged) {
		port_log("SSB64: web input owns routing (kinds %d,%d,%d,%d)\n",
		         (int)sSamples[0].kind, (int)sSamples[1].kind, (int)sSamples[2].kind, (int)sSamples[3].kind);
		sEngaged = 1;
	}
	port_input_quarantine_sdl_gamepads();

	/* libultraship keys the keyboard (and mouse) to port 1 only. Snapshot
	 * that pad before the loop overwrites it so a keyboard player on any
	 * port gets the same data. */
	PortInputPad keyboard = pads[0];

	for (int i = 0; i < PORT_INPUT_PORTS; i++) {
		const PortSample *s = &sSamples[i];
		switch (s->kind) {
		case PORT_KIND_NONE:
			memset(&pads[i], 0, sizeof(pads[i]));
			pads[i].err = PORT_INPUT_NO_RESPONSE;
			sConnected[i] = 0;
			break;
		case PORT_KIND_KEYBOARD:
			pads[i] = keyboard;
			pads[i].err = 0;
			sConnected[i] = 1;
			break;
		case PORT_KIND_GAMEPAD:
			pads[i].button = (uint16_t)(s->button & 0xFFFF);
			pads[i].stick_x = port_input_clamp_stick(s->stick_x);
			pads[i].stick_y = port_input_clamp_stick(s->stick_y);
			pads[i].err = 0;
			sConnected[i] = 1;
			break;
		default:
			sConnected[i] = 1;
			break;
		}
	}
#else
	(void)pads;
#endif
}

int port_input_port_connected(int port)
{
	if (port < 0 || port >= PORT_INPUT_PORTS) return 0;
	return sConnected[port];
}
