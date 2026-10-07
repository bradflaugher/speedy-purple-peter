#!/usr/bin/env python3
"""Render PNG previews of the SPEEDY PURPLE PETER art into build/preview/ (needs Pillow).

  metatiles.png   every level cell (animated ones: all 4 frames), DMG and CGB
  sprites.png     every sprite frame on the DMG sky (white) and the CGB sky (navy)
  font.png        the font, DMG and CGB
  title.png       the title screen (Peter's two sprint poses), DMG and CGB
  scene.png       a mock level screen with Peter and friends, DMG and CGB

Images are scaled 3x (``--scale``). Usage: python3 tools/preview_assets.py [--out DIR]
"""

import argparse
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_assets as G  # noqa: E402

DMG_SHADES = [(232, 236, 222), (160, 166, 154), (84, 90, 88), (24, 26, 28)]
SHEET = (54, 54, 60)


def c555(c):
    v = G.rgb555(c)
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    return tuple((x << 3) | (x >> 2) for x in (r, g, b))


def dmg(reg):
    return [DMG_SHADES[(reg >> (2 * i)) & 3] for i in range(4)]


class Pals(object):
    def __init__(self, d, mode):
        self.mode = mode
        if mode == 'dmg':
            self.bg = [dmg(d['dmg']['bgp'])] * 8
            o0, o1 = dmg(d['dmg']['obp0']), dmg(d['dmg']['obp1'])
            self.obj = [o1 if i == 1 else o0 for i in range(8)]
        else:
            self.bg = [[c555(c) for c in p] for p in d['bgpal']]
            self.obj = [[c555(c) for c in p] for p in d['objpal']]
        self.sky = self.bg[0][0]


def put_tile(img, x, y, tile, pal, transparent=False):
    for ty, row in enumerate(tile):
        for tx, v in enumerate(row):
            if transparent and v == 0:
                continue
            px, py = x + tx, y + ty
            if 0 <= px < img.width and 0 <= py < img.height:
                img.putpixel((px, py), pal[v])


def meta_tiles(d, name, frame=0):
    m = d['metas'][name]
    out = []
    for qi in range(4):
        out.append(m['q'][qi] if m['anim'][qi] is None else m['anim'][qi][frame])
    return out


def draw_meta(img, x, y, d, name, P, frame=0):
    m = d['metas'][name]
    for qi, t in enumerate(meta_tiles(d, name, frame)):
        put_tile(img, x + (qi & 1) * 8, y + (qi >> 1) * 8, t, P.bg[m['pals'][qi]])


def is_anim(d, name):
    return any(a is not None for a in d['metas'][name]['anim'])


SPR_PAL = {
    'PS': 0, 'PB': 0, 'GLOOP': 2, 'DOME': 2, 'SHELL': 2, 'JET': 3, 'CHOMP': 2, 'COMET': 3,
    'PLANET': 4, '1UP': 4, 'BLASTER': 4, 'NOVA': 5, 'COIN': 5, 'FLAG': 5, 'SHOT': 6,
    'PUFF': 6, 'SHARD': 6, 'N': 6, 'BRICK': 7, 'USED': 7,
}


def spr_pal(name):
    base = name[4:]
    for k in sorted(SPR_PAL, key=len, reverse=True):
        if base.startswith(k):
            return SPR_PAL[k]
    return 0


