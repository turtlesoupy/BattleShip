// stage_assets_derive.cpp — derive the port-added stages' CSS wallpaper +
// thumbnail PNGs from ROM bytes, platform-neutral.
//
// This is the in-process counterpart of tools/derive_stage_assets.py for
// builds that cannot run Python at first launch: the browser (linked into
// the Torch wasm module, see torch/src/lib/web.cpp) and, in future, Android
// (port/android_torch_bridge.cpp still carries its own copy of this logic).
//
// Only the ROM-derived outputs are produced here: <name>_background.png
// (300x220) and <name>_small.png (48x36). The nameplates (<name>_name.png)
// are script-rendered text, not ROM data, and stay in the package; the FD
// emblem PNG is skipped because mnMapsMakeEmblem does not render it yet.
//
// Depends only on Torch's bundled libvpk0 + n64graphics (rgba2png).

#include "stage_assets_derive.h"

#include "libvpk0/vpk0.h"
#include "n64graphics/n64graphics.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

#define DERIVE_LOG(...) std::fprintf(stderr, "CSS stage assets: " __VA_ARGS__), std::fputc('\n', stderr)

constexpr uint32_t kRelocTableRomAddr = 0x1AC870;   // US v1.0 (NALE) reloc table base
constexpr uint32_t kRelocTableEntrySize = 12;       // RELOC_TABLE_ENTRY_SIZE
constexpr uint32_t kRelocFileCount = 2132;          // US v1.0 reloc file count
// Data region starts after the table plus its trailing sentinel entry
// (mirrors torch SSB64::GetRelocLayout).
constexpr uint32_t kRelocDataStart =
    kRelocTableRomAddr + (kRelocFileCount + 1) * kRelocTableEntrySize;
constexpr int kIconW = 48;
constexpr int kIconH = 36;
constexpr int kSpriteSize = 68;
constexpr int kBitmapSize = 16;

struct StageDef {
    const char* name;
    uint32_t file_id;
    uint32_t sprite_off;
};

// Same table as tools/derive_stage_assets.py STAGES (minus name/emblem).
constexpr StageDef kStages[] = {
    { "final_destination", 96,   0x26C88 },
    { "metal_cavern",      0x62, 0x26C88 },
    { "battlefield",       0x61, 0x26C88 },
};

struct ParsedSprite {
    int16_t width;
    int16_t height;
    int16_t nbitmaps;
    int16_t bmheight;
    uint8_t bmfmt;
    uint8_t bmsiz;
};

struct ParsedBitmap {
    int16_t width;
    int16_t actual_height;
    uint32_t buf_off;
};

uint16_t read_be_u16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

int16_t read_be_s16(const uint8_t* p) {
    return (int16_t)read_be_u16(p);
}

uint32_t read_be_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool normalize_rom_byte_order(std::vector<uint8_t>& rom) {
    if (rom.size() < 4) return false;
    if (rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40) return true;
    if (rom[0] == 0x37 && rom[1] == 0x80 && rom[2] == 0x40 && rom[3] == 0x12) {
        const size_t n = rom.size() & ~size_t{1};
        for (size_t i = 0; i < n; i += 2) std::swap(rom[i], rom[i + 1]);
        return true;
    }
    if (rom[0] == 0x40 && rom[1] == 0x12 && rom[2] == 0x37 && rom[3] == 0x80) {
        const size_t n = rom.size() & ~size_t{3};
        for (size_t i = 0; i < n; i += 4) {
            std::swap(rom[i + 0], rom[i + 3]);
            std::swap(rom[i + 1], rom[i + 2]);
        }
        return true;
    }
    return false;
}

bool write_bytes(const std::filesystem::path& path, const unsigned char* data, int size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        DERIVE_LOG("mkdirs failed for %s: %s", path.parent_path().string().c_str(), ec.message().c_str());
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), size);
    return (bool)f;
}

