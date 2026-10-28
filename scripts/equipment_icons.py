#!/usr/bin/env python3
"""Build original monochrome equipment icons into LVGL 8 fonts.

Run: python -m pip install CairoSVG Pillow
     python scripts/equipment_icons.py
Artwork uses a 32-unit grid. Rasterization happens at build time only; firmware
uses uncompressed 4-bpp glyphs. Portafilter uses private-use U+E004 because
Unicode has no dedicated portafilter character. Keep codepoints stable.
"""
from pathlib import Path
from io import BytesIO
import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
# name, Unicode identifier, original SVG geometry
ICONS = [
    ('Coffee bean', 0x1FAD8, '<ellipse cx="16" cy="16" rx="10" ry="13" transform="rotate(35 16 16)"/><path d="M22 5C10 11 22 21 10 27"/>'),
    ('Mug', 0x2615, '<path d="M5 9H23V22Q23 27 18 27H10Q5 27 5 22Z M23 11H26Q31 11 29 17Q28 20 23 20 M9 5V3 M15 5V3 M21 5V3"/>'),
    ('Kettle', 0x1FAD6, '<path d="M9 13Q3 13 5 23Q6 28 17 28Q27 28 27 21L25 13Z M25 17L30 12L29 23L26 25 M9 12V9Q9 3 17 3Q25 3 25 9V12 M12 10H22 M17 7V10"/>'),
    ('Dripper', 0x1F53B, '<path d="M4 6H28L20 23H12Z M9 10L14 20 M16 10V20 M23 10L18 20 M16 23V27 M6 28H26"/>'),
    ('Portafilter', 0xE004, '<path d="M3 5H19V11Q19 17 11 17Q3 17 3 11Z M19 10L29 21L24 26L14 16 M7 6V10 M11 6V10 M15 6V10 M8 18V22 M14 18V22"/>'),
    ('Leaf', 0x1F343, '<path d="M5 27L23 9 M8 23Q0 5 28 4Q29 28 8 23Z M12 20L12 13 M17 15H23"/>'),
    ('Flower', 0x1F33C, '<path d="M16 10C6 -1 0 12 10 16C-1 26 12 32 16 22C26 33 32 20 22 16C33 6 20 0 16 10Z"/><circle cx="16" cy="16" r="4"/>'),
    ('Tree', 0x1F333, '<path d="M16 3L5 15H10L3 24H29L22 15H27Z M16 24V30"/>'),
    ('Mountain', 0x26F0, '<path d="M2 28L13 6L21 20L25 13L31 28Z M9 14L13 17L16 12"/>'),
    ('Wave', 0x1F30A, '<path d="M2 23C9 23 6 6 19 6Q29 6 29 16Q23 10 21 16Q18 24 30 24 M3 28H29"/>'),
    ('Sun', 0x2600, '<circle cx="16" cy="16" r="7"/><path d="M16 2V5 M16 27V30 M2 16H5 M27 16H30 M6 6L8 8 M24 24L26 26 M6 26L8 24 M24 8L26 6"/>'),
    ('Moon', 0x1F319, '<path d="M23 3A13 13 0 1 0 29 23A13 13 0 0 1 23 3Z"/>'),
    ('Star', 0x2B50, '<path d="M16 2L20 11L30 12L23 19L25 29L16 24L7 29L9 19L2 12L12 11Z"/>'),
    ('Cloud', 0x2601, '<path d="M8 26A7 7 0 0 1 6 12A10 10 0 0 1 25 12A7 7 0 0 1 25 26Z"/>'),
    ('Lightning', 0x26A1, '<path d="M18 2L5 18H14L11 30L27 12H18Z"/>'),
    ('Heart', 0x2665, '<path d="M16 28L4 16C-4 5 9 -1 16 9C23 -1 36 5 28 16Z"/>'),
    ('Diamond', 0x2666, '<path d="M16 2L29 16L16 30L3 16Z"/>'),
    ('Cat', 0x1F431, '<path d="M5 13L4 3L12 8Q16 6 20 8L28 3L27 13Q32 29 16 29Q0 29 5 13Z M10 16V18 M22 16V18 M14 21L16 23L18 21 M2 20L9 22 M2 25L9 24 M23 22L30 20 M23 24L30 25"/>'),
    ('Bird', 0x1F426, '<path d="M3 21L7 13L13 19V11Q13 3 21 5Q25 6 25 11L30 13L25 15Q24 26 11 26Z M14 20L19 16 M12 26V30 M19 25V29"/><circle cx="21" cy="10" r="1" fill="white" stroke="none"/>'),
    ('Butterfly', 0x1F98B, '<path d="M15 13C0 -4 -1 16 10 18C-1 25 9 34 15 22 M17 13C32 -4 33 16 22 18C33 25 23 34 17 22 M16 12V27 M16 12L12 5 M16 12L20 5"/>'),
]


