// stage_assets_derive.h — derive port-added stages' CSS wallpaper/thumbnail
// PNGs from ROM bytes. See stage_assets_derive.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// Writes <dst_dir>/assets/css_icons/<stage>_background.png and
// <stage>_small.png for each port-added stage. `rom_bytes` may be in any
// N64 byte order. Returns the number of stages derived successfully.
int ssb64_derive_css_stage_assets(const uint8_t* rom_bytes, size_t rom_len, const char* dst_dir);