bool extract_reloc_file(const std::vector<uint8_t>& rom, uint32_t file_id, std::vector<uint8_t>& out) {
    const size_t table_off = (size_t)kRelocTableRomAddr + (size_t)file_id * kRelocTableEntrySize;
    if (table_off + kRelocTableEntrySize > rom.size()) {
        DERIVE_LOG("ROM too small for reloc table entry %u", file_id);
        return false;
    }
    const uint8_t* te = rom.data() + table_off;
    const uint32_t first_word = read_be_u32(te + 0);
    const bool is_compressed = (first_word >> 31) != 0;
    const uint32_t data_offset = first_word & 0x7FFFFFFFu;
    const uint32_t compressed_bytes = (uint32_t)read_be_u16(te + 6) * 4u;
    const uint32_t decompressed_bytes = (uint32_t)read_be_u16(te + 10) * 4u;
    const size_t data_rom_addr = (size_t)kRelocDataStart + data_offset;
    if (data_rom_addr + compressed_bytes > rom.size()) {
        DERIVE_LOG("ROM too small for reloc file %u data", file_id);
        return false;
    }
    const uint8_t* file_data = rom.data() + data_rom_addr;
    out.resize(decompressed_bytes);
    if (is_compressed) {
        const uint32_t written = vpk0_decode(file_data, compressed_bytes, out.data(), decompressed_bytes);
        if (written == 0) {
            DERIVE_LOG("VPK0 decode failed for reloc file %u", file_id);
            return false;
        }
        if (written < decompressed_bytes) out.resize(written);
    } else {
        std::memcpy(out.data(), file_data, decompressed_bytes);
    }
    return true;
}

uint32_t resolve_reloc_ptr(const std::vector<uint8_t>& file_data, uint32_t field_off) {
    if (field_off + 4 > file_data.size()) return UINT32_MAX;
    return (read_be_u32(file_data.data() + field_off) & 0xFFFFu) * 4u;
}

bool parse_sprite(const std::vector<uint8_t>& file_data, uint32_t sprite_off, ParsedSprite& sp) {
    if (sprite_off + kSpriteSize > file_data.size()) return false;
    const uint8_t* d = file_data.data() + sprite_off;
    sp.width = read_be_s16(d + 0x04);
    sp.height = read_be_s16(d + 0x06);
    sp.nbitmaps = read_be_s16(d + 0x28);
    sp.bmheight = read_be_s16(d + 0x2C);
    sp.bmfmt = d[0x30];
    sp.bmsiz = d[0x31];
    return true;
}

bool parse_bitmap(const std::vector<uint8_t>& file_data, uint32_t bm_off, ParsedBitmap& bm) {
    if (bm_off + kBitmapSize > file_data.size()) return false;
    const uint8_t* d = file_data.data() + bm_off;
    bm.width = read_be_s16(d + 0x00);
    bm.actual_height = read_be_s16(d + 0x0C);
    bm.buf_off = resolve_reloc_ptr(file_data, bm_off + 0x08);
    return bm.buf_off != UINT32_MAX;
}

// N64 TMEM odd-row XOR-4 swizzle for 16bpp textures.
void unswizzle_rgba16_strip(std::vector<uint8_t>& strip, int width, int height) {
    const int row_bytes = width * 2;
    for (int row = 1; row < height; row += 2) {
        uint8_t* row_data = strip.data() + (size_t)row * row_bytes;
        for (int off = 0; off + 7 < row_bytes; off += 8) {
            std::swap(row_data[off + 0], row_data[off + 4]);
            std::swap(row_data[off + 1], row_data[off + 5]);
            std::swap(row_data[off + 2], row_data[off + 6]);
            std::swap(row_data[off + 3], row_data[off + 7]);
        }
    }
}

