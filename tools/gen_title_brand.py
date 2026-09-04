#!/usr/bin/env python3
"""
gen_title_brand.py — bake the smash.fun title-screen branding into the
byte patches applied by port/title_brand.cpp.

The title screen (scene nSCKindTitle) draws its text as IA8 / I4 sprites
stored in reloc file 167 (MNTitle, US ROM). The port streams that file
verbatim out of BattleShip.o2r, so the only place to change the artwork is
in-memory right after the file lands (portRelocByteSwapBlob hook). This
script renders the replacement text with system fonts, encodes it in the
exact on-ROM texture layout (IA8 nibbles, width_img row stride, SP_TEXSHUF
odd-row 32-bit half swap, per-bitmap strips) and writes the patches as C
arrays to port/title_brand_data.inc.

Sprite table (offsets are the ll* symbols from the decomp reloc symbol
table, i.e. byte offsets inside file 167):

    Copyright  0x15320  300x44 IA8, 4 strips  -> "Thomas Dimson and Joey Flynn present"
    Super      0x16728   64x50 IA8            -> blank
    Bros       0x25188   56x52 IA8            -> ".FUN"
    TM         0x0f398   32x12 I4             -> blank
    TMUnk      0x11aa8   32x12 I4             -> blank

Usage:
    python3 tools/gen_title_brand.py [--file <MNTitle raw bytes>] [--preview DIR]

The raw file can be the decomp's assets/us/relocData/167.vpk0.bin or the
`reloc_menus/MNTitle` entry of BattleShip.o2r (80-byte O2R header is
stripped automatically). Only the sprite/bitmap headers are read from it;
the pixel bytes are generated here.
"""

import argparse
import os
import struct
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
OUT_INC = os.path.join(ROOT, "port", "title_brand_data.inc")

FILE_ID = 167
FILE_SIZE = 168096

SPRITES = {
    "Copyright": 0x15320,
    "Super": 0x16728,
    "Bros": 0x25188,
    "TM": 0x0F398,
    "TMUnk": 0x11AA8,
}

FONT_DIR = "/System/Library/Fonts/Supplemental"
FONT_FOOTER = os.path.join(FONT_DIR, "Arial Rounded Bold.ttf")
FONT_LOGO = os.path.join(FONT_DIR, "Impact.ttf")


# ----------------------------------------------------------------------
# Raw file access
# ----------------------------------------------------------------------

def load_raw(path):
    if path is None:
        candidates = [
            os.path.join(ROOT, "..", "ssb-decomp-re", "assets", "us", "relocData", "167.vpk0.bin"),
            os.path.join(ROOT, "build-us", "BattleShip.o2r"),
            os.path.join(ROOT, "BattleShip.o2r"),
            os.path.join(ROOT, "..", "BattleShip.o2r"),
        ]
        for c in candidates:
            if os.path.exists(c):
                path = c
                break
        else:
            sys.exit("no MNTitle source found; pass --file")
    if path.endswith(".o2r"):
        with zipfile.ZipFile(path) as z:
            data = z.read("reloc_menus/MNTitle")
        if len(data) == FILE_SIZE + 80:
            data = data[80:]
    else:
        with open(path, "rb") as f:
            data = f.read()
    if len(data) != FILE_SIZE:
        sys.exit(f"{path}: unexpected size {len(data)} (want {FILE_SIZE}) — US ROM only")
    return data


def tok(v):
    """Reloc token -> byte offset (low 16 bits = word offset)."""
    return (v & 0xFFFF) * 4


def parse_sprite(d, off):
    s = d[off:off + 68]
    w, h = struct.unpack(">hh", s[4:8])
    attr = struct.unpack(">H", s[20:22])[0]
    nb, _ndl, _bmh, _bmhr, fmt, siz = struct.unpack(">hhhhBB", s[40:50])
    bm = tok(struct.unpack(">I", s[52:56])[0])
    bitmaps = []
    for i in range(nb):
        bw, bwi, _s, _t, buf, ah, _lut = struct.unpack(">hhhhIhh", d[bm + 16 * i:bm + 16 * i + 16])
        bitmaps.append(dict(width=bw, width_img=bwi, buf=tok(buf), height=ah))
    return dict(off=off, width=w, height=h, attr=attr, fmt=fmt, siz=siz, bitmaps=bitmaps)


