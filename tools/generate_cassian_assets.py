#!/usr/bin/env python3
"""Generate Flash-resident LVGL 9 assets from the personal site's local sources.

Requires fontTools (including WOFF2 support), Node.js and sharp. Only regeneration
uses those tools; firmware builds compile the checked-in C files directly.
"""

import argparse
import json
import math
from pathlib import Path
import re
import subprocess

from fontTools import subset
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


ROOT = Path(__file__).resolve().parents[1]
FONT_SPECS = [
    ("geist-latin.woff2", "cassian_display_28", 28, 600,
     " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.%-"),
    ("instrument-serif-latin.woff2", "cassian_editorial_18", 18, None,
     " CFBuilding things.Mapping ideas.Leaving traces."),
    ("jetbrains-mono-latin.woff2", "cassian_mono_12", 12, 400,
     "".join(map(chr, range(32, 127)))),
    ("jetbrains-mono-latin.woff2", "cassian_mono_14", 14, 400,
     "".join(map(chr, range(32, 127)))),
    ("jetbrains-mono-latin.woff2", "cassian_mono_32", 32, 400,
     "".join(map(chr, range(32, 127)))),
]


def chinese_inventory():
    """Read the fixed UI literals, excluding comments and control characters."""
    source = (ROOT / "main/personal_home.c").read_text()
    literals = re.findall(r'"(?:[^"\\]|\\.)*"', source)
    return "".join(sorted({char for text in literals for char in text
                           if ord(char) > 127}))


def retain_chinese_source(path, characters):
    """Keep only a licensed, renamed subset; the full CJK family is not linked."""
    font = TTFont(path)
    missing = sorted(set(map(ord, characters)) - set(font.getBestCmap()))
    if missing:
        raise ValueError(f"Chinese source lacks {missing}")
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    converter = subset.Subsetter(options=options)
    converter.populate(text=characters)
    converter.subset(font)
    # Give this modified source its own family name while keeping the original
    # copyright/license records, as required by the source font's OFL notice.
    names = {
        1: "Cassian UI", 2: "Regular", 3: "Cassian UI Regular Subset",
        4: "Cassian UI Regular", 6: "CassianUI-Regular", 16: "Cassian UI",
        17: "Regular",
    }
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    font.flavor = "woff2"
    font.save(ROOT / "assets/fonts/cassian-ui-source.woff2")

# Rasterize vector outlines, not browser screenshots. Each job has explicit
# dimensions; there is no runtime scaling or font lookup on the microcontroller.
RASTERIZE = r"""
const fs = require('node:fs');
const sharp = require(process.argv[1]);
const input = JSON.parse(fs.readFileSync(0, 'utf8'));
(async () => {
  const glyphs = [];
  for (const job of input.glyphs) {
    const data = await sharp(Buffer.from(job.svg)).ensureAlpha()
      .extractChannel('alpha').raw().toBuffer();
    glyphs.push(data.toString('hex'));
  }
  const portrait = await sharp(input.portrait).resize(216, 168, {
    fit: 'cover', position: 'centre'
  }).removeAlpha().toColourspace('srgb').raw().toBuffer();
  process.stdout.write(JSON.stringify({glyphs, portrait: portrait.toString('hex')}));
})().catch(e => { console.error(e); process.exit(1); });
"""


def byte_array(data):
    return "\n".join(
        "    " + ", ".join(f"0x{value:02x}" for value in data[i:i + 16]) + ","
        for i in range(0, len(data), 16)
    )


def prepare_font(filename, symbol, size, weight, characters):
    font = TTFont(ROOT / "assets/fonts" / filename)
    if weight is not None and "fvar" in font:
        font = instantiateVariableFont(font, {"wght": weight}, inplace=True)
    glyph_set = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font["head"].unitsPerEm
    glyphs = []
    for char in sorted(set(characters)):
        name = cmap.get(ord(char))
        if name is None:
            raise ValueError(f"{filename} lacks U+{ord(char):04X}")
        bounds = BoundsPen(glyph_set)
        glyph_set[name].draw(bounds)
        advance = round(font["hmtx"][name][0] * scale * 16)
        if bounds.bounds is None:
            glyphs.append(dict(code=ord(char), advance=advance, x=0, y=0,
                               width=0, height=0, svg=None))
            continue
        x0, y0, x1, y1 = bounds.bounds
        left, bottom = math.floor(x0 * scale), math.floor(y0 * scale)
        right, top = math.ceil(x1 * scale), math.ceil(y1 * scale)
        width, height = right - left, top - bottom
        pen = SVGPathPen(glyph_set)
        glyph_set[name].draw(pen)
        svg = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">'
            f'<path fill="white" transform="matrix({scale} 0 0 {-scale} '
            f'{-left} {top})" d="{pen.getCommands()}"/></svg>'
        )
        glyphs.append(dict(code=ord(char), advance=advance, x=left, y=bottom,
                           width=width, height=height, svg=svg))
    top = max(glyph["y"] + glyph["height"] for glyph in glyphs)
    bottom = min(glyph["y"] for glyph in glyphs)
    # These fixed subsets do not need the source font's generous full-Unicode
    # line box. Include every generated glyph with one pixel of breathing room.
    return dict(symbol=symbol, glyphs=glyphs,
                line_height=max(size, top - bottom + 2), baseline=1 - bottom)