rgba rgba16_pixel_to_rgba(uint8_t hi, uint8_t lo) {
    const uint16_t word = (uint16_t)((uint16_t)hi << 8 | (uint16_t)lo);
    const uint8_t r5 = (uint8_t)((word >> 11) & 0x1F);
    const uint8_t g5 = (uint8_t)((word >> 6) & 0x1F);
    const uint8_t b5 = (uint8_t)((word >> 1) & 0x1F);
    const uint8_t a1 = (uint8_t)(word & 0x01);
    return {
        (uint8_t)((r5 << 3) | (r5 >> 2)),
        (uint8_t)((g5 << 3) | (g5 >> 2)),
        (uint8_t)((b5 << 3) | (b5 >> 2)),
        (uint8_t)(a1 ? 255 : 0),
    };
}

bool write_png_rgba(const std::filesystem::path& path, const std::vector<rgba>& pixels, int width, int height) {
    unsigned char* png = nullptr;
    int png_size = 0;
    if (rgba2png(&png, &png_size, pixels.data(), width, height) != 0 || png == nullptr || png_size <= 0) {
        DERIVE_LOG("PNG encode failed for %s", path.string().c_str());
        return false;
    }
    const bool ok = write_bytes(path, png, png_size);
    std::free(png);
    if (!ok) DERIVE_LOG("write failed for %s", path.string().c_str());
    return ok;
}

// Centre-crop to the icon aspect, then bilinear downscale (matches the
// Python LANCZOS output closely enough for a 48x36 thumbnail).
std::vector<rgba> make_icon_bilinear(const std::vector<rgba>& src, int src_w, int src_h) {
    std::vector<rgba> dst((size_t)kIconW * kIconH);
    int crop_x = 0, crop_y = 0, crop_w = src_w, crop_h = src_h;
    const float src_aspect = (float)src_w / (float)src_h;
    const float icon_aspect = (float)kIconW / (float)kIconH;
    if (src_aspect > icon_aspect) {
        crop_w = (int)((float)src_h * icon_aspect);
        crop_x = (src_w - crop_w) / 2;
    } else if (src_aspect < icon_aspect) {
        crop_h = (int)((float)src_w / icon_aspect);
        crop_y = (src_h - crop_h) / 2;
    }
    const float scale_x = (float)crop_w / (float)kIconW;
    const float scale_y = (float)crop_h / (float)kIconH;
    for (int y = 0; y < kIconH; ++y) {
        const float sy = (float)crop_y + ((float)y + 0.5f) * scale_y - 0.5f;
        const int y0 = std::max(0, std::min(src_h - 1, (int)sy));
        const int y1 = std::max(0, std::min(src_h - 1, y0 + 1));
        const float fy = std::max(0.0f, std::min(1.0f, sy - (float)y0));
        for (int x = 0; x < kIconW; ++x) {
            const float sx = (float)crop_x + ((float)x + 0.5f) * scale_x - 0.5f;
            const int x0 = std::max(0, std::min(src_w - 1, (int)sx));
            const int x1 = std::max(0, std::min(src_w - 1, x0 + 1));
            const float fx = std::max(0.0f, std::min(1.0f, sx - (float)x0));
            const rgba& p00 = src[(size_t)y0 * src_w + x0];
            const rgba& p10 = src[(size_t)y0 * src_w + x1];
            const rgba& p01 = src[(size_t)y1 * src_w + x0];
            const rgba& p11 = src[(size_t)y1 * src_w + x1];
            auto sample = [&](uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11) -> uint8_t {
                const float top = (float)c00 * (1.0f - fx) + (float)c10 * fx;
                const float bot = (float)c01 * (1.0f - fx) + (float)c11 * fx;
                const float v = top * (1.0f - fy) + bot * fy;
                return (uint8_t)std::max(0.0f, std::min(255.0f, v + 0.5f));
            };
            dst[(size_t)y * kIconW + x] = {
                sample(p00.red, p10.red, p01.red, p11.red),
                sample(p00.green, p10.green, p01.green, p11.green),
                sample(p00.blue, p10.blue, p01.blue, p11.blue),
                sample(p00.alpha, p10.alpha, p01.alpha, p11.alpha),
            };
        }
    }
    return dst;
}