# ----------------------------------------------------------------------
# N64 encode / decode (validated pixel-exact against the decomp's PNG dumps
# for Super and Bros)
# ----------------------------------------------------------------------

def texshuf(row):
    b = bytearray(row)
    for i in range(0, len(b) - 7, 8):
        b[i:i + 4], b[i + 4:i + 8] = b[i + 4:i + 8], b[i:i + 4]
    return bytes(b)


def encode_sprite(sp, img):
    """img: RGBA PIL image of sprite size. Returns [(offset, bytes)] per strip."""
    assert img.size == (sp["width"], sp["height"]), (img.size, sp)
    px = img.load()
    fmt, siz = sp["fmt"], sp["siz"]
    if (fmt, siz) == (3, 1):
        bpp = 8
    elif (fmt, siz) == (4, 0):
        bpp = 4
    else:
        raise SystemExit(f"unsupported format fmt={fmt} siz={siz}")
    patches = []
    y = 0
    for bmp in sp["bitmaps"]:
        stride = bmp["width_img"] * bpp // 8
        strip = bytearray()
        for r in range(bmp["height"]):
            row = bytearray(stride)
            if y < sp["height"]:
                for x in range(min(bmp["width"], sp["width"])):
                    R, G, B, A = px[x, y]
                    I = (R * 299 + G * 587 + B * 114) // 1000
                    if bpp == 8:
                        row[x] = ((I >> 4) << 4) | (A >> 4)
                    else:
                        v = I >> 4
                        if x & 1:
                            row[x >> 1] |= v
                        else:
                            row[x >> 1] |= v << 4
            if sp["attr"] & 0x200 and (r & 1):
                row = bytearray(texshuf(row))
            strip += row
            y += 1
        patches.append((bmp["buf"], bytes(strip)))
    return patches


def decode_sprite(sp, d):
    """Inverse of encode_sprite over a raw file image; used for previews."""
    fmt, siz = sp["fmt"], sp["siz"]
    bpp = 8 if (fmt, siz) == (3, 1) else 4
    img = Image.new("RGBA", (sp["width"], sp["height"]))
    px = img.load()
    y = 0
    for bmp in sp["bitmaps"]:
        stride = bmp["width_img"] * bpp // 8
        for r in range(bmp["height"]):
            row = d[bmp["buf"] + r * stride:bmp["buf"] + (r + 1) * stride]
            if sp["attr"] & 0x200 and (r & 1):
                row = texshuf(row)
            if y >= sp["height"]:
                break
            for x in range(min(bmp["width"], sp["width"])):
                if bpp == 8:
                    v = row[x]
                    I, A = (v >> 4) * 17, (v & 15) * 17
                else:
                    v = row[x >> 1]
                    I = ((v & 15) if (x & 1) else (v >> 4)) * 17
                    A = I
                px[x, y] = (I, I, I, A)
            y += 1
    return img


# ----------------------------------------------------------------------
# Artwork
# ----------------------------------------------------------------------

