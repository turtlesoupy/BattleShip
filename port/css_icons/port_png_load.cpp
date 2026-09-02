/*
 * port_png_load.cpp — tiny C-callable PNG decoder for roster art the shell
 * stages into MEMFS (opening-movie portraits).  stb_image's implementation
 * lives in libultraship's stb_impl.c; this only wraps it so C code in the
 * decomp tree (ftport.c) can decode without touching C++ headers.
 */
#ifdef PORT

#include "../port_log.h"
#include "../staged_file.h"

#include <stb_image.h>

#include <cstdio>

extern "C" unsigned char *port_png_load_rgba8(const char *path, int *w, int *h)
{
    if (path == nullptr || *path == '\0') return nullptr;
    /* on the web this pulls the file into MEMFS if the shell has a URL for it */
    FILE *probe = port_fopen_staged(path, "rb");
    if (probe == nullptr) {
        port_log("OSBUI: portrait missing: %s\n", path);
        return nullptr;
    }
    std::fclose(probe);
    int channels = 0;
    unsigned char *rgba = stbi_load(path, w, h, &channels, 4);
    if (rgba == nullptr) {
        port_log("OSBUI: portrait decode failed: %s (%s)\n", path,
                 stbi_failure_reason() ? stbi_failure_reason() : "?");
    }
    return rgba;
}

extern "C" void port_png_free(unsigned char *pixels)
{
    if (pixels != nullptr) stbi_image_free(pixels);
}

#endif /* PORT */
