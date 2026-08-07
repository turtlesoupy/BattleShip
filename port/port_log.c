#include "port_log.h"

#include <stdio.h>
#include <stdarg.h>

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
	/* WASM: mirror to stderr (browser console) — the MEMFS log file is
	 * unreadable while the main thread is blocked, which is exactly when
	 * these breadcrumbs matter. */
	{
		va_list ap2;
		va_start(ap2, fmt);
		vfprintf(stderr, fmt, ap2);
		va_end(ap2);
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
