"""End-to-end tests: boot the real ROM in PyBoy (headless, DMG and CGB) and play it.

The host build of the simulation is the oracle. `build/sppgen path` lets the search bot play
some sectors on the host and writes the buttons it pressed, one line per frame, plus the state it
ended in. The ROM is fed exactly those buttons (by its own step counter, so a slow frame cannot
shift them) and must end in exactly the same state: same position, score, sectors, coins. The
same runs measure the frame budget (frames that missed their VBlank).

Run: make test-rom   (needs `pip install pyboy pillow numpy`)
"""
import io
import os
import subprocess
import unittest

from pyboy import PyBoy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, 'build', 'speedy-purple-peter.gb')
SYM = os.path.join(ROOT, 'build', 'speedy-purple-peter.sym')
SPPGEN = os.path.join(ROOT, 'build', 'sppgen')
SHOTS = os.path.join(ROOT, 'build', 'screens')
SRAM_SIZE = 8192
SEED = 0x1985                     # the title's default seed on a fresh save

# keep in step with dbg_world_off[] in src/gb/main.c
FIELDS = ['px', 'py', 'pvx', 'pvy', 'power', 'pstate', 'lives', 'score', 'frames', 'sectors_done',
          'dist', 'time', 'cam_x', 'over', 'coins', 'seed', 'ground', 'nova_t', 'cam_y', 'god', 'size',
          'check_col', 'gen_col', 'lv', 'bonus', 'gen', 'e', 'mode']
SIZES = {'px': 2, 'py': -2, 'pvx': -2, 'pvy': -2, 'power': 1, 'pstate': 1, 'lives': 1, 'score': 4,
         'frames': 4, 'sectors_done': 2, 'dist': 2, 'time': 2, 'cam_x': 2, 'over': 1, 'coins': 1,
         'seed': 2, 'ground': 1, 'nova_t': 2, 'cam_y': 1, 'god': 1,
         'check_col': 2, 'gen_col': 2, 'bonus': 2, 'mode': 1}
BUTTONS = [(0x01, 'right'), (0x02, 'left'), (0x04, 'up'), (0x08, 'down'), (0x10, 'a'), (0x20, 'b'),
           (0x40, 'select'), (0x80, 'start')]
PS_PLAY, PS_GROW, PS_SHRINK, PS_DEAD, PS_OVER = range(5)


class Game:
    def __init__(self, cgb, sram=None, seed=None):
        self.ram = io.BytesIO(sram if sram is not None else bytes(SRAM_SIZE))
        self.pb = PyBoy(ROM, window='null', cgb=cgb, symbols=SYM, sound_emulated=False, ram_file=self.ram)
        self.cgb = cgb
        bank, addr = self.pb.symbol_lookup('_dbg_world_off')
        self.off = {f: self.pb.memory[bank, addr + 2 * i] | self.pb.memory[bank, addr + 2 * i + 1] << 8
                    for i, f in enumerate(FIELDS)}
        self.W = self.addr('_W')
        self.held = 0
        if seed is not None:           # the title starts on this seed (as if it were the last one played)
            a, saved = self.addr('_last_seed'), self.addr('_seed_saved')
            def poke(_):
                self.pb.memory[a] = seed & 0xFF
                self.pb.memory[a + 1] = seed >> 8
                self.pb.memory[saved] = 1
            self.pb.hook_register(*self.pb.symbol_lookup('_title_screen'), poke, None)

    def addr(self, name):
        return self.pb.symbol_lookup(name)[1]

    def u(self, a, n):
        return sum(self.pb.memory[a + i] << (8 * i) for i in range(n))

    def var(self, name, n=2):
        return self.u(self.addr(name), n)

    def w(self, field):
        n = SIZES[field]
        v = self.u(self.W + self.off[field], abs(n))
        if n < 0 and v >= 1 << (8 * -n - 1):
            v -= 1 << (8 * -n)
        return v

    def poke(self, field, value):
        n = abs(SIZES[field])
        for i in range(n):
            self.pb.memory[self.W + self.off[field] + i] = (value >> (8 * i)) & 0xFF

    def set_keys(self, k):
        for bit, name in BUTTONS:
            if (k & bit) and not (self.held & bit):
                self.pb.button_press(name)
            elif not (k & bit) and (self.held & bit):
                self.pb.button_release(name)
        self.held = k

    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()

    def press(self, k, frames=2):
        self.set_keys(k)
        self.tick(frames)
        self.set_keys(0)
        self.tick(2)

    def start_run(self, mode=0):
        """from boot: the title (START), the mode menu (DOWN to the mode, START), into the run"""
        self.tick(150)                      # boot and the title
        self.press(0x80)
        self.tick(8)
        for _ in range(3):
            if self.var('_play_mode', 1) == mode:
                break
            self.press(0x08)                # down: the next mode
        self.set_keys(0x80)
        for _ in range(240):
            self.tick()
            if self.var('_hud_on', 1):
                break
        self.set_keys(0)

    def shot(self, name):
        os.makedirs(SHOTS, exist_ok=True)
        self.pb.screen.image.save(os.path.join(SHOTS, name))

    def stop(self, save=False):
        if save:                            # the cartridge RAM into self.ram (a fresh Game can boot it)
            self.ram.seek(0)
            self.pb.stop(save=True, ram_file=self.ram)
        else:
            self.pb.stop(save=False)


