"""Tests for the SPEEDY PURPLE PETER asset pipeline (tools/gen_assets.py). Stdlib unittest.

Run: python3 -m unittest discover -s tests -p 'test_assets.py'
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_assets as G  # noqa: E402

LCC = '/opt/gbdk/bin/lcc'


def c_array(src, name):
    """integer values of `const uintN_t name[...]... = {...};`"""
    m = re.search(r'const\s+uint(?:8|16)_t\s+%s\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};' % name, src, re.S)
    if not m:
        raise AssertionError('array %s not found' % name)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = body.replace('{', ' ').replace('}', ' ')
    return [int(v, 0) for v in body.replace('\n', ' ').split(',') if v.strip()]


class TestAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='spp_assets_')
        assert G.main(['--out-dir', cls.tmp, '-q']) == 0
        with open(os.path.join(cls.tmp, 'assets.h')) as f:
            cls.h = f.read()
        with open(os.path.join(cls.tmp, 'assets.c')) as f:
            cls.c = f.read()
        cls.d = G.load_assets()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def define(self, name):
        m = re.search(r'^#define\s+%s\s+(\S+)' % name, self.h, re.M)
        self.assertIsNotNone(m, 'missing #define %s' % name)
        return int(m.group(1), 0)

    def bg_tile(self, number):
        """BG tile pixels by VRAM tile number"""
        return self.d['bg'][number if number < 128 else number - 80]

    # ---- pipeline ------------------------------------------------------------------------
    def test_encode_tile(self):
        t = [[0, 1, 2, 3, 0, 1, 2, 3]] + [[3] * 8] + [[0] * 8] * 6
        e = G.encode_tile(t)
        self.assertEqual(len(e), 16)
        self.assertEqual(e[0:2], [0b01010101, 0b00110011])
        self.assertEqual(e[2:4], [0xFF, 0xFF])
        self.assertEqual(e[4:], [0] * 12)

    def test_bg_tiles_match_the_encoding(self):
        raw = c_array(self.c, 'bg_tiles')
        for i, t in enumerate(self.d['bg']):
            self.assertEqual(raw[i * 16:i * 16 + 16], G.encode_tile(t))

    def test_rgb555(self):
        self.assertEqual(G.rgb555((255, 255, 255)), 0x7FFF)
        self.assertEqual(G.rgb555((255, 0, 0)), 0x001F)
        self.assertEqual(G.rgb555((0, 0, 255)), 0x7C00)

    def test_deterministic(self):
        h1, c1 = G.gen(G.load_assets())
        h2, c2 = G.gen(G.load_assets())
        self.assertEqual(h1, h2)
        self.assertEqual(c1, c2)
        self.assertEqual(h1, self.h)
        self.assertEqual(c1, self.c)
        d2 = tempfile.mkdtemp(prefix='spp_assets2_')
        try:
            self.assertEqual(G.main(['--out-dir', d2, '-q']), 0)
            for fn in ('assets.h', 'assets.c'):
                with open(os.path.join(self.tmp, fn), 'rb') as a, open(os.path.join(d2, fn), 'rb') as b:
                    self.assertEqual(a.read(), b.read(), fn)
        finally:
            shutil.rmtree(d2, ignore_errors=True)

    def test_generated_files_up_to_date(self):
        for fn, txt in (('assets.h', self.h), ('assets.c', self.c)):
            p = os.path.join(ROOT, 'src', 'gb', fn)
            self.assertTrue(os.path.exists(p), '%s missing: run tools/gen_assets.py' % p)
            with open(p) as f:
                self.assertEqual(f.read(), txt, '%s is stale: run tools/gen_assets.py' % fn)

    def test_bad_input_rejected(self):
        d = tempfile.mkdtemp(prefix='spp_bad_')
        try:
            for fn in os.listdir(os.path.join(ROOT, 'assets')):
                shutil.copy(os.path.join(ROOT, 'assets', fn), d)
            self.assertIsNotNone(G.load_assets(d))
            cases = [
                '@meta T_NOPE pal=SKY\n' + ('.' * 16 + '\n') * 16,          # unknown cell
                '@meta T_SKY pal=SKY\n' + ('.' * 16 + '\n') * 16,           # defined twice
                '@meta T_SKY pal=SKY\n.......\n',                           # bad size
                '@sprite SPR_NOPE\n' + ('.' * 8 + '\n') * 16,               # unknown sprite
                '@bgpal PAL_SKY #000000 #000000 #000000 #000000\n',         # palette twice
            ]
            for extra in cases:
                with open(os.path.join(d, 'zz_bad.txt'), 'w') as f:
                    f.write(extra)
                self.assertRaises(G.AssetError, G.load_assets, d)
            os.remove(os.path.join(d, 'zz_bad.txt'))
            # a tube with colour 0 is refused
            p = os.path.join(d, 'tiles.txt')
            with open(p) as f:
                src = f.read()
            i = src.index('@meta T_TUBE_TL')
            j = src.index('\n', src.index('\n', i) + 1)
            with open(p, 'w') as f:
                f.write(src[:j + 1] + '.' + src[j + 2:])
            self.assertRaisesRegex(G.AssetError, 'colour 0', G.load_assets, d)
        finally:
            shutil.rmtree(d)

    def test_parse_tiles_h_order(self):
        with open(os.path.join(ROOT, 'src', 'core', 'tiles.h')) as f:
            src = f.read()
        body = src[src.index('enum {'):src.index('T_COUNT')]
        names = re.findall(r'\b(T_[A-Z0-9_]+)', body)
        self.assertEqual(G.parse_tiles_h(os.path.join(ROOT, 'src', 'core', 'tiles.h')), names)
        self.assertEqual(self.d['t_names'], names)

    # ---- contract ------------------------------------------------------------------------
    def test_contract_symbols(self):
        with open(os.path.join(ROOT, 'docs', 'CONTRACTS.md')) as f:
            doc = f.read()
        block = doc[doc.index('## Assets'):doc.index('## Sound')]
        for name in re.findall(r'#define\s+(\w+)', block):
            self.assertRegex(self.h, r'#define\s+%s\b' % name)
        for name in re.findall(r'extern\s+const\s+\w+\s+(\w+)\s*\[', block):
            self.assertRegex(self.h, r'extern\s+const\s+\w+\s+%s\s*\[' % name)
            self.assertRegex(self.c, r'const\s+\w+\s+%s\s*\[' % name)
        for i, n in enumerate(['PAL_SKY', 'PAL_GROUND', 'PAL_BRICK', 'PAL_GOLD', 'PAL_TUBE',
                               'PAL_DECOR', 'PAL_HUD', 'PAL_POLE']):
            self.assertEqual(self.define(n), i)
        for i, n in enumerate(['OPAL_PETER', 'OPAL_BLASTER', 'OPAL_GREEN', 'OPAL_RED',
                               'OPAL_ITEM', 'OPAL_GOLD', 'OPAL_FX', 'OPAL_BLOCK']):
            self.assertEqual(self.define(n), i)
        self.assertIn('#include "tiles.h"', self.h)
        self.assertIn('#pragma bank 255', self.c)          # assets live in a switchable bank
        self.assertIn('BANKREF(assets)', self.c)

    def test_every_sprite_constant(self):
        # every SPR_* named in the contract table
        with open(os.path.join(ROOT, 'docs', 'CONTRACTS.md')) as f:
            doc = f.read()
        names = set()
        for a, b, c in re.findall(r'`(SPR_[A-Z0-9_]+?)(\d)\.\.(\d)`', doc):
            for k in range(int(b), int(c) + 1):
                names.add(a + str(k))
        names |= set(n for n in re.findall(r'`(SPR_[A-Z0-9_]+)`', doc) if not n.endswith('_'))
        self.assertEqual(names - {'SPR_TILE_COUNT'}, set(G.SPR_REQUIRED))
        n = self.define('SPR_TILE_COUNT')
        for name, (w, h) in G.SPR_REQUIRED.items():
            v = self.define(name)
            self.assertEqual(v % 2, 0, '%s must be even (8x16 objects)' % name)
            self.assertLessEqual(v + (w // 8) * (h // 16) * 2, n, name)
        # frames do not overlap
        spans = sorted((self.define(nm), self.define(nm) + (w // 8) * (h // 16) * 2)
                       for nm, (w, h) in G.SPR_REQUIRED.items())
        for (a0, a1), (b0, b1) in zip(spans, spans[1:]):
            self.assertLessEqual(a1, b0)

    def test_sprites_stand_on_their_bottom_row(self):
        for name, t0, (w, h) in self.d['spr_const']:
            if w != 16 or name in G.FLOATING:
                continue
            # bottom 8 rows of the last band = the 'bottom' tile of each column
            base = t0 + (h // 16 - 1) * 4
            bottom = [self.d['spr'][base + 1][7], self.d['spr'][base + 3][7]]
            self.assertTrue(any(any(r) for r in bottom), '%s floats' % name)

    def test_budgets(self):
        n = self.define('BG_TILE_COUNT')
        self.assertLessEqual(n, 176)
        self.assertEqual(self.define('BG_TILES_LO'), min(n, 128))
        self.assertEqual(self.define('BG_TILES_HI'), n - min(n, 128))
        self.assertLessEqual(self.define('SPR_TILE_COUNT'), 208)
        self.assertLessEqual(self.define('TITLE_TILE_COUNT'), 48)
        self.assertLessEqual(self.define('ANIM_COUNT'), 8)
        self.assertEqual(self.define('ANIM_FRAMES'), 4)
        self.assertEqual(self.define('ANIM_PERIOD'), 8)
        self.assertEqual(len(c_array(self.c, 'bg_tiles')), 16 * n)
        self.assertEqual(len(c_array(self.c, 'spr_tiles')), 16 * self.define('SPR_TILE_COUNT'))
        self.assertEqual(len(c_array(self.c, 'title_tiles')), 16 * self.define('TITLE_TILE_COUNT'))
        self.assertEqual(len(c_array(self.c, 'anim_frames')), 64 * self.define('ANIM_COUNT'))

    def test_bg_tiles_deduplicated(self):
        anim = set(i for i, _ in self.d['anims'])
        static = [t for i, t in enumerate(self.d['bg']) if i not in anim]
        self.assertEqual(len(static), len(set(static)))

    def test_every_cell_has_a_metatile(self):
        n = self.define('BG_TILE_COUNT')
        names = self.d['t_names']
        tiles = c_array(self.c, 'mt_tiles')
        attrs = c_array(self.c, 'mt_attr')
        self.assertEqual(len(tiles), len(names) * 4)
        self.assertEqual(len(attrs), len(names) * 4)
        valid = set(G.bg_number(i) for i in range(n))
        for t in tiles:
            self.assertIn(t, valid)
            self.assertFalse(128 <= t < 208, 'tile %d is in sprite VRAM' % t)
        for a in attrs:
            self.assertLessEqual(a, 7, 'mt_attr is a palette number (no flips / bank bits)')
        # the mt_tiles comments follow tiles.h order
        self.assertEqual(re.findall(r'\},\s*/\* (T_\w+) \*/', self.c.split('mt_attr')[0]), names)

    def test_blank_and_sky(self):
        blank = self.define('TILE_BLANK')
        self.assertEqual(self.bg_tile(blank), G.BLANK)
        sky = c_array(self.c, 'mt_tiles')[0:4]
        self.assertEqual(sky, [blank] * 4)
        names = self.d['t_names']
        hid = names.index('T_HIDDEN_1UP')
        self.assertEqual(c_array(self.c, 'mt_tiles')[hid * 4:hid * 4 + 4], [blank] * 4)

    def test_anims(self):
        at = c_array(self.c, 'anim_tile')
        self.assertEqual(len(at), self.define('ANIM_COUNT'))
        self.assertEqual(len(set(at)), len(at))
        # the engine writes them in groups of 4 consecutive tile numbers (one VBlank copy each)
        self.assertEqual(len(at) % 4, 0)
        for g in range(0, len(at), 4):
            self.assertEqual(at[g:g + 4], list(range(at[g], at[g] + 4)), 'anim group %d not consecutive' % (g // 4))
            self.assertTrue(at[g] + 3 < 128 or at[g] >= 208, 'anim group %d straddles the VRAM halves' % (g // 4))
        tiles = c_array(self.c, 'mt_tiles')
        names = self.d['t_names']
        for cell in ('T_Q_COIN', 'T_Q_POWER', 'T_COIN'):
            i = names.index(cell)
            for t in tiles[i * 4:i * 4 + 4]:
                self.assertIn(t, at, '%s must animate' % cell)
        for i, frames in self.d['anims']:
            self.assertEqual(frames[0], self.d['bg'][i])
            self.assertGreater(len(set(frames)), 1, 'anim tile %d does not move' % i)
        # animated tiles are never shared by a cell that should not animate
        for k, n in enumerate(names):
            if n in ('T_Q_COIN', 'T_Q_POWER', 'T_COIN'):
                continue
            for t in tiles[k * 4:k * 4 + 4]:
                self.assertNotIn(t, at, n)

    def test_tubes_have_no_colour0(self):
        for name in G.TUBES:
            for t in self.d['metas'][name]['q']:
                self.assertNotIn(0, [v for row in t for v in row], name)

    def test_bump_sprites_match_the_bg(self):
        for spr, cell in (('SPR_BRICK', 'T_BRICK'), ('SPR_USED', 'T_USED')):
            t0 = self.define(spr)
            q = self.d['metas'][cell]['q']
            self.assertEqual(self.d['spr'][t0:t0 + 4], [q[0], q[2], q[1], q[3]], spr)
            self.assertEqual(self.d['metas'][cell]['pals'][0], self.define('PAL_BRICK'))
        bg = c_array(self.c, 'cgb_bg_pal')
        obj = c_array(self.c, 'cgb_obj_pal')
        b, o = self.define('PAL_BRICK'), self.define('OPAL_BLOCK')
        self.assertEqual(bg[b * 4 + 1:b * 4 + 4], obj[o * 4 + 1:o * 4 + 4])

    # ---- palettes ----------------------------------------------------------------------------
    def test_bg_colour0_is_the_sky_everywhere(self):
        bg = c_array(self.c, 'cgb_bg_pal')
        self.assertEqual(len(bg), 32)
        self.assertEqual(len(set(bg[i * 4] for i in range(8))), 1)
        self.assertEqual(len(c_array(self.c, 'cgb_obj_pal')), 32)
        for v in bg + c_array(self.c, 'cgb_obj_pal'):
            self.assertLessEqual(v, 0x7FFF)
        # the CGB sky is dark (deep space), DMG colour 0 is white
        sky = bg[0]
        r, g, b = sky & 31, (sky >> 5) & 31, (sky >> 10) & 31
        self.assertLess(r + g + b, 24)
        self.assertGreater(b, r)
        self.assertEqual(self.define('DMG_BGP') & 3, 0)

    def test_peter_is_purple(self):
        p = c_array(self.c, 'cgb_obj_pal')[self.define('OPAL_PETER') * 4 + 2]
        r, g, b = p & 31, (p >> 5) & 31, (p >> 10) & 31
        self.assertGreater(r, g)
        self.assertGreater(b, g)

    # ---- font --------------------------------------------------------------------------------
    def test_font(self):
        fm = c_array(self.c, 'font_map')
        self.assertEqual(len(fm), 128)
        blank = self.define('TILE_BLANK')
        self.assertEqual(fm[ord(' ')], blank)
        self.assertEqual(fm[0], blank)
        seen = {}
        for ch in G.FONT_REQUIRED.replace(' ', ''):
            t = fm[ord(ch)]
            self.assertNotEqual(t, blank, 'glyph %r missing' % ch)
            self.assertNotEqual(self.bg_tile(t), G.BLANK)
            self.assertNotIn(t, seen, '%r and %r share a tile' % (ch, seen.get(t)))
            seen[t] = ch
            self.assertLess(t, 128, 'font tiles must stay below 128 (the title keeps them)')
        for ch in '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:!?/*@^x':
            self.assertIn(ch, G.FONT_REQUIRED)

    # ---- title -------------------------------------------------------------------------------
    def test_title(self):
        n = self.define('TITLE_TILE_COUNT')
        tm = c_array(self.c, 'title_map')
        ta = c_array(self.c, 'title_attr')
        self.assertEqual(len(tm), 360)
        self.assertEqual(len(ta), 360)
        nbg = self.define('BG_TILE_COUNT')
        blank = self.define('TILE_BLANK')
        for i, v in enumerate(tm):
            if v >= 208:
                self.assertLess(v - 208, n)
            else:
                self.assertLess(v, min(nbg, 128), 'title (%d,%d) uses world tile %d' % (i % 20, i // 20, v))
        for a in ta:
            self.assertLessEqual(a, 7)
        self.assertTrue(any(v >= 208 for v in tm), 'no logo')
        # rows 11-17: plain sky for the engine's text
        for y in range(11, 18):
            self.assertEqual(tm[y * 20:y * 20 + 20], [blank] * 20, 'title row %d' % y)

    # ---- the compiler ------------------------------------------------------------------------
    @unittest.skipUnless(os.path.exists(LCC), 'GBDK not installed')
    def test_compiles_with_gbdk(self):
        out = os.path.join(self.tmp, 'assets.o')
        r = subprocess.run([LCC, '-I' + os.path.join(ROOT, 'src', 'core'), '-I' + self.tmp, '-c',
                            '-o', out, os.path.join(self.tmp, 'assets.c')],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertNotIn('warning', r.stdout.lower(), r.stdout)


if __name__ == '__main__':
    unittest.main()
