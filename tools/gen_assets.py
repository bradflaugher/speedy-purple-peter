#!/usr/bin/env python3
"""SPEEDY PURPLE PETER asset generator.

Reads the ASCII-art sources in ``assets/*.txt`` and writes ``src/gb/assets.c`` and
``src/gb/assets.h`` (GBDK-2020 / SDCC) exactly as specified in docs/CONTRACTS.md ("Assets").
Stdlib only and deterministic: the same input gives byte-identical output.

    python3 tools/gen_assets.py            # regenerate src/gb/assets.[ch]
    python3 tools/gen_assets.py --check    # exit 1 if the committed files are stale

Source format (every ``assets/*.txt`` file, read in sorted order)
-----------------------------------------------------------------
* Outside a block, a line starting with ``#`` is a comment. A blank line ends a block. Inside a
  block every line is a pixel row (so ``#`` pixels are fine there). Spaces inside a pixel row
  are ignored: use them to separate frames or glyphs visually.
* Pixels: ``.`` = colour 0, ``o`` = colour 1, ``+`` = colour 2, ``#`` = colour 3 (digits ``0-3``
  also work). On BG tiles colour 0 is the sky; on sprites it is transparent. The intent is the
  same everywhere: ``o`` light, ``+`` mid, ``#`` dark / outline (that is what DMG shows; on CGB
  each palette picks its own colours, see palettes.txt).
* Blocks (a header line, then pixel rows):

  ``@meta NAME... pal=P [pals=P,P,P,P] [frames=N]``      (tiles.txt)
      One or more 16x16 metatiles drawn together. The block is ``16*cols`` x ``16*rows``
      pixels and lists ``cols*rows`` names row-major (``cols`` is inferred from the width);
      ``-`` skips a cell (art drawn there only for context). ``pal`` is the CGB palette
      (``SKY GROUND BRICK GOLD TUBE DECOR HUD POLE``, or ``PAL_x``); ``pals`` sets the four
      quarters (TL, TR, BL, BR) separately. ``frames=4`` (single cell only) makes all four
      quarters animated BG tiles: the rows are then 4 frames side by side.
  ``@alias NAME = OTHER``   NAME uses OTHER's tiles and palette.
  ``@font CHARS``           8 rows of ``8*len(CHARS)`` pixels (one 8x8 glyph per char).
                            ``' '`` is always the blank tile; use ``\\`` escapes for nothing.
  ``@sprite SPR_NAME from=T_x``  a 16x16 sprite copy of metatile T_x (frame 0).
  ``@sprite SPR_NAME``      an 8x16, 16x16 or 16x32 frame (sprites.txt; file order = VRAM
                            order). Tiles go per 16-px band, per 8-px column, top then bottom
                            (8x16 OBJ mode): 16x16 = L, R(+2); 16x32 = TL, TR(+2), BL(+4), BR(+6).
  ``@titlekey C=NAME ...``  title scene legend: char -> T_* metatile (``.`` = T_SKY).
  ``@titlescene y=-1``      the title scene in metatiles (10 chars per row); ``y`` is the tile
                            row of the first metatile row (-1: metatile rows sit on odd rows).
  ``@titleart col=C row=R pal=P``  logo pixels placed at tile (C, R), overlaid on the scene.
                            Its tiles go into title_tiles (deduplicated; empty tiles are left
                            to the scene).
  ``@bgpal PAL_x c0 c1 c2 c3`` / ``@objpal OPAL_x c0 c1 c2 c3``  (``#rrggbb``)
  ``@dmg bgp=0x.. obp0=0x.. obp1=0x..``
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

BG_PALS = ['PAL_SKY', 'PAL_GROUND', 'PAL_BRICK', 'PAL_GOLD', 'PAL_TUBE', 'PAL_DECOR', 'PAL_HUD',
           'PAL_POLE']
OBJ_PALS = ['OPAL_PETER', 'OPAL_BLASTER', 'OPAL_GREEN', 'OPAL_RED', 'OPAL_ITEM', 'OPAL_GOLD',
            'OPAL_FX', 'OPAL_BLOCK']

# name -> (w, h). Every one must be defined in sprites.txt.
SPR_REQUIRED = {}
for _n in ('PS_STAND', 'PS_WALK0', 'PS_WALK1', 'PS_WALK2', 'PS_SKID', 'PS_JUMP', 'PS_DEAD', 'PB_DASH1',
           'PB_DUCK', 'GLOOP', 'GLOOP_FLAT', 'SHELL', 'SHELL_WAKE', 'COMET', 'CELL', 'BLASTER',
           'NOVA', '1UP', 'FLAG', 'BRICK', 'USED'):
    SPR_REQUIRED['SPR_' + _n] = (16, 16)
for _n in ('PB_STAND', 'PB_WALK0', 'PB_WALK1', 'PB_WALK2', 'PB_SKID', 'PB_JUMP', 'PB_DASH0', 'DOME0',
           'DOME1', 'CHOMP0', 'CHOMP1'):
    SPR_REQUIRED['SPR_' + _n] = (16, 32)
for _n in ('JET0', 'JET1', 'COIN0', 'COIN1', 'COIN2', 'SHOT', 'PUFF', 'SHARD', 'N10', 'N20',
           'N40', 'N50', 'N80', 'N1U', 'N0', 'N00', 'NP'):
    SPR_REQUIRED['SPR_' + _n] = (8, 16)

FONT_REQUIRED = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:!?/*@^x'
# 16 px wide frames that do not stand on the ground (all others must touch their bottom row)
FLOATING = ('SPR_FLAG', 'SPR_COMET')
TUBES = ('T_TUBE_TL', 'T_TUBE_TR', 'T_TUBE_L', 'T_TUBE_R')

MAX_BG_TILES = 176
MAX_SPR_TILES = 208
MAX_TITLE_TILES = 48
MAX_ANIM = 8
ANIM_FRAMES = 4
ANIM_PERIOD = 8
TITLE_BASE = 208
TEXT_ROWS = (11, 17)

PIX = {'.': 0, 'o': 1, '+': 2, '#': 3, '0': 0, '1': 1, '2': 2, '3': 3}


class AssetError(Exception):
    pass


# ------------------------------------------------------------------------------------------
# small helpers
# ------------------------------------------------------------------------------------------

def bg_number(i):
    """BG list index -> VRAM tile number (0x8800 addressing, sprites own 0..207)."""
    return i if i < 128 else i + 80


def encode_tile(tile):
    """8x8 tile (rows of colour indices) -> 16 bytes of GB 2bpp."""
    out = []
    for row in tile:
        lo = hi = 0
        for x, v in enumerate(row):
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        out.append(lo)
        out.append(hi)
    return out


def cut(pix, x, y):
    return tuple(tuple(pix[y + r][x:x + 8]) for r in range(8))


BLANK = tuple(tuple([0] * 8) for _ in range(8))


def rgb555(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def parse_color(s, where):
    m = re.match(r'^#([0-9a-fA-F]{6})$', s)
    if not m:
        raise AssetError('%s: bad colour %r (want #rrggbb)' % (where, s))
    v = m.group(1)
    return (int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16))


def pal_name(s, where, names=BG_PALS, prefix='PAL_'):
    n = s if s.startswith(prefix) else prefix + s
    if n not in names:
        raise AssetError('%s: unknown palette %r' % (where, s))
    return names.index(n)


def parse_tiles_h(path):
    """the T_* enum of src/core/tiles.h, in order (T_COUNT excluded)."""
    with open(path) as f:
        src = f.read()
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    m = re.search(r'enum\s*\{(.*?)\}', src, re.S)
    if not m:
        raise AssetError('%s: no enum found' % path)
    names = []
    for item in m.group(1).split(','):
        item = item.strip()
        if not item:
            continue
        name, _, val = item.partition('=')
        name = name.strip()
        if val.strip() and int(val.strip(), 0) != len(names):
            raise AssetError('%s: %s has an explicit value out of order' % (path, name))
        names.append(name)
    if not names or names[-1] != 'T_COUNT':
        raise AssetError('%s: enum must end with T_COUNT' % path)
    return names[:-1]


# ------------------------------------------------------------------------------------------
# parsing
# ------------------------------------------------------------------------------------------

class Block(object):
    def __init__(self, kind, args, opts, where):
        self.kind, self.args, self.opts, self.where = kind, args, opts, where
        self.rows = []

    def pixels(self):
        if not self.rows:
            raise AssetError('%s: @%s has no pixel rows' % (self.where, self.kind))
        out = []
        for y, r in enumerate(self.rows):
            row = []
            for ch in r:
                if ch not in PIX:
                    raise AssetError('%s: bad pixel %r in row %d' % (self.where, ch, y))
                row.append(PIX[ch])
            out.append(row)
        return out

    @property
    def w(self):
        return len(self.rows[0]) if self.rows else 0

    @property
    def h(self):
        return len(self.rows)


NOPIX = ('alias', 'bgpal', 'objpal', 'dmg', 'titlekey')
PIXKINDS = ('meta', 'font', 'sprite', 'titlescene', 'titleart')


def parse_file(path):
    blocks = []
    cur = None
    with open(path) as f:
        lines = f.read().split('\n')
    for ln, raw in enumerate(lines, 1):
        line = raw.rstrip()
        where = '%s:%d' % (os.path.basename(path), ln)
        if cur is None and line.startswith('#'):
            continue
        if not line.strip():
            cur = None
            continue
        if line.startswith('@'):
            toks = line[1:].split()
            kind = toks[0]
            if kind not in NOPIX + PIXKINDS:
                raise AssetError('%s: unknown block @%s' % (where, kind))
            args, opts = [], {}
            for t in toks[1:]:
                if '=' in t and kind not in ('alias', 'titlekey', 'font') and not t.startswith('#'):
                    k, v = t.split('=', 1)
                    opts[k] = v
                else:
                    args.append(t)
            if kind == 'font':
                args = [line[1:].split(None, 1)[1] if len(toks) > 1 else '']
            blk = Block(kind, args, opts, where)
            blocks.append(blk)
            cur = None if kind in NOPIX else blk
            continue
        if cur is None:
            raise AssetError('%s: pixel row outside a block' % where)
        row = line.strip().replace(' ', '')
        if cur.rows and len(row) != len(cur.rows[0]):
            raise AssetError('%s: row width %d != %d' % (where, len(row), len(cur.rows[0])))
        cur.rows.append(row)
    return blocks


# ------------------------------------------------------------------------------------------
# building
# ------------------------------------------------------------------------------------------

def load_assets(assets_dir=None, tiles_h=None):
    assets_dir = assets_dir or os.path.join(ROOT, 'assets')
    tiles_h = tiles_h or os.path.join(ROOT, 'src', 'core', 'tiles.h')
    t_names = parse_tiles_h(tiles_h)

    blocks = []
    files = sorted(fn for fn in os.listdir(assets_dir) if fn.endswith('.txt'))
    if not files:
        raise AssetError('%s: no .txt sources' % assets_dir)
    for fn in files:
        blocks.extend(parse_file(os.path.join(assets_dir, fn)))

    d = {'t_names': t_names}

    # ---- palettes ----
    bgpal, objpal, dmg = {}, {}, None
    for b in blocks:
        if b.kind in ('bgpal', 'objpal'):
            names, store = (BG_PALS, bgpal) if b.kind == 'bgpal' else (OBJ_PALS, objpal)
            if len(b.args) != 5 or b.args[0] not in names:
                raise AssetError('%s: want @%s NAME c0 c1 c2 c3' % (b.where, b.kind))
            if b.args[0] in store:
                raise AssetError('%s: %s defined twice' % (b.where, b.args[0]))
            store[b.args[0]] = [parse_color(c, b.where) for c in b.args[1:]]
        elif b.kind == 'dmg':
            try:
                dmg = dict((k, int(b.opts[k], 0)) for k in ('bgp', 'obp0', 'obp1'))
            except (KeyError, ValueError):
                raise AssetError('%s: want @dmg bgp=0x.. obp0=0x.. obp1=0x..' % b.where)
    for n in BG_PALS:
        if n not in bgpal:
            raise AssetError('missing @bgpal %s' % n)
    for n in OBJ_PALS:
        if n not in objpal:
            raise AssetError('missing @objpal %s' % n)
    if dmg is None:
        raise AssetError('missing @dmg')
    sky = bgpal['PAL_SKY'][0]
    for n in BG_PALS:
        if bgpal[n][0] != sky:
            raise AssetError('%s: colour 0 must equal PAL_SKY colour 0 (the sky)' % n)
    d['bgpal'] = [bgpal[n] for n in BG_PALS]
    d['objpal'] = [objpal[n] for n in OBJ_PALS]
    d['dmg'] = dmg

    # ---- metatiles: name -> {'q': [4 tiles], 'pals': [4], 'anim': [None|4 frames per q]} ----
    metas = {}
    aliases = []
    anim_src = []        # (meta block id, quarter) in definition order
    for bi, b in enumerate(blocks):
        if b.kind == 'alias':
            if len(b.args) != 3 or b.args[1] != '=':
                raise AssetError('%s: want @alias NAME = OTHER' % b.where)
            aliases.append((b.args[0], b.args[2], b.where))
            continue
        if b.kind != 'meta':
            continue
        if 'pal' not in b.opts and 'pals' not in b.opts:
            raise AssetError('%s: @meta needs pal=' % b.where)
        if 'pals' in b.opts:
            pals = [pal_name(p, b.where) for p in b.opts['pals'].split(',')]
            if len(pals) != 4:
                raise AssetError('%s: pals= needs 4 entries' % b.where)
        else:
            pals = [pal_name(b.opts['pal'], b.where)] * 4
        frames = int(b.opts.get('frames', '1'))
        pix = b.pixels()
        if b.h % 16 or b.w % (16 * frames):
            raise AssetError('%s: metatile block %dx%d is not a multiple of 16' % (b.where, b.w, b.h))
        cols, rows = b.w // (16 * frames), b.h // 16
        if len(b.args) != cols * rows:
            raise AssetError('%s: block holds %d metatiles but names %d' %
                             (b.where, cols * rows, len(b.args)))
        if frames not in (1, ANIM_FRAMES):
            raise AssetError('%s: frames must be 1 or %d' % (b.where, ANIM_FRAMES))
        if frames > 1 and cols * rows != 1:
            raise AssetError('%s: an animated block holds one metatile' % b.where)
        for i, name in enumerate(b.args):
            if name == '-':
                continue
            if name not in t_names:
                raise AssetError('%s: unknown level cell %s (not in tiles.h)' % (b.where, name))
            if name in metas:
                raise AssetError('%s: %s defined twice' % (b.where, name))
            cx, cy = (i % cols) * 16, (i // cols) * 16
            q, anim = [], []
            for qi, (ox, oy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
                if frames == 1:
                    q.append(cut(pix, cx + ox, cy + oy))
                    anim.append(None)
                else:
                    fr = [cut(pix, f * 16 + ox, oy) for f in range(frames)]
                    q.append(fr[0])
                    anim.append(fr)
                    anim_src.append((bi, qi))
            metas[name] = {'q': q, 'pals': pals, 'anim': anim, 'block': bi, 'where': b.where}
    for name, other, where in aliases:
        if name not in t_names:
            raise AssetError('%s: unknown level cell %s' % (where, name))
        if name in metas:
            raise AssetError('%s: %s defined twice' % (where, name))
        if other not in metas:
            raise AssetError('%s: alias of undefined %s' % (where, other))
        metas[name] = metas[other]
    for name in t_names:
        if name not in metas:
            raise AssetError('no metatile for %s (tiles.h)' % name)
    for name in TUBES:
        if name in metas:
            for t in metas[name]['q']:
                if any(v == 0 for row in t for v in row):
                    raise AssetError('%s: tubes must not use colour 0 (the Chomper shows '
                                     'through it)' % metas[name]['where'])
    d['metas'] = metas
    if len(anim_src) > MAX_ANIM:
        raise AssetError('%d animated BG tiles > %d' % (len(anim_src), MAX_ANIM))

    # ---- font ----
    glyphs = {' ': BLANK}
    for b in blocks:
        if b.kind != 'font':
            continue
        chars = b.args[0]
        pix = b.pixels()
        if b.h != 8 or b.w != 8 * len(chars):
            raise AssetError('%s: @font %r needs 8 rows of %d pixels' % (b.where, chars, 8 * len(chars)))
        for i, ch in enumerate(chars):
            if ch in glyphs:
                raise AssetError('%s: glyph %r defined twice' % (b.where, ch))
            if ord(ch) >= 128:
                raise AssetError('%s: glyph %r is not ASCII' % (b.where, ch))
            glyphs[ch] = cut(pix, i * 8, 0)
    for ch in FONT_REQUIRED:
        if ch not in glyphs:
            raise AssetError('font: missing glyph %r' % ch)
    d['glyphs'] = glyphs

    # ---- title (parsed before the BG list so its world tiles can go first) ----
    tkey = {'.': 'T_SKY'}
    scene = None
    arts = []
    for b in blocks:
        if b.kind == 'titlekey':
            for t in b.args:
                if len(t) < 3 or t[1] != '=' or t[2:] not in t_names:
                    raise AssetError('%s: bad @titlekey entry %r' % (b.where, t))
                tkey[t[0]] = t[2:]
        elif b.kind == 'titlescene':
            scene = b
        elif b.kind == 'titleart':
            arts.append(b)
    if scene is None:
        raise AssetError('missing @titlescene')
    scene_y = int(scene.opts.get('y', '0'))
    if scene.w != 10:
        raise AssetError('%s: the title scene is 10 metatiles wide' % scene.where)
    title_cells = []        # (tile x, tile y, metatile name, quarter)
    for my, row in enumerate(scene.rows):
        for mx, ch in enumerate(row):
            if ch not in tkey:
                raise AssetError('%s: title scene char %r has no @titlekey' % (scene.where, ch))
            for qi, (ox, oy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
                ty = scene_y + my * 2 + oy
                if 0 <= ty < 18:
                    title_cells.append((mx * 2 + ox, ty, tkey[ch], qi))
    covered = set((x, y) for x, y, _, _ in title_cells)
    if len(covered) != 20 * 18:
        raise AssetError('%s: the title scene must cover all 20x18 tiles' % scene.where)
    title_metas = []
    for _, _, n, _ in title_cells:
        if n not in title_metas:
            title_metas.append(n)

    # ---- BG tile list: blank, font, title-scene metatiles, other metatiles ----
    bg = []            # 8x8 tiles
    index = {}         # tile -> list index (static tiles only)
    anim_idx = {}      # (block id, quarter) -> list index

    def add(t):
        if t not in index:
            index[t] = len(bg)
            bg.append(t)
        return index[t]
    add(BLANK)
    font_idx = {}
    for ch in FONT_REQUIRED + ''.join(sorted(c for c in glyphs if c not in FONT_REQUIRED)):
        font_idx[ch] = add(glyphs[ch])
    order = title_metas + [n for n in t_names if n not in title_metas]
    mt_idx = {}
    for n in order:
        m = metas[n]
        idx = []
        for qi in range(4):
            if m['anim'][qi] is None:
                idx.append(add(m['q'][qi]))
            else:
                key = (m['block'], qi)
                if key not in anim_idx:
                    anim_idx[key] = len(bg)
                    bg.append(m['q'][qi])
                idx.append(anim_idx[key])
        mt_idx[n] = idx
    if len(bg) > MAX_BG_TILES:
        raise AssetError('BG tileset has %d tiles > %d (VRAM budget)' % (len(bg), MAX_BG_TILES))
    d['bg'] = bg
    d['font_idx'] = font_idx
    d['mt_idx'] = mt_idx
    d['anims'] = []    # (list index, [frames])
    for key in anim_src:
        bi, qi = key
        name = [n for n in t_names if metas[n]['block'] == bi][0]
        d['anims'].append((anim_idx[key], metas[name]['anim'][qi]))

    # ---- title tiles ----
    tmap = [[0] * 20 for _ in range(18)]
    tattr = [[0] * 20 for _ in range(18)]
    for x, y, n, qi in title_cells:
        i = mt_idx[n][qi]
        if i >= 128:
            raise AssetError('title scene uses %s, BG tile %d >= 128: numbers 208..255 are '
                             'overwritten by the title tiles' % (n, i))
        tmap[y][x] = bg_number(i)
        tattr[y][x] = metas[n]['pals'][qi]
    title_tiles = []
    tindex = {}
    for b in arts:
        pix = b.pixels()
        if b.w % 8 or b.h % 8:
            raise AssetError('%s: title art %dx%d not a multiple of 8' % (b.where, b.w, b.h))
        try:
            c0, r0 = int(b.opts['col']), int(b.opts['row'])
        except (KeyError, ValueError):
            raise AssetError('%s: @titleart needs col= and row=' % b.where)
        pal = pal_name(b.opts.get('pal', 'SKY'), b.where)
        for ty in range(b.h // 8):
            for tx in range(b.w // 8):
                x, y = c0 + tx, r0 + ty
                if not (0 <= x < 20 and 0 <= y < 18):
                    raise AssetError('%s: title art tile (%d,%d) off screen' % (b.where, x, y))
                t = cut(pix, tx * 8, ty * 8)
                if t == BLANK:
                    continue
                if 11 <= y <= 17:
                    raise AssetError('%s: title rows 11-17 are for engine text' % b.where)
                if t not in tindex:
                    tindex[t] = len(title_tiles)
                    title_tiles.append(t)
                tmap[y][x] = TITLE_BASE + tindex[t]
                tattr[y][x] = pal
    # rows 11-17 are the engine's text area: plain sky, in the HUD palette like the font
    for y in range(TEXT_ROWS[0], TEXT_ROWS[1] + 1):
        for x in range(20):
            if tmap[y][x] != bg_number(index[BLANK]):
                raise AssetError('title (%d,%d): rows %d-%d must be plain sky (engine text)' %
                                 ((x, y) + TEXT_ROWS))
            tattr[y][x] = BG_PALS.index('PAL_HUD')
    if len(title_tiles) > MAX_TITLE_TILES:
        raise AssetError('title uses %d unique tiles > %d' % (len(title_tiles), MAX_TITLE_TILES))
    for y in range(18):
        for x in range(20):
            v = tmap[y][x]
            if v >= TITLE_BASE and v - TITLE_BASE >= len(title_tiles):
                raise AssetError('title map (%d,%d) references a missing title tile' % (x, y))
            if 128 <= v < TITLE_BASE:
                raise AssetError('title map (%d,%d) uses tile %d' % (x, y, v))
    d['title_tiles'] = title_tiles
    d['title_map'] = tmap
    d['title_attr'] = tattr

    # ---- sprites ----
    spr = []
    spr_const = []     # (name, tile number, (w, h))
    seen = set()
    for b in blocks:
        if b.kind != 'sprite':
            continue
        if len(b.args) != 1:
            raise AssetError('%s: want @sprite SPR_NAME' % b.where)
        name = b.args[0]
        if name not in SPR_REQUIRED:
            raise AssetError('%s: unknown sprite %s' % (b.where, name))
        if name in seen:
            raise AssetError('%s: %s defined twice' % (b.where, name))
        seen.add(name)
        if 'from' in b.opts:            # a sprite copy of a metatile (bump animation)
            if b.rows:
                raise AssetError('%s: a from= sprite has no pixel rows' % b.where)
            if b.opts['from'] not in metas:
                raise AssetError('%s: unknown metatile %s' % (b.where, b.opts['from']))
            q = metas[b.opts['from']]['q']
            b.rows = [''.join('.o+#'[v] for v in q[(y // 8) * 2][y % 8] + q[(y // 8) * 2 + 1][y % 8])
                      for y in range(16)]
        pix = b.pixels()
        if (b.w, b.h) != SPR_REQUIRED[name]:
            raise AssetError('%s: %s is %dx%d, want %dx%d' %
                             ((b.where, name, b.w, b.h) + SPR_REQUIRED[name]))
        if b.w == 16 and name not in FLOATING and not any(pix[-1]):
            raise AssetError('%s: %s does not stand on its bottom pixel row' % (b.where, name))
        spr_const.append((name, len(spr), (b.w, b.h)))
        for by in range(b.h // 16):
            for tx in range(b.w // 8):
                spr.append(cut(pix, tx * 8, by * 16))
                spr.append(cut(pix, tx * 8, by * 16 + 8))
    for name in sorted(SPR_REQUIRED):
        if name not in seen:
            raise AssetError('sprites: missing %s' % name)
    if len(spr) > MAX_SPR_TILES:
        raise AssetError('sprites use %d tiles > %d' % (len(spr), MAX_SPR_TILES))
    d['spr'] = spr
    d['spr_const'] = spr_const
    return d


# ------------------------------------------------------------------------------------------
# output
# ------------------------------------------------------------------------------------------

def c_bytes(vals, per_line=16, indent='    '):
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append(indent + ','.join('0x%02X' % v for v in vals[i:i + per_line]) + ',')
    return '\n'.join(lines)


def gen(d):
    t_names = d['t_names']
    n_bg = len(d['bg'])
    lo = min(n_bg, 128)
    hi = n_bg - lo
    head = '/* GENERATED by tools/gen_assets.py from the assets/ sources - do not edit. */\n'

    h = [head, '#ifndef SPP_ASSETS_H', '#define SPP_ASSETS_H', '',
         '#include <stdint.h>', '#include "tiles.h"                 /* T_COUNT */', '',
         '/* All of this lives in a switchable ROM bank: read it with the bank switched in',
         '   (BANK(assets)); the engine copies what it needs at run time to RAM. */',
         '#ifdef __SDCC', '#include <gbdk/platform.h>', 'BANKREF_EXTERN(assets)', '#endif', '',
         '/* ---- world BG tileset (also used by the HUD window and menus) ---- */',
         '#define BG_TILE_COUNT %d' % n_bg,
         '#define BG_TILES_LO   %d' % lo,
         '#define BG_TILES_HI   %d' % hi,
         'extern const uint8_t bg_tiles[];',
         'extern const uint8_t mt_tiles[T_COUNT][4];',
         'extern const uint8_t mt_attr[T_COUNT][4];', '',
         '/* font: ASCII -> BG tile number (unmapped -> TILE_BLANK) */',
         'extern const uint8_t font_map[128];',
         '#define TILE_BLANK %d' % bg_number(d['font_idx'][' ']), '',
         '/* animated BG tiles: every ANIM_PERIOD frames, BG tile anim_tile[i] gets pattern',
         '   anim_frames[i][f], f = 0..ANIM_FRAMES-1 */',
         '#define ANIM_COUNT  %d' % len(d['anims']),
         '#define ANIM_FRAMES %d' % ANIM_FRAMES,
         '#define ANIM_PERIOD %d' % ANIM_PERIOD,
         'extern const uint8_t anim_tile[ANIM_COUNT];',
         'extern const uint8_t anim_frames[ANIM_COUNT][ANIM_FRAMES][16];', '',
         '/* ---- sprites (8x16 OBJ mode): tile number of the top-left 8x16 object ---- */',
         '#define SPR_TILE_COUNT %d' % len(d['spr'])]
    for name, n, (w, hh) in d['spr_const']:
        h.append('#define %-16s %3d   /* %dx%d */' % (name, n, w, hh))
    h += ['extern const uint8_t spr_tiles[];', '',
          '/* ---- palettes ---- */']
    for i, n in enumerate(BG_PALS):
        h.append('#define %-12s %d' % (n, i))
    for i, n in enumerate(OBJ_PALS):
        h.append('#define %-12s %d' % (n, i))
    h += ['extern const uint16_t cgb_bg_pal[8][4];',
          'extern const uint16_t cgb_obj_pal[8][4];',
          '#define DMG_BGP  0x%02X' % d['dmg']['bgp'],
          '#define DMG_OBP0 0x%02X   /* Peter, enemies, items */' % d['dmg']['obp0'],
          '#define DMG_OBP1 0x%02X   /* the blaster suit, flashing */' % d['dmg']['obp1'], '',
          '/* ---- title screen: title_tiles load at BG tile number 208 ---- */',
          '#define TITLE_TILE_COUNT %d' % len(d['title_tiles']),
          'extern const uint8_t title_tiles[];',
          'extern const uint8_t title_map[18][20];',
          'extern const uint8_t title_attr[18][20];', '',
          '#endif', '']

    c = [head, '#pragma bank 255', '#include "assets.h"', '', 'BANKREF(assets)', '']
    allb = []
    for t in d['bg']:
        allb += encode_tile(t)
    c += ['const uint8_t bg_tiles[%d] = {' % (16 * n_bg), c_bytes(allb), '};', '']

    c.append('const uint8_t mt_tiles[T_COUNT][4] = {')
    for n in t_names:
        c.append('    {%s},  /* %s */' % (','.join('%3d' % bg_number(i) for i in d['mt_idx'][n]), n))
    c += ['};', '', 'const uint8_t mt_attr[T_COUNT][4] = {']
    for n in t_names:
        c.append('    {%s},  /* %s */' % (','.join(str(p) for p in d['metas'][n]['pals']), n))
    c += ['};', '']

    fm = [bg_number(d['font_idx'][' '])] * 128
    for ch, i in d['font_idx'].items():
        fm[ord(ch)] = bg_number(i)
    c += ['const uint8_t font_map[128] = {', c_bytes(fm), '};', '']

    c.append('const uint8_t anim_tile[ANIM_COUNT] = {%s};' %
             ', '.join(str(bg_number(i)) for i, _ in d['anims']))
    c += ['', 'const uint8_t anim_frames[ANIM_COUNT][ANIM_FRAMES][16] = {']
    for i, frames in d['anims']:
        c.append('    {')
        for f in frames:
            c.append('        {%s},' % ','.join('0x%02X' % v for v in encode_tile(f)))
        c.append('    },')
    c += ['};', '']

    sb = []
    for t in d['spr']:
        sb += encode_tile(t)
    c += ['const uint8_t spr_tiles[%d] = {' % (16 * len(d['spr'])), c_bytes(sb), '};', '']

    for arr, pals, names in (('cgb_bg_pal', d['bgpal'], BG_PALS), ('cgb_obj_pal', d['objpal'], OBJ_PALS)):
        c.append('const uint16_t %s[8][4] = {' % arr)
        for n, p in zip(names, pals):
            c.append('    {%s},  /* %s */' % (', '.join('0x%04X' % rgb555(x) for x in p), n))
        c += ['};', '']

    tb = []
    for t in d['title_tiles']:
        tb += encode_tile(t)
    if not tb:
        tb = [0] * 16
    c += ['const uint8_t title_tiles[%d] = {' % len(tb), c_bytes(tb), '};', '']
    for arr, grid in (('title_map', d['title_map']), ('title_attr', d['title_attr'])):
        c.append('const uint8_t %s[18][20] = {' % arr)
        for row in grid:
            c.append('    {%s},' % ','.join('%3d' % v for v in row))
        c += ['};', '']
    return '\n'.join(h), '\n'.join(c)


def report(d):
    return ('BG %d/%d tiles (lo %d, hi %d), anim %d/%d, sprites %d/%d tiles, title %d/%d tiles'
            % (len(d['bg']), MAX_BG_TILES, min(len(d['bg']), 128), max(0, len(d['bg']) - 128),
               len(d['anims']), MAX_ANIM, len(d['spr']), MAX_SPR_TILES,
               len(d['title_tiles']), MAX_TITLE_TILES))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets'))
    ap.add_argument('--tiles-h', default=os.path.join(ROOT, 'src', 'core', 'tiles.h'))
    ap.add_argument('--out-dir', default=os.path.join(ROOT, 'src', 'gb'))
    ap.add_argument('--check', action='store_true', help='fail if the outputs are stale')
    ap.add_argument('-q', '--quiet', action='store_true')
    a = ap.parse_args(argv)
    try:
        d = load_assets(a.assets, a.tiles_h)
    except AssetError as e:
        sys.stderr.write('gen_assets: error: %s\n' % e)
        return 2
    h, c = gen(d)
    outs = (('assets.h', h), ('assets.c', c))
    if a.check:
        stale = []
        for fn, txt in outs:
            p = os.path.join(a.out_dir, fn)
            if not os.path.exists(p) or open(p).read() != txt:
                stale.append(fn)
        if stale:
            sys.stderr.write('gen_assets: stale: %s (run tools/gen_assets.py)\n' % ', '.join(stale))
            return 1
        return 0
    if not os.path.isdir(a.out_dir):
        os.makedirs(a.out_dir)
    for fn, txt in outs:
        with open(os.path.join(a.out_dir, fn), 'w', newline='\n') as f:
            f.write(txt)
    if not a.quiet:
        print('gen_assets: ' + report(d))
    return 0


if __name__ == '__main__':
    sys.exit(main())
