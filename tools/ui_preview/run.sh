#!/usr/bin/env bash
# Render Astra Monitor screens on the host with the real main/ui.c and LVGL.
# Usage: tools/ui_preview/run.sh [out_dir]   (default: build/ui_preview)
# Needs a C compiler, the managed LVGL component (run one idf.py build first),
# and Python with Pillow to turn the PPM frames into 2x PNGs.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${1:-${root}/build/ui_preview}"
lvgl="${root}/managed_components/lvgl__lvgl"
obj="${root}/build/ui_preview_obj"
mkdir -p "${out}" "${obj}"

if [[ ! -d "${lvgl}/src" ]]; then
    echo "ERROR: ${lvgl} missing; run idf.py build once to fetch managed components." >&2
    exit 1
fi

cflags=(-std=gnu11 -O1 -w -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE
        -I"${root}/tools/ui_preview" -I"${root}/tools/ui_preview/stubs"
        -I"${lvgl}" -I"${root}/main")

# LVGL 只编译一次(按源文件缓存目标文件)。
objs=()
while IFS= read -r src; do
    o="${obj}/$(echo "${src#"${lvgl}/"}" | tr '/' '_').o"
    if [[ ! -f "${o}" ]]; then
        "${CC:-cc}" "${cflags[@]}" -c "${src}" -o "${o}"
    fi
    objs+=("${o}")
done < <(find "${lvgl}/src" -name '*.c' | sort)

"${CC:-cc}" "${cflags[@]}" \
    "${root}/tools/ui_preview/preview.c" "${root}/main/ui.c" "${root}/main/ui_assets.c" \
    "${root}/main/gh_model.c" "${root}/main/app_nav.c" "${root}/main/portal_proto.c" "${root}/main/lynn_anim.c" "${root}/main/lynn_stage.c" "${root}/main/lynn_stage_data.c" \
    "${root}/assets/fonts/ui_font_12.c" "${root}/assets/fonts/ui_font_24.c" \
    "${root}/assets/fonts/ui_font_36.c" \
    "${objs[@]}" -lm -o "${obj}/preview"
"${obj}/preview" "${out}"

python3 - "${out}" <<'EOF'
import sys
from pathlib import Path
from PIL import Image
out = Path(sys.argv[1])
frames = []
for ppm in sorted(out.glob("*.ppm")):
    im = Image.open(ppm).convert("RGB").resize((480, 640), Image.NEAREST)
    im.save(ppm.with_suffix(".png"))
    ppm.unlink()
    frames.append(im)
sheet = Image.new("RGB", (len(frames) * 500, 660), (40, 36, 33))
for i, im in enumerate(frames):
    sheet.paste(im, (i * 500 + 10, 10))
sheet.save(out / "contact-sheet.png")
print(f"contact sheet: {out / 'contact-sheet.png'}")
EOF
