// title_brand.cpp — smash.fun branding on the title screen.
//
// The title screen's "SUPER" / "BROS." / "TM" marks and the copyright
// footer are IA8 / I4 sprites stored inside reloc file 167 (MNTitle, US
// ROM). The port streams that file byte-for-byte out of BattleShip.o2r, so
// the artwork is swapped in memory the moment the file lands in RAM,
// before the byte-swap / reloc fixups run (portRelocByteSwapBlob). The
// replacement pixels are pre-encoded in the on-ROM texture layout by
// tools/gen_title_brand.py, so everything downstream (TEXSHUF deswizzle,
// TMEM loads, sprite tinting) treats them exactly like the originals.
//
// Result: "SUPER" blank, "BROS." -> ".FUN" (so the logo reads SMASH.FUN),
// both TM marks blank, footer -> "Thomas Dimson and Joey Flynn present".
//
// SSB64_TITLE_BRAND=0 restores the stock title screen.

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "port_log.h"

namespace {

struct TitleBrandCheck {
	uint32_t offset; // Sprite struct offset inside the file
	int16_t width;
	int16_t height;
};

struct TitleBrandPatch {
	uint32_t offset;
	uint32_t length;
	const unsigned char *data;
};

#include "title_brand_data.inc"

bool brand_enabled()
{
	static int state = 0; // 0 = uninit, 1 = on, -1 = off
	if (state == 0) {
		const char *env = std::getenv("SSB64_TITLE_BRAND");
		state = (env != nullptr && env[0] == '0') ? -1 : 1;
	}
	return state == 1;
}

// Sprite header fields are big-endian s16 at +4 (width) / +6 (height).
bool sprite_matches(const uint8_t *p, const TitleBrandCheck &c)
{
	int16_t w = (int16_t)((p[c.offset + 4] << 8) | p[c.offset + 5]);
	int16_t h = (int16_t)((p[c.offset + 6] << 8) | p[c.offset + 7]);
	return w == c.width && h == c.height;
}

} // namespace

// Called with the raw (ROM byte order) file image, before any fixups.
extern "C" void portTitleBrandPatch(void *data, size_t size, unsigned int file_id)
{
	if (file_id != TITLE_BRAND_FILE_ID || size != TITLE_BRAND_FILE_SIZE)
		return;
	if (!brand_enabled())
		return;

	uint8_t *p = static_cast<uint8_t *>(data);
	for (const TitleBrandCheck &c : sTitleBrandChecks) {
		if (c.offset + 8 > size || !sprite_matches(p, c)) {
			port_log("[TITLE_BRAND] file %u does not look like US MNTitle (sprite @0x%X); leaving stock\n",
			         file_id, c.offset);
			return;
		}
	}
	size_t total = 0;
	for (const TitleBrandPatch &patch : sTitleBrandPatches) {
		if (patch.offset + patch.length > size)
			continue;
		std::memcpy(p + patch.offset, patch.data, patch.length);
		total += patch.length;
	}
	port_log("[TITLE_BRAND] applied smash.fun title branding (%zu bytes)\n", total);
}