def write_font(spec, rasters):
    bitmap = bytearray()
    descriptors = ["    {0},"]
    codes = []
    for glyph in spec["glyphs"]:
        offset = len(bitmap)
        if glyph["svg"] is not None:
            raw = bytes.fromhex(next(rasters))
            assert len(raw) == glyph["width"] * glyph["height"]
            # LVGL PLAIN 4bpp stores pixels consecutively, high nibble first.
            pixels = [(value * 15 + 127) // 255 for value in raw]
            if len(pixels) % 2:
                pixels.append(0)
            bitmap.extend((pixels[i] << 4) | pixels[i + 1]
                          for i in range(0, len(pixels), 2))
        descriptors.append(
            f"    {{.bitmap_index = {offset}, .adv_w = {glyph['advance']}, "
            f".box_w = {glyph['width']}, .box_h = {glyph['height']}, "
            f".ofs_x = {glyph['x']}, .ofs_y = {glyph['y']}}},"
        )
        codes.append(glyph["code"])
    first = codes[0]
    offsets = ", ".join(str(code - first) for code in codes)
    symbol = spec["symbol"]
    content = f"""// Generated by tools/generate_cassian_assets.py; source/license: assets/README.md.
#include "lvgl.h"

static const uint8_t bitmap[] = {{
{byte_array(bitmap)}
}};
static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {{
{chr(10).join(descriptors)}
}};
static const uint16_t unicode_offsets[] = {{ {offsets} }};
static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{.range_start = {first}, .range_length = {codes[-1] - first + 1},
      .glyph_id_start = 1, .unicode_list = unicode_offsets,
      .list_length = {len(codes)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}},
}};
static const lv_font_fmt_txt_dsc_t descriptor = {{
    .glyph_bitmap = bitmap, .glyph_dsc = glyphs, .cmaps = cmaps,
    .cmap_num = 1, .bpp = 4, .bitmap_format = 0,
}};
const lv_font_t {symbol} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {spec['line_height']}, .base_line = {spec['baseline']},
    .dsc = &descriptor,
}};
"""
    (ROOT / "assets/fonts" / f"{symbol}.c").write_text(content)
    print(f"{symbol}: {len(codes)} glyphs, {len(bitmap)} bitmap bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sharp-module", default="sharp",
                        help="sharp package name or installed module directory")
    parser.add_argument("--chinese-source", type=Path,
                        help="licensed full Noto Sans SC font for refreshing the retained subset")
    args = parser.parse_args()
    chinese = chinese_inventory()
    characters = "".join(map(chr, range(32, 127))) + chinese
    if args.chinese_source:
        retain_chinese_source(args.chinese_source, characters)
    font_specs = FONT_SPECS + [
        ("cassian-ui-source.woff2", "cassian_ui_14", 14, None, characters),
        ("cassian-ui-source.woff2", "cassian_ui_18", 18, None, characters),
        ("cassian-ui-source.woff2", "cassian_ui_28", 28, None, "\u9759\u97f3"),
    ]
    fonts = [prepare_font(*spec) for spec in font_specs]
    jobs = [{"svg": glyph["svg"]} for font in fonts for glyph in font["glyphs"]
            if glyph["svg"] is not None]
    result = subprocess.run(
        ["node", "-e", RASTERIZE, args.sharp_module],
        input=json.dumps(dict(glyphs=jobs,
                              portrait=str(ROOT / "assets/images/cassian-avatar.png"))),
        text=True, capture_output=True, check=True,
    )
    rasterized = json.loads(result.stdout)
    rasters = iter(rasterized["glyphs"])
    for spec in fonts:
        write_font(spec, rasters)
    (ROOT / "assets/fonts/cassian-ui-characters.txt").write_text(chinese + "\n")

    rgb = bytes.fromhex(rasterized["portrait"])
    assert len(rgb) == 216 * 168 * 3
    pixels = bytearray()
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i:i + 3]
        color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        pixels.extend(color.to_bytes(2, "little"))
    (ROOT / "assets/images/cassian_portrait.c").write_text(f"""// Generated by tools/generate_cassian_assets.py; source/license: assets/README.md.
#include "lvgl.h"

static const LV_ATTRIBUTE_MEM_ALIGN uint8_t pixels[] = {{
{byte_array(pixels)}
}};
const lv_image_dsc_t cassian_portrait = {{
    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 216, .h = 168, .stride = 432}},
    .data_size = sizeof(pixels), .data = pixels,
}};
""")
    print(f"cassian_portrait: {len(pixels)} RGB565 bytes in Flash")


if __name__ == "__main__":
    main()