def render_outlined(text, font, scale, stroke, size, anchor_xy, rotate=0.0):
    """Render `text` white with a black outline into an RGBA image of
    `size`, supersampled at `scale`. anchor_xy is the centre in final px."""
    W, H = size
    big = Image.new("RGBA", (W * scale, H * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    cx, cy = anchor_xy[0] * scale, anchor_xy[1] * scale
    if rotate:
        # Draw centred on its own canvas, rotate, then paste.
        bbox = draw.textbbox((0, 0), text, font=font, stroke_width=stroke)
        tw, th = bbox[2] - bbox[0] + 2 * stroke, bbox[3] - bbox[1] + 2 * stroke
        pad = int(max(tw, th) * 0.6)
        glyph = Image.new("RGBA", (tw + 2 * pad, th + 2 * pad), (0, 0, 0, 0))
        gd = ImageDraw.Draw(glyph)
        gd.text((pad + stroke - bbox[0], pad + stroke - bbox[1]), text, font=font,
                fill=(255, 255, 255, 255), stroke_width=stroke, stroke_fill=(0, 0, 0, 255))
        glyph = glyph.rotate(rotate, resample=Image.BICUBIC, expand=True)
        big.alpha_composite(glyph, (int(cx - glyph.width / 2), int(cy - glyph.height / 2)))
    else:
        draw.text((cx, cy), text, font=font, anchor="mm", fill=(255, 255, 255, 255),
                  stroke_width=stroke, stroke_fill=(0, 0, 0, 255))
    return big.resize((W, H), Image.LANCZOS)


def art_copyright(sp):
    W, H = sp["width"], sp["height"]
    scale = 8
    font = ImageFont.truetype(FONT_FOOTER, 11 * scale)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    line1 = render_outlined("Thomas Dimson and Joey Flynn", font, scale, scale, (W, H), (W / 2, 7))
    line2 = render_outlined("present", font, scale, scale, (W, H), (W / 2, 21))
    img.alpha_composite(line1)
    img.alpha_composite(line2)
    return img


def art_bros(sp):
    W, H = sp["width"], sp["height"]
    scale = 8
    font = ImageFont.truetype(FONT_LOGO, 27 * scale)
    return render_outlined(".FUN", font, scale, 2 * scale, (W, H), (W / 2 + 1, H / 2 - 3), rotate=13)


def art_blank(sp):
    return Image.new("RGBA", (sp["width"], sp["height"]), (0, 0, 0, 0))


ART = {
    "Copyright": art_copyright,
    "Super": art_blank,
    "Bros": art_bros,
    "TM": art_blank,
    "TMUnk": art_blank,
}


# ----------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------

def write_inc(path, patches, checks):
    lines = []
    lines.append("/* Generated by tools/gen_title_brand.py — do not edit by hand. */")
    lines.append(f"#define TITLE_BRAND_FILE_ID {FILE_ID}u")
    lines.append(f"#define TITLE_BRAND_FILE_SIZE {FILE_SIZE}u")
    lines.append("")
    lines.append("/* {offset, width, height} of each replaced Sprite header — used to")
    lines.append(" * verify we are looking at the expected US MNTitle file before patching. */")
    lines.append("static const TitleBrandCheck sTitleBrandChecks[] = {")
    for name, off, w, h in checks:
        lines.append(f"\t{{ 0x{off:05X}u, {w}, {h} }}, /* {name} */")
    lines.append("};")
    lines.append("")
    for i, (name, off, data) in enumerate(patches):
        lines.append(f"/* {name} strip {i}: {len(data)} bytes @ 0x{off:05X} */")
        lines.append(f"static const unsigned char sTitleBrandData{i}[{len(data)}] = {{")
        for j in range(0, len(data), 24):
            chunk = data[j:j + 24]
            lines.append("\t" + ",".join(f"0x{b:02X}" for b in chunk) + ",")
        lines.append("};")
    lines.append("")
    lines.append("static const TitleBrandPatch sTitleBrandPatches[] = {")
    for i, (name, off, data) in enumerate(patches):
        lines.append(f"\t{{ 0x{off:05X}u, {len(data)}u, sTitleBrandData{i} }},")
    lines.append("};")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", help="raw MNTitle bytes (167.vpk0.bin or BattleShip.o2r)")
    ap.add_argument("--preview", help="directory for 4x preview PNGs (before/after)")
    ap.add_argument("--out", default=OUT_INC)
    args = ap.parse_args()

    d = load_raw(args.file)
    patched = bytearray(d)
    patches, checks = [], []
    for name, off in SPRITES.items():
        sp = parse_sprite(d, off)
        checks.append((name, off, sp["width"], sp["height"]))
        img = ART[name](sp)
        for buf, data in encode_sprite(sp, img):
            patches.append((name, buf, data))
            patched[buf:buf + len(data)] = data
        print(f"{name:10s} {sp['width']}x{sp['height']} fmt={sp['fmt']} siz={sp['siz']} "
              f"strips={len(sp['bitmaps'])} bytes={sum(len(p[2]) for p in patches if p[0] == name)}")
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            for tag, src in (("before", d), ("after", bytes(patched))):
                im = decode_sprite(sp, src)
                bg = Image.new("RGBA", im.size, (70, 70, 70, 255))
                bg.alpha_composite(im)
                bg.resize((im.width * 4, im.height * 4), Image.NEAREST).save(
                    os.path.join(args.preview, f"{name}_{tag}_x4.png"))

    write_inc(args.out, patches, checks)
    total = sum(len(p[2]) for p in patches)
    print(f"wrote {args.out}: {len(patches)} patches, {total} bytes")


if __name__ == "__main__":
    main()
