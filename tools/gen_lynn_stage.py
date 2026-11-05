#!/usr/bin/env python3
"""Generate the Lynn stage data and agent tool manifest from tools/lynn_actions.py.

  python3 tools/gen_lynn_stage.py --prep   # clean new raw drawings into assets/images/lynn_stage/
  python3 tools/gen_lynn_stage.py          # write main/lynn_stage_data.{c,h} and docs/lynn-tools.json

Frames: reused sprites come from assets/images/lynn/, new ones from assets/images/lynn_stage/ (64x64 RGBA,
feet on row 59). All frames and overlay sprites share one palette (index 0 = transparent) and are stored
cropped to their bounding box at 1 byte per pixel.
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lynn_actions as A  # noqa: E402

ASSETS_OLD = ROOT / 'assets/images/lynn'
ASSETS_NEW = ROOT / 'assets/images/lynn_stage'
RAW = [ROOT / 'build/artgen/stage3/out', ROOT / 'build/artgen/stage2/out']
STAGE_W, STAGE_H = 80, 76
PAPER = (250, 248, 245)
SHADOW = (223, 218, 211)
BASE_LINE = '今天也在这里陪你。'

C = {'k': (42, 43, 49), 'w': (255, 255, 255), 'r': (224, 63, 31), 'g': (31, 158, 94), 'd': (20, 98, 60),
     't': (186, 98, 66), 'T': (138, 66, 44), 'b': (74, 163, 219), 'B': (40, 90, 133), 's': (253, 236, 223),
     'n': (46, 59, 90), 'y': (255, 204, 0), 'm': (108, 102, 96), 'a': (234, 182, 158)}


def art(text):
    rows = text.strip('\n').split('\n')
    im = Image.new('RGBA', (max(len(r) for r in rows), len(rows)))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch not in '. ':
                im.putpixel((x, y), (*C[ch], 255))
    return im


def bubble(icon):
    """11x11 speech bubble: white body with a 7-wide icon (rows 2..), tail at the bottom."""
    inner = ['wwwwwww'] * 7
    for i, row in enumerate(icon[:6]):
        inner[1 + i] = row.ljust(7, '.').replace('.', 'w')
    rows = ['.kkkkkkkkk.'] + ['kw' + r + 'wk' for r in inner] + ['.kkkwwkkkk.', '....kwk....', '.....k.....']
    return art('\n'.join(rows))


EMOTE_ICONS = {
    'heart':    ['.rr.rr.', 'rrrrrrr', 'rrrrrrr', '.rrrrr.', '..rrr..', '...r...'],
    'note':     ['...BBB.', '...B.B.', '...B...', '.BBB...', '.BBB...'],
    'bang':     ['...k...', '...k...', '...k...', '.......', '...k...'],
    'question': ['..kkk..', '.k...k.', '....k..', '...k...', '.......', '...k...'],
    'sweat':    ['....b..', '...bb..', '..bbbb.', '..bbbb.', '...bb..'],
    'zzz':      ['.BBBB..', '...B...', '..B....', '.BBBB..', '....BB.', '....B..'],
    'anger':    ['.r...r.', '..r.r..', '.......', '..r.r..', '.r...r.'],
    'sparkle':  ['...y...', '..yyy..', 'yyyyyyy', '..yyy..', '...y...'],
    'dots':     ['.......', '.......', 'm.m.m..', '.......'],
    'tear':     ['...b...', '..bbb..', '.bbbbb.', '.bbbbb.', '..bbb..'],
}
SPRITES = {
    'HAND': art('''
....kkkkkkkk....
...kwwwwwwwwk...
...knnnnnnnnk...
..kssssssssssk..
.ksssssssssssk..
kssssssssssssssk
kssksssksssksssk
kssksssksssksssk
.kskkskksskkssk.
..k..k..k...kk..'''),
    'POT_DROOP': art('''
......d...
.....dgd..
....dggd..
...dgd.dd.
..dd..dgd.
..d...dgd.
.TTTTTTTT.
.TttttttT.
..TttttT..
..TTTTTT..'''),
    'POT_PERK': art('''
......yy..
.....yrry.
..d...yy..
.dgd..dd..
.dggd.dd.d
..dgddddgd
.TTTTTTTT.
.TttttttT.
..TttttT..
..TTTTTT..'''),
    'DROP': art('b\nB'),
    'SPLASH': art('b.b\n.b.'),
    'PLANE': art('''
kkk....
kwwkk..
.kwwwkk
.kwwkk.
kkk....'''),
    'POP': art('.kkk.\nkwwwk\nkwwwk\n.kwk.\n..k..'),
}


def grid64(path):
    big = Image.open(path).convert('RGBA')
    w = big.size[0]
    if w % 64 == 0:
        s = w // 64
        im = Image.new('RGBA', (64, 64))
        im.putdata([big.getpixel((x * s + s // 2, y * s + s // 2)) for y in range(64) for x in range(64)])
    else:
        big.putdata([(r, g, b, 255) if a >= 128 else (0, 0, 0, 0) for r, g, b, a in big.getdata()])
        im = big.convert('RGBa').resize((64, 64), Image.BOX).convert('RGBA')
    im.putdata([(r, g, b, 255) if a >= 128 else (0, 0, 0, 0) for r, g, b, a in im.getdata()])
    return im


def own_palette(im, n=18):
    opaque = [p[:3] for p in im.getdata() if p[3]]
    strip = Image.new('RGB', (len(opaque), 1))
    strip.putdata(opaque)
    pal = strip.quantize(colors=n, method=Image.Quantize.MEDIANCUT).getpalette()[:n * 3]
    return [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]


def snap(im, pal):
    def near(c):
        return min(pal, key=lambda p: (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2)
    cache = {}
    out = []
    for p in im.getdata():
        if not p[3]:
            out.append((0, 0, 0, 0))
            continue
        c = cache.get(p[:3]) or cache.setdefault(p[:3], near(p[:3]))
        out.append((*c, 255))
    res = Image.new('RGBA', im.size)
    res.putdata(out)
    return res


def feet(im):
    bottom = im.getbbox()[3]
    out = Image.new('RGBA', (64, 64))
    out.paste(im, (0, 60 - bottom))
    return out


def used_frames():
    names = []
    for a in A.IDLE:
        for k in ('entry', 'loop', 'exit'):
            names += [s['frame'] for s in a[k]]
    for a in A.INTERACT:
        names += [s['frame'] for s in a['steps']]
    names += [s['frame'] for s in A.BASE]
    return list(dict.fromkeys(names))


def prep():
    ASSETS_NEW.mkdir(parents=True, exist_ok=True)
    for name in used_frames():
        if name in A.ALL_FRAMES_FROM_ASSETS or (ASSETS_NEW / f'{name}.png').exists():
            continue
        src = next((d / f'{name}.png' for d in RAW if (d / f'{name}.png').exists()), None)
        if not src:
            print('missing raw', name)
            continue
        im = grid64(src)
        im = feet(snap(im, own_palette(im)))
        im.save(ASSETS_NEW / f'{name}.png')
        print('prepped', name, im.getbbox())


def load(name):
    p = ASSETS_OLD / f'lynn_{name}.png' if name in A.ALL_FRAMES_FROM_ASSETS else ASSETS_NEW / f'{name}.png'
    return Image.open(p).convert('RGBA')


def build():
    frames = used_frames()
    imgs = {n: load(n) for n in frames}
    sprites = dict(SPRITES)
    for e in A.EMOTES:
        sprites[f'EMOTE_{e.upper()}'] = bubble(EMOTE_ICONS[e])

    # 共用调色板:所有帧 + 道具 + 底色,中位切分到 ≤ 250 色。
    every = list(imgs.values()) + list(sprites.values())
    opaque = [p[:3] for im in every for p in im.getdata() if p[3]] + [PAPER, SHADOW]
    uniq = list(dict.fromkeys(opaque))
    if len(uniq) <= 250:
        pal = uniq
    else:
        strip = Image.new('RGB', (len(opaque), 1))
        strip.putdata(opaque)
        raw = strip.quantize(colors=250, method=Image.Quantize.MEDIANCUT).getpalette()[:750]
        pal = [tuple(raw[i:i + 3]) for i in range(0, 750, 3)]
    pal = list(dict.fromkeys(pal + [PAPER, SHADOW]))
    index = {}

    def idx(c):
        if c not in index:
            index[c] = 1 + min(range(len(pal)), key=lambda i: sum((pal[i][k] - c[k]) ** 2 for k in range(3)))
        return index[c]

    pixels = bytearray()
    def emit(im):
        x0, y0, x1, y1 = im.getbbox()
        off = len(pixels)
        for y in range(y0, y1):
            for x in range(x0, x1):
                p = im.getpixel((x, y))
                pixels.append(idx(p[:3]) if p[3] else 0)
        return (x0, y0, x1 - x0, y1 - y0, off)

    frame_bmp = {n: emit(imgs[n]) for n in frames}
    sprite_names = ['HAND', 'POT_DROOP', 'POT_PERK', 'DROP', 'SPLASH', 'PLANE', 'POP'] + \
        [f'EMOTE_{e.upper()}' for e in A.EMOTES]
    sprite_bmp = []
    for n in sprite_names:
        im = sprites[n]
        x0, y0, w, h, off = emit(im)
        sprite_bmp.append((0, 0, w, h, off)) if (x0, y0) == (0, 0) else sprite_bmp.append((-x0, -y0, w, h, off))

    # 壶嘴、纸飞机出手点、头部行数(点头/摇头只移动这些行)
    def spout(im):
        best = None
        for y in range(64):
            for x in range(36, 64):
                r, g, b, a = im.getpixel((x, y))
                if a and b > r + 50 and g > 120 and (best is None or (y, x) > (best[1], best[0])):
                    best = (x, y)
        return best
    sx, sy = spout(imgs['water_3'])
    p2 = imgs['plane_2']
    lx, ly = max(((x, y) for y in range(0, 40) for x in range(64) if p2.getpixel((x, y))[3]), key=lambda t: t[0] - t[1])
    pot_h = sprites['POT_DROOP'].height
    head_rows = 31

    steps = []
    def add(seq):
        start = len(steps)
        steps.extend(seq)
        return start, len(seq)

    actions = []
    for a in A.IDLE:
        e, ne = add(a['entry'])
        l, nl = add(a['loop'])
        x, nx = add(a['exit'])
        actions.append((a, 0, e, ne, l, nl, x, nx, a['loops'][0], a['loops'][1]))
    for a in A.INTERACT:
        e, ne = add(a['steps'])
        actions.append((a, 1, e, ne, 0, 0, 0, 0, 1, 1))
    base_start, base_n = len(steps), len(A.BASE)
    steps.extend(A.BASE)
    fidx = {n: i for i, n in enumerate(frames)}
    eidx = {e: i + 1 for i, e in enumerate(A.EMOTES)}

    h = ['// main/lynn_stage_data.h —— 由 tools/gen_lynn_stage.py 生成,请勿手改。', '#pragma once', '',
         '#include <stdint.h>', '',
         f'#define LYNN_STAGE_W {STAGE_W}', f'#define LYNN_STAGE_H {STAGE_H}',
         f'#define LYNN_FX_NONE {A.FX_NONE}', f'#define LYNN_FX_HAND {A.FX_HAND}',
         f'#define LYNN_FX_POT {A.FX_POT}', f'#define LYNN_FX_PLANE {A.FX_PLANE}',
         f'#define LYNN_F_FLIP {A.F_FLIP}', f'#define LYNN_F_POUR {A.F_POUR}',
         f'#define LYNN_F_LAUNCH {A.F_LAUNCH}', f'#define LYNN_F_PERK {A.F_PERK}',
         f'#define LYNN_HAND_NONE ({A.HAND_NONE})',
         f'#define LYNN_COLOR_PAPER {idx(PAPER)}', f'#define LYNN_COLOR_SHADOW {idx(SHADOW)}',
         f'#define LYNN_BASE_LINE "{BASE_LINE}"', '']
    for i, n in enumerate(sprite_names):
        h.append(f'#define LYNN_SPR_{n} {i}')
    h += [f'#define LYNN_SPR_EMOTE_BASE {sprite_names.index("EMOTE_" + A.EMOTES[0].upper())}', '',
          'typedef struct { int8_t x, y; uint8_t w, h; uint32_t offset; } lynn_bmp_t;',
          'typedef struct { uint8_t frame, ticks; int8_t lift, dx, hx, hy; uint8_t flags, emote; int8_t hand; } lynn_step_t;',
          'typedef struct {',
          '    const char *id, *name, *line, *desc;',
          '    uint8_t kind, fx;      // kind: 0 = 待机活动, 1 = 互动',
          '    uint16_t entry, n_entry, loop, n_loop, exit, n_exit;',
          '    uint8_t loops_min, loops_max;',
          '} lynn_action_t;', '',
          'extern const uint16_t lynn_palette[];', 'extern const uint8_t lynn_pixels[];',
          'extern const lynn_bmp_t lynn_frames[];', 'extern const lynn_bmp_t lynn_sprites[];',
          'extern const lynn_step_t lynn_steps[];', 'extern const lynn_step_t lynn_base[];',
          'extern const lynn_action_t lynn_actions[];',
          'extern const uint8_t lynn_action_count, lynn_idle_count, lynn_head_rows;',
          'extern const uint16_t lynn_frame_count, lynn_base_count;',
          'extern const int8_t lynn_spout_x, lynn_spout_y, lynn_launch_x, lynn_launch_y;',
          'extern const int8_t lynn_pot_x, lynn_pot_y, lynn_pot_rim;', '']

    def rgb565(c):
        return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)

    def cstr(s):
        return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

    def step_c(s):
        return (f'{{{fidx[s["frame"]]}, {s["t"]}, {s["lift"]}, {s["dx"]}, {s["hx"]}, {s["hy"]}, {s["flags"]}, '
                f'{eidx.get(s["emote"], 0)}, {s["hand"]}}}')

    c = ['// main/lynn_stage_data.c —— 由 tools/gen_lynn_stage.py 生成,请勿手改。',
         '// 帧来自 assets/images/lynn/ 与 assets/images/lynn_stage/(角色 Lynn,lynn.stv.pm);动作见 tools/lynn_actions.py。',
         '#include "lynn_stage_data.h"', '',
         f'const uint16_t lynn_palette[{len(pal) + 1}] = {{ 0,']
    c += ['    ' + ', '.join(f'0x{rgb565(p):04x}' for p in pal[i:i + 12]) + ',' for i in range(0, len(pal), 12)]
    c += ['};', f'const uint8_t lynn_pixels[{len(pixels)}] = {{']
    c += ['    ' + ','.join(str(b) for b in pixels[i:i + 40]) + ',' for i in range(0, len(pixels), 40)]
    c += ['};', f'const lynn_bmp_t lynn_frames[{len(frames)}] = {{']
    c += [f'    {{{x}, {y}, {w}, {hh}, {off}}},  // {n}' for n, (x, y, w, hh, off) in frame_bmp.items()]
    c += ['};', f'const lynn_bmp_t lynn_sprites[{len(sprite_bmp)}] = {{']
    c += [f'    {{{x}, {y}, {w}, {hh}, {off}}},  // {n}' for n, (x, y, w, hh, off) in zip(sprite_names, sprite_bmp)]
    c += ['};', f'const lynn_step_t lynn_steps[{len(steps)}] = {{']
    c += ['    ' + step_c(s) + ',' for s in steps]
    c += ['};', f'const lynn_step_t *const lynn_base_ptr = &lynn_steps[{base_start}];',
          f'const lynn_step_t lynn_base[{base_n}] = {{'] + ['    ' + step_c(s) + ',' for s in A.BASE]
    c += ['};', f'const lynn_action_t lynn_actions[{len(actions)}] = {{']
    for a, kind, e, ne, l, nl, x, nx, lo, hi in actions:
        c.append(f'    {{{cstr(a["id"])}, {cstr(a["name"])}, {cstr(a["line"])}, {cstr(a["desc"])}, {kind}, '
                 f'{a.get("fx", 0)}, {e}, {ne}, {l}, {nl}, {x}, {nx}, {lo}, {hi}}},')
    c += ['};',
          f'const uint8_t lynn_action_count = {len(actions)};', f'const uint8_t lynn_idle_count = {len(A.IDLE)};',
          f'const uint8_t lynn_head_rows = {head_rows};', f'const uint16_t lynn_frame_count = {len(frames)};',
          f'const uint16_t lynn_base_count = {base_n};',
          f'const int8_t lynn_spout_x = {sx}, lynn_spout_y = {sy};',
          f'const int8_t lynn_launch_x = {lx}, lynn_launch_y = {ly};',
          f'const int8_t lynn_pot_x = {sx - 1}, lynn_pot_y = {60 - pot_h}, lynn_pot_rim = {60 - pot_h + 6};', '']
    c = [l for l in c if not l.startswith('const lynn_step_t *const lynn_base_ptr')]
    (ROOT / 'main/lynn_stage_data.h').write_text('\n'.join(h), encoding='utf-8')
    (ROOT / 'main/lynn_stage_data.c').write_text('\n'.join(c), encoding='utf-8')

    # agent 工具清单
    def dur(seq):
        return round(sum(s['t'] for s in seq) / A.TICK_HZ, 1)
    acts = [dict(id=a['id'], name=a['name'], kind='idle', description=a['desc'], default_line=a['line'],
                 approx_seconds=dur(a['entry'] + a['loop'] * a['loops'][0] + a['exit'])) for a in A.IDLE]
    acts += [dict(id=a['id'], name=a['name'], kind='interaction', description=a['desc'], default_line=a['line'],
                  approx_seconds=dur(a['steps'])) for a in A.INTERACT]
    enum_doc = '; '.join(f'{x["id"]}: {x["description"]}' for x in acts)
    manifest = {
        'version': 1,
        'transport': {'serial': 'USB serial console, one command per line',
                      'commands': {'lynn_play_action': 'lynn play <action> [line]', 'lynn_say': 'lynn say <text>',
                                   'lynn_list_actions': 'lynn list', 'lynn_status': 'lynn status'}},
        'tools': [
            {'name': 'lynn_play_action',
             'description': 'Make Lynn, the pixel-art mascot on the device\'s Lynn page, perform an animated action. '
                            'Interactions play once and return to idle; idle activities loop a few times. Optionally '
                            'set the line shown in her speech card. ' + enum_doc,
             'input_schema': {'type': 'object', 'properties': {
                 'action': {'type': 'string', 'enum': [x['id'] for x in acts]},
                 'line': {'type': 'string', 'maxLength': 30,
                          'description': 'Optional Chinese or English line for her speech card (≤ 30 characters).'}},
                 'required': ['action']}},
            {'name': 'lynn_say',
             'description': 'Change the line in Lynn\'s speech card without changing her animation.',
             'input_schema': {'type': 'object', 'properties': {'text': {'type': 'string', 'maxLength': 30}},
                              'required': ['text']}},
            {'name': 'lynn_list_actions', 'description': 'List Lynn\'s available actions.',
             'input_schema': {'type': 'object', 'properties': {}}},
            {'name': 'lynn_status', 'description': 'Report the action Lynn is currently playing and her line.',
             'input_schema': {'type': 'object', 'properties': {}}},
        ],
        'actions': acts,
    }
    (ROOT / 'docs/lynn-tools.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                encoding='utf-8')
    write_docs(acts)
    print(f'{len(frames)} frames, {len(sprite_bmp)} sprites, {len(pal)} colours, {len(pixels)} pixel bytes, '
          f'{len(steps)} steps, {len(actions)} actions; spout=({sx},{sy}) launch=({lx},{ly})')


def write_docs(acts):
    """docs/lynn-actions.md + .zh_CN.md, generated from the same catalog as the firmware and tool manifest."""
    idle = [x for x in acts if x['kind'] == 'idle']
    inter = [x for x in acts if x['kind'] == 'interaction']
    en = ['<p align="right">', '  <a href="lynn-actions.zh_CN.md">简体中文</a> · <strong>English</strong>', '</p>', '',
          '# Lynn actions', '',
          '<!-- Generated by tools/gen_lynn_stage.py from tools/lynn_actions.py; edit the catalog, not this file. -->',
          '',
          'The Lynn page (left of Overview) shows a large full-body Lynn drawn on a 64-pixel grid and scaled 3x, '
          'animated at 24 fps. Each drawing is held for several ticks, as in hand-made pixel animation, while '
          'the patting hand, water drops, paper plane and emote bubbles move every tick.', '',
          f'She has {len(idle)} idle activities and {len(inter)} interactions. Between activities she breathes, '
          'blinks and glances around; every 8-15 seconds she starts a random idle activity (entry, a random number '
          'of loops, exit). Interactions interrupt her, play once and return to idle.', '',
          '## Controls', '',
          '| Input | Action |', '| --- | --- |',
          '| OK click or double click | A random interaction |',
          '| OK long press, UP/DOWN long press | Head pat |',
          '| UP / DOWN | Previous / next page |', '',
          '## Agent tools', '',
          'Every action is an agent tool. [`lynn-tools.json`](lynn-tools.json) contains tool definitions '
          '(`lynn_play_action`, `lynn_say`, `lynn_list_actions`, `lynn_status`) with JSON input schemas and the '
          'full action list. Until a network bridge exists, a host agent calls them through the USB serial '
          'console, one command per line:', '',
          '| Tool | Serial command |', '| --- | --- |',
          '| `lynn_play_action` | `lynn play <action> [line]` |',
          '| `lynn_say` | `lynn say <text>` |',
          '| `lynn_list_actions` | `lynn list` |',
          '| `lynn_status` | `lynn status` |', '',
          'Playing an action or changing the line switches the device to the Lynn page and wakes the screen. '
          'Lines are UTF-8, up to about 30 characters; characters outside the 12 px font (GB2312 level 1 and '
          'ASCII) show as placeholder boxes.', '',
          '## Idle activities', '', '| Id | Description | Seconds (one loop) |', '| --- | --- | --- |']
    en += [f'| `{x["id"]}` | {x["description"]} | {x["approx_seconds"]} |' for x in idle]
    en += ['', '## Interactions', '', '| Id | Description | Seconds |', '| --- | --- | --- |']
    en += [f'| `{x["id"]}` | {x["description"]} | {x["approx_seconds"]} |' for x in inter]
    en += ['', '## Adding an action', '',
           '1. Describe new drawings in `tools/lynn_art.py`, then generate keys and in-betweens with '
           '`python3 tools/lynn_art.py --phase keys|inbetweens` and the image-gen batch script.',
           '2. Clean them with `python3 tools/gen_lynn_stage.py --prep` and review them; they land in '
           '`assets/images/lynn_stage/`.',
           '3. Add the timeline to `tools/lynn_actions.py` and run `python3 tools/gen_lynn_stage.py`.',
           '4. Check it with `tools/lynn_stage_preview/run.sh <id>`, then run the host tests.', '']
    zh = ['<p align="right">', '  <strong>简体中文</strong> · <a href="lynn-actions.md">English</a>', '</p>', '',
          '# Lynn 动作', '', '<!-- 由 tools/gen_lynn_stage.py 根据 tools/lynn_actions.py 生成;请修改目录文件,不要改本文件。 -->',
          '',
          'Lynn 页（总览左边）显示大尺寸全身 Lynn：64 像素栅格的像素画整数放大 3 倍，以 24 fps 播放。每张画面'
          '像手绘像素动画一样保持若干拍，而摸头的手、水滴、纸飞机和表情气泡每拍都会移动。', '',
          f'她有 {len(idle)} 个待机活动和 {len(inter)} 个互动动作。活动之间她会呼吸、眨眼、左右张望；每隔 8~15 秒'
          '随机开始一个待机活动（进入、随机循环若干次、退出）。互动动作会打断她，播放一次后回到待机。', '',
          '## 按键', '', '| 输入 | 动作 |', '| --- | --- |',
          '| 确定单击或双击 | 随机一个互动 |', '| 确定长按、上/下长按 | 摸头 |', '| 上 / 下 | 上一页 / 下一页 |', '',
          '## Agent 工具', '',
          '每个动作都是 agent 工具。[`lynn-tools.json`](lynn-tools.json) 包含工具定义（`lynn_play_action`、'
          '`lynn_say`、`lynn_list_actions`、`lynn_status`）、JSON 输入结构和完整动作列表。在网络桥接完成之前，'
          '电脑上的 agent 通过 USB 串口调用它们，每行一条命令：', '',
          '| 工具 | 串口命令 |', '| --- | --- |',
          '| `lynn_play_action` | `lynn play <动作> [台词]` |', '| `lynn_say` | `lynn say <台词>` |',
          '| `lynn_list_actions` | `lynn list` |', '| `lynn_status` | `lynn status` |', '',
          '播放动作或修改台词会把设备切到 Lynn 页并点亮屏幕。台词为 UTF-8，约 30 字以内；12 px 字体（GB2312 '
          '一级汉字与 ASCII）以外的字符显示为占位方框。', '',
          '## 待机活动', '', '| Id | 名称 | 默认台词 | 秒（一轮） |', '| --- | --- | --- | --- |']
    zh += [f'| `{x["id"]}` | {x["name"]} | {x["default_line"]} | {x["approx_seconds"]} |' for x in idle]
    zh += ['', '## 互动动作', '', '| Id | 名称 | 默认台词 | 秒 |', '| --- | --- | --- | --- |']
    zh += [f'| `{x["id"]}` | {x["name"]} | {x["default_line"]} | {x["approx_seconds"]} |' for x in inter]
    zh += ['', '## 新增动作', '',
           '1. 在 `tools/lynn_art.py` 里描述新画面，用 `python3 tools/lynn_art.py --phase keys|inbetweens` 和 '
           'image-gen 批处理脚本生成关键帧与中间帧。',
           '2. 用 `python3 tools/gen_lynn_stage.py --prep` 清理并人工检查，结果放在 `assets/images/lynn_stage/`。',
           '3. 在 `tools/lynn_actions.py` 添加时间轴，运行 `python3 tools/gen_lynn_stage.py`。',
           '4. 用 `tools/lynn_stage_preview/run.sh <id>` 预览，再运行宿主测试。', '']
    (ROOT / 'docs/lynn-actions.md').write_text('\n'.join(en), encoding='utf-8')
    (ROOT / 'docs/lynn-actions.zh_CN.md').write_text('\n'.join(zh), encoding='utf-8')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--prep', action='store_true')
    args = ap.parse_args()
    prep() if args.prep else build()