bool derive_stage_asset(const std::vector<uint8_t>& rom, const StageDef& stage, const std::filesystem::path& out_dir) {
    std::vector<uint8_t> file_data;
    if (!extract_reloc_file(rom, stage.file_id, file_data)) return false;

    ParsedSprite sp{};
    if (!parse_sprite(file_data, stage.sprite_off, sp) ||
        sp.width <= 0 || sp.height <= 0 || sp.nbitmaps <= 0 || sp.bmsiz != 2) {
        DERIVE_LOG("unsupported sprite for %s", stage.name);
        return false;
    }
    const uint32_t bm_array_start = resolve_reloc_ptr(file_data, stage.sprite_off + 0x34);
    if (bm_array_start == UINT32_MAX) return false;

    std::vector<rgba> pixels;
    pixels.reserve((size_t)sp.width * sp.height);
    for (int i = 0; i < sp.nbitmaps; ++i) {
        ParsedBitmap bm{};
        const uint32_t bm_off = bm_array_start + (uint32_t)i * kBitmapSize;
        if (!parse_bitmap(file_data, bm_off, bm) || bm.width <= 0 || bm.actual_height <= 0) return false;
        const size_t strip_bytes = (size_t)bm.width * (size_t)bm.actual_height * 2u;
        if ((size_t)bm.buf_off + strip_bytes > file_data.size()) {
            DERIVE_LOG("%s bitmap[%d] out of range", stage.name, i);
            return false;
        }
        std::vector<uint8_t> strip(strip_bytes);
        std::memcpy(strip.data(), file_data.data() + bm.buf_off, strip_bytes);
        unswizzle_rgba16_strip(strip, bm.width, bm.actual_height);
        const int rendered_rows = std::min<int>(sp.bmheight, bm.actual_height);
        for (int row = 0; row < rendered_rows; ++row) {
            for (int x = 0; x < bm.width; ++x) {
                const size_t off = ((size_t)row * bm.width + x) * 2u;
                pixels.push_back(rgba16_pixel_to_rgba(strip[off], strip[off + 1]));
            }
        }
    }
    const size_t expected_pixels = (size_t)sp.width * sp.height;
    if (pixels.size() < expected_pixels) {
        DERIVE_LOG("%s decoded %zu pixels, expected %zu", stage.name, pixels.size(), expected_pixels);
        return false;
    }
    pixels.resize(expected_pixels);

    const std::filesystem::path bg_path = out_dir / (std::string(stage.name) + "_background.png");
    const std::filesystem::path icon_path = out_dir / (std::string(stage.name) + "_small.png");
    if (!write_png_rgba(bg_path, pixels, sp.width, sp.height)) return false;
    const std::vector<rgba> icon = make_icon_bilinear(pixels, sp.width, sp.height);
    if (!write_png_rgba(icon_path, icon, kIconW, kIconH)) return false;
    return true;
}

} // namespace

int ssb64_derive_css_stage_assets(const uint8_t* rom_bytes, size_t rom_len, const char* dst_dir) {
    std::vector<uint8_t> rom(rom_bytes, rom_bytes + rom_len);
    if (!normalize_rom_byte_order(rom)) {
        DERIVE_LOG("ROM has unrecognized byte order; skipping");
        return 0;
    }
    const std::filesystem::path out_dir = std::filesystem::path(dst_dir) / "assets" / "css_icons";
    int ok_count = 0;
    for (const StageDef& stage : kStages) {
        if (derive_stage_asset(rom, stage, out_dir)) ok_count++;
    }
    DERIVE_LOG("derived %d/%zu stage asset sets into %s",
               ok_count, sizeof(kStages) / sizeof(kStages[0]), out_dir.string().c_str());
    return ok_count;
}
