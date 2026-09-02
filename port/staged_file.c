#include "staged_file.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* Returns 1 when the file exists in MEMFS (already there, or fetched now),
 * 0 when the shell knows no URL for it or the fetch failed. See
 * web/index.html Module.ensureStaged. */
EM_JS(int, port_ensure_staged_js, (const char *path), {
    if (typeof Module.ensureStaged !== 'function') return 1;
    try {
        return Module.ensureStaged(UTF8ToString(path)) ? 1 : 0;
    } catch (e) {
        console.warn('ensureStaged failed:', e);
        return 0;
    }
});
#endif

FILE *port_fopen_staged(const char *path, const char *mode)
{
    if (path == NULL)
    {
        return NULL;
    }
#ifdef __EMSCRIPTEN__
    port_ensure_staged_js(path);
#endif
    return fopen(path, mode);
}