def host_path(sectors, seed=SEED):
    out = os.path.join(ROOT, 'build', 'path_%d_%04x.txt' % (sectors, seed))
    line = subprocess.check_output([SPPGEN, 'path', str(seed), str(sectors), out], text=True)
    toks = line.split()
    state = {toks[i]: int(toks[i + 1]) for i in range(0, len(toks) - 1, 2)}
    with open(out) as f:
        keys = [int(x, 16) for x in f.read().split()]
    return keys, state


# the frame budget runs: eight sectors on each of these seeds, on both machines
BUDGET_SEEDS = [0x1985, 0x1234, 0xBEEF, 0x0042, 0x7A11, 0xC0DE, 0x2024, 0x9999]


class TestRom(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys, cls.expect = host_path(2)

    def play_path(self, cgb, keys, seed=SEED):
        g = Game(cgb, seed=seed)
        g.start_run()
        self.assertEqual(g.w('seed'), seed)
        n = len(keys)
        ring = g.addr('_dbg_ring')
        g.pb.memory[g.addr('_dbg_stop_at')] = 0
        g.pb.memory[g.addr('_dbg_stop_at') + 1] = 0
        fed = 0
        d0 = g.var('_dbg_drops')
        g.pb.memory[g.addr('_dbg_feed')] = 1
        guard = 0
        while True:
            s = g.var('_dbg_steps')
            # keep the ring filled up to 200 steps ahead, and the stop mark just past it
            while fed < n and fed < s + 200:
                g.pb.memory[ring + (fed & 255)] = keys[fed]
                fed += 1
            stop = fed
            g.pb.memory[g.addr('_dbg_stop_at')] = stop & 0xFF
            g.pb.memory[g.addr('_dbg_stop_at') + 1] = stop >> 8
            if s >= n:
                break
            g.tick(16)
            guard += 1
            self.assertLess(guard, n, 'the ROM stopped stepping')
        g.tick(4)       # the step counter goes up as a step begins: let the last one finish
        drops = g.var('_dbg_drops') - d0
        return g, drops

    def state(self, g):
        return {'px': g.w('px'), 'py': g.w('py'), 'score': g.w('score'), 'sectors': g.w('sectors_done'),
                'frames': g.w('frames'), 'coins': g.w('coins'), 'lives': g.w('lives'), 'power': g.w('power'),
                'dist': g.w('dist'), 'time': g.w('time')}

    def check_same_as_host(self, cgb):
        g, drops = self.play_path(cgb, self.keys)
        try:
            got = self.state(g)
            self.assertEqual(got, {k: self.expect[k] for k in got})
            g.shot('%s_after_two_sectors.png' % ('cgb' if cgb else 'dmg'))
            self.assertEqual(drops, 0, "%d slow frames in %d" % (drops, len(self.keys)))
        finally:
            g.stop()

    def test_dmg_plays_like_the_host(self):
        self.check_same_as_host(False)

    def test_cgb_plays_like_the_host(self):
        self.check_same_as_host(True)

    def test_frame_budget(self):
        """Eight sectors on eight seeds, each the same as on the host. The CGB (double speed) never
        misses a frame; the DMG at most one in 20,000 (busy moments: a crowd, a bump, the time bonus)."""
        for cgb in (False, True):
            total = drops = 0
            report = []
            for seed in BUDGET_SEEDS:
                keys, expect = host_path(8, seed)
                g, d = self.play_path(cgb, keys, seed)
                try:
                    got = self.state(g)
                    self.assertEqual(got, {k: expect[k] for k in got}, 'seed %04X' % seed)
                finally:
                    g.stop()
                total += len(keys)
                drops += d
                report.append('%04X:%d' % (seed, d))
            print('\n  %s: %d slow frames in %d (%s)' % ('CGB' if cgb else 'DMG', drops, total, ' '.join(report)))
            self.assertLessEqual(drops, 0 if cgb else total // 20000)

    def test_title_and_seed_entry(self):
        g = Game(True)
        try:
            g.tick(150)
            g.shot('cgb_title.png')
            g.press(0x02)          # left: edit the last digit
            g.press(0x04)          # up: +1
            g.press(0x80)          # to the mode menu
            g.tick(8)
            g.shot('cgb_modes.png')
            g.press(0x80)          # classic
            for _ in range(240):
                g.tick()
                if g.var('_hud_on', 1):
                    break
            self.assertEqual(g.w('seed'), (SEED & 0xFFF0) | ((SEED + 1) & 0xF))
        finally:
            g.stop()

    def test_pause_freezes_and_restart(self):
        g = Game(False)
        try:
            g.start_run()
            g.set_keys(0x21)
            g.tick(60)
            g.set_keys(0)
            # the window's HUD: PETER (row 0) and the score with the gap after it (row 1)
            spots = list(range(0, 8)) + list(range(32, 42))
            hud = [g.pb.memory[0x9C00 + i] for i in spots]
            g.press(0x80)              # pause
            f = g.w('frames')
            g.tick(60)
            self.assertEqual(g.w('frames'), f)
            g.shot('dmg_pause.png')
            g.press(0x80)              # resume
            g.tick(10)
            self.assertGreater(g.w('frames'), f)
            # the HUD is back as it was (no stray seed digits; nothing scored meanwhile)
            self.assertEqual([g.pb.memory[0x9C00 + i] for i in spots], hud)
            g.press(0x80)              # pause, then SELECT restarts the same seed
            g.press(0x40)
            for _ in range(120):
                g.tick()
                if g.w('frames') < 20:
                    break
            self.assertLess(g.w('frames'), 20)
            self.assertEqual(g.w('seed'), SEED)
        finally:
            g.stop()

    def test_modes(self):
        """The mode menu: B goes back to the seed, DOWN picks a mode, and the run plays it: in
        auto run Peter runs right at full speed with no buttons held, and the mode is saved."""
        g = Game(False)
        try:
            g.tick(150)
            g.press(0x80)          # to the mode menu
            g.tick(8)
            g.press(0x20)          # B: back to the seed
            g.tick(8)
            g.press(0x80)
            g.tick(8)
            g.press(0x08)          # auto sprint
            g.press(0x08)          # auto run
            self.assertEqual(g.var('_play_mode', 1), 2)
            g.shot('dmg_modes.png')
            g.press(0x80)
            for _ in range(240):
                g.tick()
                if g.var('_hud_on', 1):
                    break
            self.assertEqual(g.w('mode'), 2)
            x0 = g.w('px')
            g.tick(120)
            self.assertGreater(g.w('px') - x0, 150)       # running, nothing pressed
            self.assertEqual(g.w('pvx'), 0x2900)
        finally:
            g.stop(save=True)
        g2 = Game(False, g.ram.getvalue())                 # the mode is remembered
        try:
            g2.tick(150)
            self.assertEqual(g2.var('_play_mode', 1), 2)
        finally:
            g2.stop()

    def test_game_over_saves_the_best(self):
        g = Game(False)
        try:
            g.start_run()
            g.poke('lives', 1)
            g.poke('time', 2)                # the clock runs out in a moment
            g.poke('score', 12340)           # (a best worth saving)
            g.set_keys(0x21)
            for _ in range(600):
                g.tick()
                if g.w('over'):
                    break
            self.assertEqual(g.w('over'), 1)
            g.set_keys(0)
            score = g.w('score')
            self.assertGreater(score, 0)
            g.tick(400)                      # the game over jingle, then the card
            g.shot('dmg_game_over.png')
            self.assertEqual(g.var('_best_score', 4), score)
            g.press(0x80)
            g.tick(60)
        finally:
            g.stop(save=True)
        sram = g.ram.getvalue()
        g2 = Game(False, sram)               # a fresh boot reads the best back
        try:
            g2.tick(150)
            self.assertEqual(g2.var('_best_score', 4), score)
        finally:
            g2.stop()


if __name__ == '__main__':
    unittest.main()
