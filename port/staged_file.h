#ifndef PORT_STAGED_FILE_H
#define PORT_STAGED_FILE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fopen() for roster-provided files (fighter bundles, .osbui packs,
 * announcer clips) that the web shell may not have written into MEMFS yet.
 *
 * On the web the character select can list a thousand fighters; staging
 * every bundle up front would download gigabytes before the engine even
 * boots. Instead the shell registers a URL per MEMFS path
 * (Module.__stageUrls) and this wrapper asks it to fetch the file the first
 * time the engine opens it (Module.ensureStaged, synchronous by necessity —
 * the caller is deep inside game code on an N64-thread fiber). The shell
 * prefetches in the background so the synchronous path usually finds the
 * bytes already cached.
 *
 * On native builds this is plain fopen(). */
FILE *port_fopen_staged(const char *path, const char *mode);

#ifdef __cplusplus
}
#endif

#endif
