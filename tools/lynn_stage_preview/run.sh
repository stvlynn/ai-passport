#!/usr/bin/env bash
# Render Lynn stage actions with the real engine (main/lynn_stage.c + generated data) into page GIFs.
# Usage: tools/lynn_stage_preview/run.sh [action_id ...]   (default: all actions)
# Output: build/lynn_stage_preview/<id>.gif and contact.png. Needs a C compiler and Python + Pillow.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${root}/build/lynn_stage_preview"
mkdir -p "${out}"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -I"${root}/main" \
    "${root}/tools/lynn_stage_preview/dump.c" "${root}/main/lynn_stage.c" "${root}/main/lynn_stage_data.c" \
    -o "${out}/dump"
"${out}/dump" "${out}" "$@"

python3 - "${out}" "${root}" <<'EOF'
import struct, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out, root = Path(sys.argv[1]), Path(sys.argv[2])
font = ImageFont.truetype(str(root / 'build/fontsrc/fp12/fusion-pixel-12px-proportional-zh_hans.ttf'), 12)
W, H, SW, SH = 240, 320, 80, 76
PAPER, INK, MUTED, LINE, HAIR = (250, 248, 245), (20, 17, 15), (108, 102, 96), (56, 50, 46), (223, 218, 211)

def rgb(v):
    return ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)

mask = Image.new('L', (W, H), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=30, fill=255)
thumbs = []
for txt in sorted(out.glob('*.txt')):
    line, name, n, kind = txt.read_text(encoding='utf-8').splitlines()[:4]
    raw = (out / (txt.stem + '.bin')).read_bytes()
    pages = []
    for i in range(int(n)):
        px = struct.unpack_from(f'<{SW * SH}H', raw, i * SW * SH * 2)
        st = Image.new('RGB', (SW, SH))
        st.putdata([rgb(v) for v in px])
        pg = Image.new('RGB', (W, H), PAPER)
        pg.paste(st.resize((SW * 3, SH * 3), Image.NEAREST), (0, 76))
        d = ImageDraw.Draw(pg)
        d.rectangle((14, 36, 226, 72), fill=HAIR)
        d.rectangle((12, 34, 224, 70), fill=(255, 255, 255), outline=LINE)
        d.text((20, 38), line, fill=INK, font=font)
        d.text((20, 53), f'{kind} · {name}', fill=MUTED, font=font)
        d.rectangle((20, 13, 26, 19), fill=INK)
        d.text((32, 9), 'Lynn', fill=INK, font=font)
        for k in range(5):
            x = W // 2 - 23 + k * 10
            d.rectangle((x, 304, x + 5, 309), outline=INK, fill=INK if k == 0 else None)
        fin = Image.new('RGB', (W, H))
        fin.paste(pg, (0, 0), mask)
        pages.append(fin)
    big = [p.resize((W * 2, H * 2), Image.NEAREST).convert('P', palette=Image.ADAPTIVE, colors=128) for p in pages]
    big[0].save(out / f'{txt.stem}.gif', save_all=True, append_images=big[1:], duration=42, loop=0, disposal=2)
    thumbs.append((txt.stem, pages[len(pages) // 2]))
cols = 8
sheet = Image.new('RGB', (cols * 120, ((len(thumbs) + cols - 1) // cols) * 172), (40, 36, 33))
d = ImageDraw.Draw(sheet)
for i, (name, p) in enumerate(thumbs):
    x, y = (i % cols) * 120, (i // cols) * 172
    sheet.paste(p.resize((120, 160)), (x, y))
    d.text((x + 4, y + 160), name, fill=(255, 255, 255), font=font)
sheet.save(out / 'contact.png')
print(f'{len(thumbs)} actions -> {out}')
EOF