def raster(geometry, size):
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-1 -1 34 34"><g fill="none" stroke="white" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">{geometry}</g></svg>'
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size * 4, output_height=size * 4)
    return Image.open(BytesIO(png)).getchannel('A').resize((size, size), Image.Resampling.LANCZOS)


def font(size):
    glyphs = sorted(ICONS, key=lambda icon: icon[1])
    bitmap = []
    descriptors = ['{.bitmap_index=0, .adv_w=0, .box_w=0, .box_h=0, .ofs_x=0, .ofs_y=0}']
    for _, _, geometry in glyphs:
        pixels = [(value * 15 + 127) // 255 for value in raster(geometry, size).getdata()]
        descriptors.append(f'{{.bitmap_index={len(bitmap)}, .adv_w={(size + 2) * 16}, .box_w={size}, .box_h={size}, .ofs_x=1, .ofs_y=-2}}')
        bitmap.extend((pixels[i] << 4) | pixels[i+1] for i in range(0, len(pixels), 2))
    prefix = f'icons_{size}'
    data = ',\n'.join('    ' + ','.join(f'0x{v:02x}' for v in bitmap[i:i+24]) for i in range(0, len(bitmap), 24))
    cmaps = ',\n'.join(f'    {{.range_start={code}, .range_length=1, .glyph_id_start={i+1}, .type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}}' for i, (_, code, _) in enumerate(glyphs))
    glyph_descriptors = ',\n    '.join(descriptors)
    return f'''static const uint8_t {prefix}_bitmap[] = {{
{data}
}};
static const lv_font_fmt_txt_glyph_dsc_t {prefix}_glyphs[] = {{
    {glyph_descriptors}
}};
static const lv_font_fmt_txt_cmap_t {prefix}_cmaps[] = {{
{cmaps}
}};
static lv_font_fmt_txt_glyph_cache_t {prefix}_cache;
static const lv_font_fmt_txt_dsc_t {prefix}_dsc = {{
    .glyph_bitmap={prefix}_bitmap, .glyph_dsc={prefix}_glyphs,
    .cmaps={prefix}_cmaps, .cmap_num=20, .bpp=4, .cache=&{prefix}_cache
}};
const lv_font_t equipment_icons_{size} = {{
    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
    .line_height={size}, .base_line=2, .dsc=&{prefix}_dsc
}};
'''


if __name__ == '__main__':
    assert len(ICONS) == len({code for _, code, _ in ICONS}) == 20
    output = '// Generated by scripts/equipment_icons.py. Original artwork; do not edit.\n#include "lvgl.h"\n\n'
    output += font(22) + '\n' + font(32)
    (ROOT / 'main/equipment_icons.c').write_text(output)
    header = '#pragma once\n\nnamespace smart_scale::ui {\ninline constexpr const char* equipment_icons[] = {\n'
    header += ''.join(f'    "\\U{code:08x}", // {name}\n' for name, code, _ in ICONS)
    header += '};\n}\n'
    (ROOT / 'main/equipment_icons.hxx').write_text(header)
    print('Generated 20 icons at 22 px and 32 px')