def sprite_pixels(d, name):
    """the frame as rows of colour indices"""
    for n, t0, (w, h) in d['spr_const']:
        if n == name:
            px = [[0] * w for _ in range(h)]
            i = t0
            for by in range(h // 16):
                for tx in range(w // 8):
                    for half in range(2):
                        t = d['spr'][i]
                        i += 1
                        for y in range(8):
                            for x in range(8):
                                px[by * 16 + half * 8 + y][tx * 8 + x] = t[y][x]
            return px
    raise KeyError(name)


def dash_pixels(d, pose):
    """the title's sprint: DASH0's top half over DASH0's (pose 0) or DASH1's (pose 1) legs"""
    top = sprite_pixels(d, 'SPR_PB_DASH0')
    return top[:16] + (sprite_pixels(d, 'SPR_PB_DASH1') if pose else top[16:])


def draw_sprite(img, x, y, d, name, P, pal=None, flip=False):
    px = dash_pixels(d, 1) if name == 'DASH1' else sprite_pixels(d, name)
    if name == 'DASH1':
        name = 'SPR_PB_DASH0'
    pal = P.obj[spr_pal(name) if pal is None else pal]
    w = len(px[0])
    for yy, row in enumerate(px):
        for xx, v in enumerate(row):
            if v:
                X = x + (w - 1 - xx if flip else xx)
                if 0 <= X < img.width and 0 <= y + yy < img.height:
                    img.putpixel((X, y + yy), pal[v])


def scale(img, k):
    return img.resize((img.width * k, img.height * k), Image.NEAREST)


def sheet_metatiles(d, P):
    names = d['t_names']
    per = 4                     # cells per row; each cell shows its 4 frames if animated
    cellw = 4 * 17 + 6
    img = Image.new('RGB', (per * cellw + 4, ((len(names) + per - 1) // per) * 20 + 4), SHEET)
    for i, n in enumerate(names):
        x, y = 4 + (i % per) * cellw, 4 + (i // per) * 20
        for f in range(4 if is_anim(d, n) else 1):
            draw_meta(img, x + f * 17, y, d, n, P, f)
    return img


def sheet_sprites(d, P):
    names = [n for n, _, _ in d['spr_const']]
    per = 12
    img = Image.new('RGB', (per * 20 + 4, ((len(names) + per - 1) // per) * 36 + 4), P.sky)
    for i, n in enumerate(names):
        x, y = 4 + (i % per) * 20, 4 + (i // per) * 36
        draw_sprite(img, x, y, d, n, P)
    return img


def sheet_peter(d, P):
    """Peter's frames, both suits, both directions"""
    small = ['SPR_PS_STAND', 'SPR_PS_WALK0', 'SPR_PS_WALK1', 'SPR_PS_WALK2', 'SPR_PS_SKID',
             'SPR_PS_JUMP', 'SPR_PS_DEAD']
    big = ['SPR_PB_STAND', 'SPR_PB_WALK0', 'SPR_PB_WALK1', 'SPR_PB_WALK2', 'SPR_PB_SKID',
           'SPR_PB_JUMP', 'SPR_PB_DUCK', 'SPR_PB_DASH0', 'DASH1']
    img = Image.new('RGB', (9 * 20 + 4, 4 * 36 + 4), P.sky)
    for r, (frames, pal, flip) in enumerate(((small, 0, False), (big, 0, False),
                                             (big, 1, False), (small, 0, True))):
        for i, n in enumerate(frames):
            h = 32 if n == 'DASH1' or (n.startswith('SPR_PB') and n != 'SPR_PB_DUCK') else 16
            draw_sprite(img, 4 + i * 20, 4 + r * 36 + (32 - h), d, n, P, pal, flip)
    return img


def sheet_font(d, P):
    chars = G.FONT_REQUIRED + 'abc'
    img = Image.new('RGB', (len(chars) * 8 + 8, 16), P.sky)
    for i, ch in enumerate(chars):
        idx = d['font_idx'].get(ch, 0)
        put_tile(img, 4 + i * 8, 4, d['bg'][idx], P.bg[6])
    return img


def tile_by_number(d, n):
    if n >= G.TITLE_BASE:
        return d['title_tiles'][n - G.TITLE_BASE]
    i = n if n < 128 else n - 80
    return d['bg'][i]


def draw_text(img, d, P, x, y, s, pal=6):
    for i, ch in enumerate(s):
        idx = d['font_idx'].get(ch, d['font_idx'][' '])
        put_tile(img, x + i * 8, y, d['bg'][idx], P.bg[pal])


def screen_title(d, P, pose=0):
    img = Image.new('RGB', (160, 144), P.sky)
    for y in range(18):
        for x in range(20):
            put_tile(img, x * 8, y * 8, tile_by_number(d, d['title_map'][y][x]),
                     P.bg[d['title_attr'][y][x]])
    # the engine's text and Peter, where title_screen() (src/gb/screens.c) puts them
    draw_text(img, d, P, 4 * 8, 12 * 8, 'PRESS START')
    draw_text(img, d, P, 3 * 8, 14 * 8, 'SEED 1985 SEL:NEW')
    draw_text(img, d, P, 3 * 8, 16 * 8, 'BEST 0012340')
    draw_text(img, d, P, 8 * 8, 17 * 8, '01234M')
    if pose:                    # the trail's tail flickers with his stride
        for y in (5, 6):
            put_tile(img, 0, y * 8, G.BLANK, P.bg[0])
    draw_sprite(img, 16, 40, d, 'DASH1' if pose else 'SPR_PB_DASH0', P)
    return img


SCENE = [
    '..........',
    '.a....PQ..',
    '..C...pq.b',
    '.LMMR.....',
    '.....?BQB.',
    '..........',
    '.......Tt.',
    '..h....Uu.',
    '.lFr.123ss',
    'GGGGGGGGGG',
    'gggggggggg',
]
SCENE_KEY = {'.': 'T_SKY', 'a': 'T_STARS_A', 'b': 'T_STARS_B', 'C': 'T_COIN',
             'L': 'T_CLOUD_L', 'M': 'T_CLOUD_M', 'R': 'T_CLOUD_R', '?': 'T_Q_COIN',
             'B': 'T_BRICK', 'Q': 'T_Q_POWER', 'T': 'T_TUBE_TL', 't': 'T_TUBE_TR',
             'U': 'T_TUBE_L', 'u': 'T_TUBE_R', 's': 'T_SOLID', 'h': 'T_HILL_TOP',
             'l': 'T_HILL_L', 'F': 'T_HILL_CRATER', 'r': 'T_HILL_R', 'G': 'T_GROUND_TOP',
             'g': 'T_GROUND', '1': 'T_BUSH_L', '2': 'T_BUSH_M', '3': 'T_BUSH_R',
             'P': 'T_PLANET_TL', 'p': 'T_PLANET_BL', 'q': 'T_PLANET_BR'}


def screen_level(d, P, frame=0):
    img = Image.new('RGB', (160, 144), P.sky)
    for y, row in enumerate(SCENE):
        for x, ch in enumerate(row):
            name = SCENE_KEY[ch]
            if ch == 'Q' and y == 1:
                name = 'T_PLANET_TR'
            draw_meta(img, x * 16, y * 16 - 16, d, name, P, frame)
    draw_sprite(img, 120, 56, d, 'SPR_CHOMP0', P)       # really drawn behind the BG
    for x in range(7, 9):                                # so redraw the tube over it
        for y in (6, 7):
            draw_meta(img, x * 16, y * 16 - 16, d, SCENE_KEY[SCENE[y][x]], P)
    draw_sprite(img, 20, 96, d, 'SPR_PS_WALK0', P)
    draw_sprite(img, 92, 32, d, 'SPR_PB_JUMP', P)
    draw_sprite(img, 52, 96, d, 'SPR_GLOOP', P)
    draw_sprite(img, 136, 80, d, 'SPR_DOME0', P, flip=True)
    draw_sprite(img, 4, 56, d, 'SPR_COMET', P)
    draw_sprite(img, 76, 48, d, 'SPR_PLANET', P)
    return img


def side_by_side(*imgs, gap=6):
    w = sum(i.width for i in imgs) + gap * (len(imgs) - 1)
    h = max(i.height for i in imgs)
    out = Image.new('RGB', (w, h), SHEET)
    x = 0
    for i in imgs:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(G.ROOT, 'build', 'preview'))
    ap.add_argument('--scale', type=int, default=3)
    a = ap.parse_args(argv)
    d = G.load_assets()
    os.makedirs(a.out, exist_ok=True)
    Pd, Pc = Pals(d, 'dmg'), Pals(d, 'cgb')
    outs = {
        'metatiles.png': side_by_side(sheet_metatiles(d, Pd), sheet_metatiles(d, Pc)),
        'sprites.png': side_by_side(sheet_sprites(d, Pd), sheet_sprites(d, Pc)),
        'peter.png': side_by_side(sheet_peter(d, Pd), sheet_peter(d, Pc)),
        'font.png': side_by_side(sheet_font(d, Pd), sheet_font(d, Pc)),
        'title.png': side_by_side(screen_title(d, Pd), screen_title(d, Pd, 1),
                                  screen_title(d, Pc), screen_title(d, Pc, 1)),
        'scene.png': side_by_side(screen_level(d, Pd), screen_level(d, Pc)),
    }
    for fn, img in sorted(outs.items()):
        scale(img, a.scale).save(os.path.join(a.out, fn))
    print('preview: wrote %s (%s)' % (a.out, ', '.join(sorted(outs))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
