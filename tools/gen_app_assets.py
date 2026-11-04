#!/usr/bin/env python3
"""Generate main/ui_assets.{c,h} from the Lynn pixel sprites and inline icon art.

Sprites: assets/images/lynn/lynn_<pose>.png (64x64 RGBA, hard alpha).
Icons:   pixel art defined below as strings, one character per pixel.
Output:  LVGL 9 lv_image_dsc_t in RGB565A8 (RGB565 plane, then an A8 plane);
         sprites are pre-scaled 2x (128x128) with nearest neighbour.

Run from the repository root:  python3 tools/gen_app_assets.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPRITES = [
    # 静帧与心情的小动作
    "idle", "idle_blink", "idle_2", "idle_3", "happy", "happy_2", "happy_hop",
    "surprised", "surprised_2", "review", "review_2", "review_3",
    "sleep", "sleep_2", "sleep_3", "confused", "confused_2", "confused_3",
    # 待机日常与互动的逐帧动画(见 main/lynn_anim.c)
    "read_1", "read_2", "read_3", "read_4", "type_1", "type_2", "type_3", "type_4",
    "doze_1", "doze_2", "doze_3", "doze_4", "wave_1", "wave_2", "wave_3",
    "tea_1", "tea_2", "tea_3", "stretch_1", "stretch_2", "stretch_3",
    "music_1", "music_2", "music_3", "party_1", "party_2", "party_3",
    "inspect_1", "inspect_2",
]

# Sunbeam tokens (AstraFoundry/design): ink-900, ink-500, ink-300, yellow-500, success, danger, info.
COLORS = {
    "#": (0x14, 0x11, 0x0F),
    "m": (0x6C, 0x66, 0x60),
    "l": (0xC0, 0xB9, 0xAF),
    "y": (0xFF, 0xCC, 0x00),
    "g": (0x1F, 0x9E, 0x5E),
    "r": (0xE0, 0x3F, 0x1F),
    "b": (0x33, 0x5D, 0xDB),
    "w": (0xFF, 0xFF, 0xFF),
}

ICONS = {
    "star": """
....#....
...#y#...
...#y#...
####y####
#yyyyyyy#
.#yyyyy#.
.#yy#yy#.
#yy#.#yy#
###...###""",
    "star_ink": """
....#....
...###...
...###...
#########
#########
.#######.
.###.###.
###...###
##.....##""",
    "issue": """
..ggggg..
.g.....g.
g.......g
g...g...g
g..ggg..g
g...g...g
g.......g
.g.....g.
..ggggg..""",
    "issue_done": """
..mmmmm..
.m.....m.
m.......m
m.....m.m
m.m..m..m
m..mm...m
m.......m
.m.....m.
..mmmmm..""",
    "pr": """
.g.....g.
ggg...g..
.g...gggg
.g..g..g.
.g.....g.
.g.....g.
.g.....g.
ggg...ggg
.g.....g.""",
    "merged": """
.b.......
bbb......
.bb......
.b.b.....
.b..bb.b.
.b....bbb
.b.....b.
bbb......
.b.......""",
    "pr_closed": """
.r...r.r.
rrr...r..
.r...r.r.
.r.......
.r.....r.
.r.....r.
.r.....r.
rrr...rrr
.r.....r.""",
    "lock": """
..###..
.#...#.
.#...#.
#######
###.###
###.###
#######""",
}


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def emit(name, w, h, pixels, out):
    """pixels: list of (r, g, b, a) rows-major."""
    data = bytearray()
    for r, g, b, _ in pixels:
        v = rgb565(r, g, b) if _ else 0
        data += bytes((v & 0xFF, v >> 8))
    data += bytes(a for *_, a in pixels)
    out.append(f"static const uint8_t {name}_map[] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ", ".join(f"0x{x:02x}" for x in data[i:i + 24]) + ",")
    out.append("};")
    out.append(f"const lv_image_dsc_t {name} = {{")
    out.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,")
    out.append(f"                .w = {w}, .h = {h}, .stride = {w * 2} }},")
    out.append(f"    .data_size = sizeof({name}_map),")
    out.append(f"    .data = {name}_map,")
    out.append("};")
    out.append("")


def main():
    src = ["// main/ui_assets.c —— 由 tools/gen_app_assets.py 生成,请勿手改。",
           "// 精灵源图:assets/images/lynn/(Lynn 角色,lynn.stv.pm);图标为本项目原创像素画。",
           '#include "ui_assets.h"', ""]
    hdr = ["// main/ui_assets.h —— 由 tools/gen_app_assets.py 生成,请勿手改。",
           "#pragma once", "", '#include "lvgl.h"', ""]

    for pose in SPRITES:
        im = Image.open(ROOT / "assets/images/lynn" / f"lynn_{pose}.png").convert("RGBA")
        assert im.size == (64, 64), pose
        # 预先 2 倍最近邻放大:省掉 LVGL 运行时缩放的变换缓冲与 CPU,代价是 Flash。
        im = im.resize((128, 128), Image.NEAREST)
        px = [(r, g, b, 255 if a >= 128 else 0) for r, g, b, a in im.getdata()]
        name = f"ui_img_lynn_{pose}"
        emit(name, 128, 128, px, src)
        hdr.append(f"extern const lv_image_dsc_t {name};")

    for key, art in ICONS.items():
        rows = [r for r in art.strip("\n").split("\n")]
        w, h = len(rows[0]), len(rows)
        assert all(len(r) == w for r in rows), key
        px = []
        for row in rows:
            for ch in row:
                px.append((*COLORS[ch], 255) if ch != "." else (0, 0, 0, 0))
        name = f"ui_icon_{key}"
        emit(name, w, h, px, src)
        hdr.append(f"extern const lv_image_dsc_t {name};")

    (ROOT / "main/ui_assets.c").write_text("\n".join(src), encoding="utf-8")
    (ROOT / "main/ui_assets.h").write_text("\n".join(hdr) + "\n", encoding="utf-8")
    print(f"wrote {len(SPRITES)} sprites and {len(ICONS)} icons")


if __name__ == "__main__":
    main()
