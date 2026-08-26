#include "port_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static FILE *sLogFile = NULL;

void port_log_init(const char *path)
{
	if (sLogFile != NULL) return;
	sLogFile = fopen(path, "w");
}

void port_log_close(void)
{
	if (sLogFile != NULL) {
		fclose(sLogFile);
		sLogFile = NULL;
	}
}

int port_log_get_fd(void)
{
	if (sLogFile == NULL) return -1;
	return fileno(sLogFile);
}

void port_log(const char *fmt, ...)
{
#ifdef __EMSCRIPTEN__
	/* WASM: optionally mirror to stderr, which the shell routes to
	 * console.warn. OFF by default, and that default matters: several
	 * port breadcrumbs are ungated and fire per-GObj (gobj_alloc,
	 * gcEjectGObj ENTER/EXIT in sys/objman.c, ftMainSetStatus,
	 * figatree-bind), so a busy match emits hundreds of lines per frame.
	 * Each one becomes a retained console record; the burst blocks the
	 * main thread for seconds, starving the SDL audio queue — the
	 * intermittent "freeze + glitching sound + recovers cleanly" hitch.
	 * The mirror was added because the MEMFS log is unreadable while the
	 * main thread is blocked; it turned out to be what blocked it.
	 * Set SSB64_LOG_CONSOLE=1 (?SSB64_LOG_CONSOLE=1 on the shell URL) to
	 * get the breadcrumbs back when actually chasing a hang. The MEMFS
	 * file below is written either way. */
	{
		static int sMirror = -1;
		if (sMirror < 0) {
			const char *e = getenv("SSB64_LOG_CONSOLE");
			sMirror = (e != NULL && e[0] == '1') ? 1 : 0;
		}
		if (sMirror) {
			va_list ap2;
			va_start(ap2, fmt);
			vfprintf(stderr, fmt, ap2);
			va_end(ap2);
		}
	}
#endif
	if (sLogFile == NULL) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(sLogFile, fmt, ap);
	va_end(ap);
	/* fflush on every call costs seconds per frame on a slow drive when
	 * figatree watchdogs fire 28x per frame during a stuck APPEAR. Rely on
	 * stdio's buffer + OS-on-exit flush for normal logging; crash dumps
	 * have their own flush path. */
}
